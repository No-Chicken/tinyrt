#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,failures,pulses,stops;
static tinyrt_status_t reply;
#define CHECK(x) do {++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);}} while(0)
static tinyrt_status_t pulse(void *ctx,uint32_t strength,uint32_t duration) {
    (void)ctx;CHECK(strength>=1 && strength<=100 && duration>=1 && duration<=2000);++pulses;return reply;
}
static tinyrt_status_t stop(void *ctx) {(void)ctx;++stops;return reply;}
static tinyrt_runtime_t *create(const char *name,uint32_t permissions,int available,tinyrt_status_t expected) {
    char path[1024];uint8_t bytes[8192];snprintf(path,sizeof(path),"%s/haptic_%s.wasm",FIXTURE_DIR,name);
    FILE *f=fopen(path,"rb");if(!f)exit(2);uint32_t n=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);
    tinyrt_package_policy_t policy={1,permissions,2,100000};tinyrt_runtime_host_t host={0};
    if(available){host.haptic_pulse=pulse;host.haptic_stop=stop;}
    tinyrt_runtime_t *r=NULL;CHECK(tinyrt_runtime_create(bytes,n,&policy,&host,&r)==expected);return r;
}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);size_t baseline=tinyrt_runtime_memory_used();
    CHECK(!create("valid",31,1,TINYRT_VERIFY_FAILED));
    const char *bad[]={"zero","strength","duration","long","negative"};
    for(unsigned i=0;i<5;++i){tinyrt_runtime_t *r=create(bad[i],63,1,TINYRT_OK);
        if(r){unsigned before=pulses;CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED);CHECK(pulses==before);tinyrt_runtime_destroy(r);}}
    const char *good[]={"valid","maximum","cancel","event","result"};
    for(unsigned i=0;i<5;++i){reply=i==4?TINYRT_BUSY:TINYRT_OK;tinyrt_runtime_t *r=create(good[i],63,1,TINYRT_OK);
        if(r){unsigned before=pulses,was_stops=stops;CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
            if(i==3)CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
            CHECK(pulses==before+(i==2?0u:1u));CHECK(stops==was_stops+(i==2?1u:0u));tinyrt_runtime_destroy(r);}}
    tinyrt_runtime_t *r=create("result",63,0,TINYRT_OK);
    if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);tinyrt_runtime_destroy(r);}
    const char *stages[]={"render","stop"};reply=TINYRT_OK;
    for(unsigned i=0;i<2;++i){r=create(stages[i],63,1,TINYRT_OK);if(!r)continue;
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);unsigned before=pulses;static tinyrt_frame_t frame;
        CHECK((i?tinyrt_runtime_stop(r):tinyrt_runtime_render(r,&frame))==TINYRT_VERIFY_FAILED);
        CHECK(pulses==before);tinyrt_runtime_destroy(r);}
    reply=TINYRT_IO_ERROR;r=create("valid",63,1,TINYRT_OK);
    if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED);tinyrt_runtime_destroy(r);}
    CHECK(tinyrt_runtime_memory_used()==baseline);tinyrt_runtime_system_shutdown();
    printf("HAPTIC checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
