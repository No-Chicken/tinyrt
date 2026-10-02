#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,failures;
#define CHECK(x) do {++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);}} while(0)
static tinyrt_frame_t frame;
static tinyrt_runtime_t *create(const char *name,uint32_t permissions,tinyrt_status_t expected) {
    char path[1024];uint8_t bytes[8192];snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,name);
    FILE *f=fopen(path,"rb");if(!f)exit(2);uint32_t n=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);
    tinyrt_package_policy_t policy={1,permissions,2,100000};tinyrt_runtime_host_t host={0};tinyrt_runtime_t *r=NULL;
    CHECK(tinyrt_runtime_create(bytes,n,&policy,&host,&r)==expected);return r;
}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);size_t baseline=tinyrt_runtime_memory_used();
    tinyrt_runtime_t *r=create("input_subscribed",15,TINYRT_OK);
    if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
#ifdef TINYRT_INPUT_EVENTS_MASK
        CHECK(tinyrt_runtime_input_events(r)==0x38);
#endif
        for(int kind=3;kind<=5;++kind){CHECK(tinyrt_runtime_event(r,kind,10,20,0)==TINYRT_OK);
            CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.commands[0].rgb==(uint32_t)kind);}
        CHECK(tinyrt_runtime_event(r,3,-1,0,0)==TINYRT_INVALID_ARGUMENT);
        CHECK(tinyrt_runtime_event(r,4,0,466,0)==TINYRT_INVALID_ARGUMENT);
        CHECK(tinyrt_runtime_event(r,6,0,0,0)==TINYRT_INVALID_ARGUMENT);
        CHECK(tinyrt_runtime_event(r,1,0,0,0)==TINYRT_OK);
        tinyrt_runtime_destroy(r);}
    const char *defaults[]={"pixel_valid","input_disabled"};
    for(unsigned i=0;i<2;++i){r=create(defaults[i],15,TINYRT_OK);if(!r)continue;
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
#ifdef TINYRT_INPUT_EVENTS_MASK
        CHECK(tinyrt_runtime_input_events(r)==0);
#endif
        CHECK(tinyrt_runtime_event(r,3,0,0,0)==TINYRT_INVALID_ARGUMENT);
        tinyrt_runtime_destroy(r);}
    const char *bad[]={"input_bad_1","input_bad_8","input_bad_16","input_bad_32","input_bad_55","input_bad_64","input_bad_-1"};
    for(unsigned i=0;i<7;++i){r=create(bad[i],15,TINYRT_OK);if(!r)continue;
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED);tinyrt_runtime_destroy(r);}
    const char *stages[]={"input_event","input_render","input_stop"};
    for(unsigned i=0;i<3;++i){r=create(stages[i],15,TINYRT_OK);if(!r)continue;
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
        if(i==0)CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_VERIFY_FAILED);
        if(i==1)CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED&&frame.count==0);
        if(i==2)CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);
        tinyrt_runtime_destroy(r);}
    CHECK(!create("input_subscribed",13,TINYRT_VERIFY_FAILED));
    CHECK(tinyrt_runtime_memory_used()==baseline);tinyrt_runtime_system_shutdown();
    printf("INPUT checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
