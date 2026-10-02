#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,failures,observations;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);} } while(0)
static uint8_t bytes[8192];
static uint32_t size;
static tinyrt_frame_t first,second,snapshot;
static const tinyrt_frame_t *observed;
static uint32_t now(void *ctx) {
    (void)ctx;
    if(observed) {
        ++observations;
        CHECK(observed->count==2 && observed->pixel_bytes==8);
        CHECK(observed->commands[0].kind==TINYRT_DRAW_CLEAR && observed->commands[0].rgb==0x123456);
        CHECK(observed->commands[1].kind==TINYRT_DRAW_RGB565 && observed->pixels[1]==0xf8);
    }
    return 123;
}
static tinyrt_package_policy_t policy={1,15,2,100000};
static tinyrt_runtime_host_t host={NULL,NULL,NULL,now};
static tinyrt_runtime_t *create(const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,name);
    FILE *f=fopen(path,"rb");if(!f)exit(2);
    size=(uint32_t)fread(bytes,1,sizeof(bytes),f);CHECK(!ferror(f)&&fgetc(f)==EOF);fclose(f);
    tinyrt_runtime_t *r=NULL;CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&r)==TINYRT_OK);return r;
}
static int filled(const void *data,size_t length,uint8_t value) {
    const uint8_t *p=data;
    for(size_t i=0;i<length;++i)if(p[i]!=value)return 0;
    return 1;
}
static void tails(const tinyrt_frame_t *frame,uint8_t value) {
    CHECK(filled(frame->commands+frame->count,
        (TINYRT_FRAME_MAX_COMMANDS-frame->count)*sizeof(frame->commands[0]),value));
    CHECK(filled(frame->pixels+frame->pixel_bytes,TINYRT_FRAME_MAX_PIXEL_BYTES-frame->pixel_bytes,value));
    tinyrt_draw_command_t clear={0};clear.kind=TINYRT_DRAW_CLEAR;clear.rgb=0x123456;
    CHECK(!memcmp(&frame->commands[0],&clear,sizeof(clear)));
    tinyrt_draw_command_t pixel={0};pixel.kind=TINYRT_DRAW_RGB565;pixel.x=10;pixel.y=20;pixel.w=2;pixel.h=2;
    CHECK(!memcmp(&frame->commands[1],&pixel,sizeof(pixel)));
}
static void caller_owned_frames(void) {
    size_t baseline=tinyrt_runtime_memory_used();
    for(unsigned generation=0;generation<2;++generation) {
        tinyrt_runtime_t *r=create("frame_observed");if(!r)continue;
        printf("FRAME_MEMORY generation=%u live_bytes=%zu frame_bytes=%zu\n",generation,
               tinyrt_runtime_memory_used()-baseline,sizeof(first));
        tinyrt_runtime_t *other=NULL;
        CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&other)==TINYRT_BUSY && !other);
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
        memset(&first,0xa5,sizeof(first));observed=&first;
        CHECK(tinyrt_runtime_render(r,&first)==TINYRT_OK && first.count==2 && first.pixel_bytes==8);
        observed=NULL;tails(&first,0xa5);
        snapshot=first;
        CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
        CHECK(!memcmp(&first,&snapshot,sizeof(first)));
        memset(&second,0x5a,sizeof(second));observed=&second;
        CHECK(tinyrt_runtime_render(r,&second)==TINYRT_OK && second.count==2 && second.pixel_bytes==8);
        observed=NULL;tails(&second,0x5a);
        CHECK(!memcmp(&first,&snapshot,sizeof(first)));
        /* Reuse the last output while non-render callbacks execute. */
        memset(&second,0xcc,sizeof(second));
        CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
        CHECK(tinyrt_runtime_stop(r)==TINYRT_OK);
        CHECK(filled(&second,sizeof(second),0xcc));
        tinyrt_runtime_destroy(r);
        CHECK(filled(&second,sizeof(second),0xcc));
        CHECK(!memcmp(&first,&snapshot,sizeof(first)));
        CHECK(tinyrt_runtime_memory_used()==baseline);
    }
    CHECK(observations==4);
}
static void rejected_outputs(void) {
    const char *names[]={"frame_partial_failure","frame_empty","skip_clear"};
    size_t baseline=tinyrt_runtime_memory_used();
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i) {
        tinyrt_runtime_t *r=create(names[i]);if(!r)continue;
        memset(&second,0xa5,sizeof(second));
        CHECK(tinyrt_runtime_render(r,&second)==TINYRT_INVALID_ARGUMENT && second.count==0);
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
        CHECK(tinyrt_runtime_render(r,&second)==TINYRT_VERIFY_FAILED && second.count==0);
        CHECK(tinyrt_runtime_last_error(r)[0]);
        memset(&second,0x5a,sizeof(second));
        CHECK(tinyrt_runtime_render(r,&second)==TINYRT_VERIFY_FAILED && second.count==0);
        CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);
        tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==baseline);
    }
    memset(&second,0xa5,sizeof(second));
    CHECK(tinyrt_runtime_render(NULL,&second)==TINYRT_INVALID_ARGUMENT && second.count==0);
    CHECK(tinyrt_runtime_render(NULL,NULL)==TINYRT_INVALID_ARGUMENT);
}
static void output_released_before_other_callbacks(void) {
    const char *names[]={"pixel_event","skip_event","rr_event","pixel_stop","skip_stop","stop_draw"};
    size_t baseline=tinyrt_runtime_memory_used();
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i) {
        tinyrt_runtime_t *r=create(names[i]);if(!r)continue;
        CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
        CHECK(tinyrt_runtime_render(r,&second)==TINYRT_OK);
        memset(&second,0xcc,sizeof(second));snapshot=second;
        if(i<3)CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_VERIFY_FAILED);
        else CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);
        CHECK(!memcmp(&second,&snapshot,sizeof(second)));
        tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==baseline);
    }
}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    caller_owned_frames();rejected_outputs();output_released_before_other_callbacks();
    tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);
    printf("FRAMES checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
