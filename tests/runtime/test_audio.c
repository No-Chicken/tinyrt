#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks, failures, plays;
static tinyrt_status_t reply;
#define CHECK(x) do {++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);}} while(0)
static tinyrt_status_t play(void *ctx,uint32_t offset,uint32_t length,uint32_t rate) {
    (void)ctx;CHECK(rate==16000);++plays;
    if(offset>32000 || length>32000-offset)return TINYRT_INVALID_ARGUMENT;
    return reply;
}
static tinyrt_runtime_t *create(const char *name,uint32_t permission,tinyrt_status_t status) {
    char path[1024];uint8_t bytes[8192];snprintf(path,sizeof(path),"%s/audio_%s.wasm",FIXTURE_DIR,name);
    FILE *f=fopen(path,"rb");if(!f)exit(2);uint32_t n=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);
    tinyrt_package_policy_t policy={1,permission,2,100000};
    tinyrt_runtime_host_t host={0};host.audio_play=play;tinyrt_runtime_t *r=NULL;
    CHECK(tinyrt_runtime_create(bytes,n,&policy,&host,&r)==status);return r;
}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);size_t baseline=tinyrt_runtime_memory_used();
    CHECK(!create("valid",15,TINYRT_VERIFY_FAILED));
    const char *bad[]={"zero","odd","long","rate","wrap","range"};
    for(unsigned i=0;i<6;++i){tinyrt_runtime_t *r=create(bad[i],31,TINYRT_OK);
        if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED);tinyrt_runtime_destroy(r);}}
    for(unsigned busy=0;busy<2;++busy){reply=busy?TINYRT_BUSY:TINYRT_OK;
        tinyrt_runtime_t *r=create("valid",31,TINYRT_OK);
        if(r){unsigned before=plays;CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);CHECK(plays==before+1);tinyrt_runtime_destroy(r);}}
    reply=TINYRT_OK;
    tinyrt_runtime_t *spam=create("spam",31,TINYRT_OK);
    if(spam){unsigned before=plays;CHECK(tinyrt_runtime_init(spam,466,466)==TINYRT_OK);
        CHECK(plays==before+4);tinyrt_runtime_destroy(spam);}
    const char *stages[]={"render","stop","event"};
    for(unsigned i=0;i<3;++i){tinyrt_runtime_t *r=create(stages[i],31,TINYRT_OK);if(!r)continue;
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);unsigned before=plays;static tinyrt_frame_t frame;
        if(i==0)CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED);
        if(i==1)CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);
        if(i==2)CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
        CHECK(plays==before+(i==2?1u:0u));tinyrt_runtime_destroy(r);}
    CHECK(tinyrt_runtime_memory_used()==baseline);tinyrt_runtime_system_shutdown();
    printf("AUDIO checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
