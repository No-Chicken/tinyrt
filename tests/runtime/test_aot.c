#include "tinyrt_runtime.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,imports;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);} } while(0)
typedef struct { HANDLE stop,thread; void (*cancel)(void *);void *arg;unsigned arms,disarms;uint32_t ms; } guard_t;
static DWORD WINAPI watchdog(void *arg) {guard_t *g=arg;if(WaitForSingleObject(g->stop,g->ms)==WAIT_TIMEOUT)g->cancel(g->arg);return 0;}
static tinyrt_status_t arm(void *ctx,void (*cancel)(void *),void *arg,uint32_t ms) {
    guard_t *g=ctx;g->cancel=cancel;g->arg=arg;g->ms=ms;++g->arms;
    g->stop=CreateEvent(NULL,TRUE,FALSE,NULL);if(!g->stop)return TINYRT_NO_MEMORY;
    g->thread=CreateThread(NULL,0,watchdog,g,0,NULL);
    if(!g->thread){CloseHandle(g->stop);g->stop=NULL;return TINYRT_NO_MEMORY;}return TINYRT_OK;
}
static void disarm(void *ctx) {guard_t *g=ctx;++g->disarms;SetEvent(g->stop);WaitForSingleObject(g->thread,INFINITE);CloseHandle(g->thread);CloseHandle(g->stop);g->thread=g->stop=NULL;g->cancel=NULL;g->arg=NULL;}
static uint32_t now(void *ctx) {(void)ctx;++imports;Sleep(2);return 10;}
static int32_t get(void *ctx,uint32_t k,int32_t f) {(void)ctx;(void)k;return f;}
static tinyrt_status_t put(void *ctx,uint32_t k,int32_t v) {(void)ctx;(void)k;(void)v;return TINYRT_OK;}
static tinyrt_package_policy_t policy={1,15,2,100000};
static tinyrt_runtime_host_t host={NULL,get,put,now};
static tinyrt_package_aot_profile_t profile={true,false,5,"x86_64","generic",{1}};
static tinyrt_package_aot_metadata_t metadata={5,7,"x86_64","generic",{1},{1},{1},{1},{1},{1},{1}};
static guard_t guard_state;
static tinyrt_execution_guard_t guard={&guard_state,arm,disarm,75};
static unsigned char bytes[262144];static uint32_t size;
static tinyrt_frame_t frame,front;
static void read_fixture(const char *name) {char path[1024];snprintf(path,sizeof(path),"%s/%s.aot",AOT_FIXTURE_DIR,name);FILE *f=fopen(path,"rb");if(!f)exit(2);size=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);}
static tinyrt_status_t create(tinyrt_runtime_t **out) {
#ifdef TINYRT_RUNTIME_AOT_VERSION
    tinyrt_runtime_aot_config_t config={&profile,&guard};
    return tinyrt_runtime_create_aot(bytes,size,&policy,&host,&config,&metadata,out);
#else
    (void)profile;(void)metadata;(void)guard;
    return tinyrt_runtime_create(bytes,size,&policy,&host,out);
#endif
}
static tinyrt_status_t read_bytes(void *ctx,uint32_t off,void *out,uint32_t n) {(void)ctx;if(off>size||n>size-off)return TINYRT_IO_ERROR;memcpy(out,bytes+off,n);return TINYRT_OK;}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);size_t base=tinyrt_runtime_memory_used();
    read_fixture("frame_observed");tinyrt_runtime_t *r=NULL;
    CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&r)==TINYRT_VERIFY_FAILED && !r);
    CHECK(create(&r)==TINYRT_OK && r!=NULL);
    if(!r){tinyrt_runtime_system_shutdown();return 1;}
    CHECK(tinyrt_runtime_memory_used()>base+65536);
    CHECK(tinyrt_runtime_set_execution_guard(r,NULL)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK && frame.count==2 && frame.pixel_bytes==8);
    CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
    CHECK(tinyrt_runtime_stop(r)==TINYRT_OK);
    CHECK(guard_state.arms==4 && guard_state.disarms==4 && !guard_state.thread);
    tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==base);
    const char *reject[]={"nomax","huge","ctor","post","start","unknown"};
    for(unsigned i=0;i<sizeof(reject)/sizeof(*reject);++i){read_fixture(reject[i]);r=NULL;CHECK(create(&r)==TINYRT_VERIFY_FAILED && !r);tinyrt_runtime_destroy(r);r=NULL;CHECK(tinyrt_runtime_memory_used()==base);}
#ifdef TINYRT_RUNTIME_AOT_VERSION
    read_fixture("frame_observed");tinyrt_store_io_t io={0};io.read=read_bytes;
    tinyrt_runtime_aot_config_t config={&profile,&guard};
    CHECK(tinyrt_runtime_validate_aot(&config,&io,0,size,&policy,&metadata)==TINYRT_OK);
    CHECK(tinyrt_runtime_memory_used()==base);
    config.guard=NULL;CHECK(tinyrt_runtime_create_aot(bytes,size,&policy,&host,&config,&metadata,&r)==TINYRT_INVALID_ARGUMENT && !r);
    CHECK(tinyrt_runtime_validate_aot(&config,&io,0,size,&policy,&metadata)==TINYRT_INVALID_ARGUMENT);
    config.guard=&guard;metadata.compat_id[0]=2;
    CHECK(tinyrt_runtime_create_aot(bytes,size,&policy,&host,&config,&metadata,&r)==TINYRT_VERIFY_FAILED && !r);
    metadata.compat_id[0]=1;metadata.safety_flags=3;CHECK(create(&r)==TINYRT_VERIFY_FAILED && !r);metadata.safety_flags=7;
    profile.enabled=false;CHECK(create(&r)==TINYRT_INVALID_ARGUMENT && !r);profile.enabled=true;
    bytes[4]^=1;CHECK(create(&r)==TINYRT_VERIFY_FAILED && !r);bytes[4]^=1;
#else
    (void)read_bytes;
#endif
    const struct {const char *name;unsigned stage;int cancelled;} faults[]={
        {"guard_pure",2,1},{"guard_import",2,1},{"guard_init",0,1},{"guard_event",1,1},{"guard_render",2,1},{"guard_stop",3,1},
        {"pure_init",0,1},{"pure_event",1,1},{"pure_render",2,1},{"pure_stop",3,1},
        {"trap_init",0,0},{"trap_event",1,0},{"trap_render",2,0},{"trap_stop",3,0}};
    for(unsigned i=0;i<sizeof(faults)/sizeof(*faults);++i){read_fixture(faults[i].name);r=NULL;memset(&guard_state,0,sizeof(guard_state));CHECK(create(&r)==TINYRT_OK);
        ULONGLONG start=GetTickCount64();tinyrt_status_t s=tinyrt_runtime_init(r,466,466);
        if(faults[i].stage){CHECK(s==TINYRT_OK);if(faults[i].stage==1)s=tinyrt_runtime_event(r,2,0,0,0);else if(faults[i].stage==3)s=tinyrt_runtime_stop(r);else s=tinyrt_runtime_render(r,&frame);}
        CHECK(s==TINYRT_VERIFY_FAILED && GetTickCount64()-start<1000);
        CHECK(strstr(tinyrt_runtime_last_error(r),faults[i].cancelled?"cancel":"unreachable")!=NULL);
        CHECK(guard_state.arms==guard_state.disarms && !guard_state.thread);
        tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==base);
        read_fixture("frame_observed");CHECK(create(&r)==TINYRT_OK);CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
        CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==base);
    }
    read_fixture("pixel_maximum");CHECK(create(&r)==TINYRT_OK);CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK && frame.pixel_bytes==122880);
    for(unsigned i=0;i<frame.pixel_bytes;++i)if(frame.pixels[i]!=(uint8_t)i){CHECK(0);break;}
    tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==base);
    read_fixture("pixel_then_skip");CHECK(create(&r)==TINYRT_OK);CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_render(r,&front)==TINYRT_OK);CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
    memset(&frame,0xa5,sizeof(frame));CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK && !frame.count);CHECK(front.count==2 && front.pixel_bytes==8);tinyrt_runtime_destroy(r);
    read_fixture("frame_partial_failure");CHECK(create(&r)==TINYRT_OK);CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED && !frame.count);CHECK(front.count==2 && front.pixel_bytes==8);tinyrt_runtime_destroy(r);
    for(unsigned i=0;i<50;++i){read_fixture("pixel_valid");CHECK(create(&r)==TINYRT_OK);CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==base);}
    CHECK(tinyrt_runtime_memory_peak()<=TINYRT_RUNTIME_HEAP_LIMIT);tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);
    printf("AOT checks=%u failures=%u imports=%u\n",checks,failures,imports);return failures?1:0;
}
