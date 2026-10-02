#include "tinyrt_runtime.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,imports;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);} } while(0)
typedef struct {
    HANDLE stop,thread;
    void (*cancel)(void *);void *cancel_ctx;
    unsigned arms,disarms,completed;
    int fail_arm;
    uint32_t timeout_ms;
} guard_t;
static DWORD WINAPI watchdog(void *arg) {
    guard_t *g=arg;
    if(WaitForSingleObject(g->stop,g->timeout_ms)==WAIT_TIMEOUT) {
        g->cancel(g->cancel_ctx);
        Sleep(20); /* Disarm must join an already-running cancellation. */
        ++g->completed;
    }
    return 0;
}
static tinyrt_status_t arm(void *ctx,void (*cancel)(void *),void *arg,uint32_t ms) {
    guard_t *g=ctx;++g->arms;
    if(g->fail_arm)return TINYRT_IO_ERROR;
    g->cancel=cancel;g->cancel_ctx=arg;g->timeout_ms=ms;
    g->stop=CreateEvent(NULL,TRUE,FALSE,NULL);if(!g->stop)return TINYRT_NO_MEMORY;
    g->thread=CreateThread(NULL,0,watchdog,g,0,NULL);
    if(!g->thread){CloseHandle(g->stop);g->stop=NULL;return TINYRT_NO_MEMORY;}
    return TINYRT_OK;
}
static void disarm(void *ctx) {
    guard_t *g=ctx;++g->disarms;SetEvent(g->stop);
    CHECK(WaitForSingleObject(g->thread,INFINITE)==WAIT_OBJECT_0);
    CloseHandle(g->thread);CloseHandle(g->stop);g->thread=g->stop=NULL;
    g->cancel=NULL;g->cancel_ctx=NULL;
}
static uint32_t now(void *ctx) {(void)ctx;++imports;Sleep(2);return 123;}
static tinyrt_frame_t frame;
static tinyrt_runtime_t *create(const char *name,guard_t *g) {
    char path[1024];uint8_t bytes[8192];snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,name);
    FILE *f=fopen(path,"rb");if(!f)exit(2);uint32_t n=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);
    tinyrt_package_policy_t policy={1,15,2,100000};tinyrt_runtime_host_t host={NULL,NULL,NULL,now};
    tinyrt_runtime_t *r=NULL;CHECK(tinyrt_runtime_create(bytes,n,&policy,&host,&r)==TINYRT_OK);
#ifdef TINYRT_EXECUTION_GUARD_VERSION
    tinyrt_execution_guard_t config={g,arm,disarm,75};
    CHECK(tinyrt_runtime_set_execution_guard(r,&config)==TINYRT_OK);
#else
    (void)g;(void)arm;(void)disarm;
#endif
    return r;
}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);size_t baseline=tinyrt_runtime_memory_used();
    guard_t g={0};tinyrt_runtime_t *r=create("frame_observed",&g);
    CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
    CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
    CHECK(tinyrt_runtime_stop(r)==TINYRT_OK);
    CHECK(g.arms==4 && g.disarms==4 && g.completed==0 && !g.thread);
    tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==baseline);
    if(failures){tinyrt_runtime_system_shutdown();return 1;}
    const char *names[]={"guard_init","guard_event","guard_render","guard_stop"};
    for(unsigned i=0;i<4;++i) {
        memset(&g,0,sizeof(g));r=create(names[i],&g);imports=0;
        LARGE_INTEGER begin,end,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&begin);
        tinyrt_status_t status=tinyrt_runtime_init(r,466,466);
        if(i){CHECK(status==TINYRT_OK);
            if(i==1)status=tinyrt_runtime_event(r,2,0,0,0);
            if(i==2)status=tinyrt_runtime_render(r,&frame);
            if(i==3)status=tinyrt_runtime_stop(r);
        }
        QueryPerformanceCounter(&end);
        double ms=(double)(end.QuadPart-begin.QuadPart)*1000.0/(double)freq.QuadPart;
        CHECK(status==TINYRT_VERIFY_FAILED && imports>1 && ms>=75 && ms<500);
        CHECK(g.completed==1 && g.arms==(i?2u:1u) && g.disarms==g.arms);
        CHECK(!g.thread && !g.cancel && !g.cancel_ctx);
        CHECK(strstr(tinyrt_runtime_last_error(r),"cancel")!=NULL);
        unsigned arms=g.arms;status=tinyrt_runtime_render(r,&frame);
        CHECK(status==(i==0||i==3?TINYRT_INVALID_ARGUMENT:TINYRT_VERIFY_FAILED) && frame.count==0);
        CHECK(g.arms==arms);tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==baseline);
        printf("GUARD %s elapsed_ms=%.3f imports=%u\n",names[i],ms,imports);
    }
    for(unsigned i=0;i<50;++i) {
        memset(&g,0,sizeof(g));r=create("frame_observed",&g);
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
        CHECK(tinyrt_runtime_stop(r)==TINYRT_OK);
        CHECK(g.arms==2 && g.disarms==2 && !g.thread);
        tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==baseline);
    }
    memset(&g,0,sizeof(g));g.fail_arm=1;r=create("frame_observed",&g);
    CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_IO_ERROR && g.arms==1 && g.disarms==0);
    CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);tinyrt_runtime_destroy(r);
    CHECK(tinyrt_runtime_memory_used()==baseline);tinyrt_runtime_system_shutdown();
    printf("GUARD checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
