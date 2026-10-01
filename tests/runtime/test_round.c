#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
static unsigned char bytes[8192];
static uint32_t size;
static tinyrt_package_policy_t policy={1,15,2,100000};
static int32_t saved;
static unsigned writes;
static int32_t get(void *ctx,uint32_t key,int32_t fallback) {(void)ctx;(void)key;(void)fallback;return saved;}
static tinyrt_status_t set(void *ctx,uint32_t key,int32_t value) {(void)ctx;(void)key;saved=value;++writes;return TINYRT_OK;}
static uint32_t now(void *ctx) {(void)ctx;return 123;}
static tinyrt_runtime_host_t host={NULL,get,set,now};
static tinyrt_frame_t frame,previous;
static void fixture(const char *name) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,name);
    FILE *f=fopen(path,"rb"); if (!f) exit(2);
    size=(uint32_t)fread(bytes,1,sizeof(bytes),f); CHECK(!ferror(f) && fgetc(f)==EOF); fclose(f);
}
static tinyrt_status_t read_bytes(void *ctx,uint32_t offset,void *out,uint32_t n) {
    (void)ctx; if(offset>size || n>size-offset)return TINYRT_IO_ERROR;
    memcpy(out,bytes+offset,n);return TINYRT_OK;
}
static tinyrt_runtime_t *create(const char *name) {
    fixture(name);tinyrt_runtime_t *rt=NULL;
    CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&rt)==TINYRT_OK);
    return rt;
}
static void graphics(void) {
    tinyrt_runtime_t *rt=create("round_valid");if(!rt)return;
    CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_OK);CHECK(frame.count==4);
    const tinyrt_draw_command_t *r=&frame.commands[1],*a=&frame.commands[2],*t=&frame.commands[3];
    CHECK(r->kind==TINYRT_DRAW_ROUND_RECT && r->x==10 && r->y==20 && r->w==80 && r->h==40 && r->radius==12 && r->rgb==0xabcdef);
    CHECK(a->kind==TINYRT_DRAW_ARC && a->x==233 && a->y==233 && a->radius==200 && a->thickness==12 && a->start_angle==0 && a->end_angle==360);
    CHECK(a->w==0 && a->h==0 && a->font_px==0 && a->align==0);
    CHECK(t->kind==TINYRT_DRAW_TEXT_BOX && t->x==40 && t->y==30 && t->w==300 && t->h==56 && t->font_px==48 && t->align==1 && !strcmp(t->text,"TEXT"));
    CHECK(t->radius==0 && t->thickness==0 && t->start_angle==0 && t->end_angle==0);
    tinyrt_runtime_destroy(rt);
    const char *good[]={"round_all_imports","round_cap128","text_63","round_styles"};
    for(unsigned i=0;i<4;++i){rt=create(good[i]);if(!rt)continue;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_OK);
        if(i==1)CHECK(frame.count==128);
        if(i==2)CHECK(strlen(frame.commands[1].text)==63);
        if(i==3){const int fonts[]={18,24,36,48},aligns[]={0,1,2,1};CHECK(frame.count==7);
            for(unsigned n=0;n<4;++n)CHECK(frame.commands[n+1].font_px==fonts[n] && frame.commands[n+1].align==aligns[n]);
            CHECK(frame.commands[5].radius==0);CHECK(frame.commands[6].start_angle==90 && frame.commands[6].end_angle==90);}
        tinyrt_runtime_destroy(rt);}
    const char *bad[]={"rr_xnegative","rr_overflow","rr_zero","rr_negative_radius","rr_large_radius",
        "arc_zero","arc_negative","arc_overflow","arc_xedge","arc_leftedge","arc_thinzero","arc_thick","arc_negative_angle","arc_angle361","arc_reversed",
        "text_negative","text_zero_width","text_overflow","text_ptr","text_ptr_wrap","text_empty","text_long","text_font","text_align_negative","text_align",
        "text_badutf8","text_control","text_surrogate","round_cap129","rr_noclear","arc_noclear","textbox_noclear"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){rt=create(bad[i]);if(!rt)continue;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);memset(&frame,0xa5,sizeof(frame));previous=frame;
        CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_VERIFY_FAILED);CHECK(!memcmp(&frame,&previous,sizeof(frame)));
        CHECK(tinyrt_runtime_last_error(rt)[0]);CHECK(tinyrt_runtime_stop(rt)==TINYRT_VERIFY_FAILED);tinyrt_runtime_destroy(rt);}
    const char *outside[]={"rr_init","arc_init","textbox_init","rr_event","arc_event","textbox_event"};
    for(unsigned i=0;i<6;++i){rt=create(outside[i]);if(!rt)continue;
        if(i<3)CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_VERIFY_FAILED);
        else {CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_event(rt,2,0,0,0)==TINYRT_VERIFY_FAILED);}
        tinyrt_runtime_destroy(rt);}
    fixture("round_valid");tinyrt_package_policy_t denied=policy;denied.permissions=14;rt=NULL;
    CHECK(tinyrt_runtime_create(bytes,size,&denied,&host,&rt)==TINYRT_VERIFY_FAILED && !rt);
}
static void stopping(void) {
    tinyrt_runtime_t *rt=create("stop_valid");if(!rt)return;saved=0;writes=0;
    CHECK(tinyrt_runtime_stop(rt)==TINYRT_INVALID_ARGUMENT && writes==0);
    CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);
    CHECK(tinyrt_runtime_stop(rt)==TINYRT_OK && saved==77 && writes==1);
    CHECK(tinyrt_runtime_stop(rt)==TINYRT_OK && writes==1);
    CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_runtime_event(rt,2,0,0,0)==TINYRT_INVALID_ARGUMENT);tinyrt_runtime_destroy(rt);
    rt=create("stop_legacy");if(rt){saved=0;writes=0;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);
        CHECK(tinyrt_runtime_stop(rt)==TINYRT_OK && writes==0 && saved==0);tinyrt_runtime_destroy(rt);}
    const char *bad[]={"stop_failure","stop_trap","stop_spin","stop_draw"};
    for(unsigned i=0;i<4;++i){rt=create(bad[i]);if(!rt)continue;writes=0;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_stop(rt)==TINYRT_VERIFY_FAILED);
        unsigned once=writes;CHECK(once==(i==3?0u:1u));
        CHECK(tinyrt_runtime_last_error(rt)[0]);CHECK(tinyrt_runtime_stop(rt)==TINYRT_VERIFY_FAILED && writes==once);tinyrt_runtime_destroy(rt);}
    rt=create("stop_init_failure");if(rt){writes=0;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_VERIFY_FAILED);
        CHECK(tinyrt_runtime_stop(rt)==TINYRT_VERIFY_FAILED && writes==0);tinyrt_runtime_destroy(rt);}
    rt=create("stop_render_failure");if(rt){writes=0;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_VERIFY_FAILED);
        CHECK(tinyrt_runtime_stop(rt)==TINYRT_VERIFY_FAILED && writes==0);tinyrt_runtime_destroy(rt);}
    rt=create("stop_valid");if(rt){writes=0;
        CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_event(rt,2,0,0,0)==TINYRT_VERIFY_FAILED);
        CHECK(tinyrt_runtime_stop(rt)==TINYRT_VERIFY_FAILED && writes==0);tinyrt_runtime_destroy(rt);}
}
int main(void) {
    tinyrt_store_io_t io={0}; io.read=read_bytes;
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    fixture("round_valid"); CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_OK);
    fixture("stop_bad_signature"); CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_VERIFY_FAILED);
    fixture("stop_bad_kind"); CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_VERIFY_FAILED);
    fixture("stop_valid");writes=0;CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_OK && writes==0);
    graphics();stopping();
    tinyrt_runtime_system_shutdown(); CHECK(tinyrt_runtime_memory_used()==0);
    printf("ROUND_STOP checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
