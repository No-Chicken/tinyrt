#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static tinyrt_frame_t frame;
static uint8_t bytes[65536];
static tinyrt_runtime_t *load(const char *name) {
 char path[1024];snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,name);FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint32_t n=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);tinyrt_runtime_t *r=NULL;
 tinyrt_package_policy_t p={1,9,2,100000};tinyrt_runtime_host_t host={0};
 if(tinyrt_runtime_create(bytes,n,&p,&host,&r)!=TINYRT_OK)return NULL;return r;
}
int main(void) {
 CHECK(tinyrt_runtime_system_init()==TINYRT_OK);tinyrt_runtime_t *r=load("gfx_rect");CHECK(r);
 CHECK(tinyrt_runtime_init(r,4,4)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
 CHECK(frame.count==1 && frame.commands[0].kind==9 && frame.gfx_bytes==28);
 uint16_t out[16];CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(out[0]==0xf800 && out[2]==0);
 tinyrt_frame_release(&frame);tinyrt_runtime_destroy(r);
 const char *bad[]={"gfx_invalid","gfx_noclose","gfx_badflags"};
 for(unsigned i=0;i<3;i++){r=load(bad[i]);CHECK(r);CHECK(tinyrt_runtime_init(r,4,4)==TINYRT_OK);
 CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_INVALID_ARGUMENT && frame.count==0);
 CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);tinyrt_runtime_destroy(r);}
 r=load("gfx_grid");CHECK(r);CHECK(tinyrt_runtime_init(r,4,4)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(out[0]==0xf800 && out[1]==0x7e0 && out[4]==31 && out[5]==0);tinyrt_frame_release(&frame);tinyrt_runtime_destroy(r);
 r=load("gfx_resident");CHECK(r);CHECK(tinyrt_runtime_init(r,4,4)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
 CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);tinyrt_runtime_destroy(r);
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(out[0]==0xf800 && out[1]==0x7e0 && out[4]==31);tinyrt_frame_release(&frame);
 r=load("gfx_fb");CHECK(r);CHECK(tinyrt_runtime_init(r,4,4)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
 CHECK(frame.gfx_flags==2 && frame.damage_count==0);CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));
 const uint16_t want[]={0xf800,0xf800,0x7e0,0x7e0,0xf800,0xf800,0x7e0,0x7e0,31,31,65535,65535,31,31,65535,65535};CHECK(!memcmp(out,want,sizeof(want)));
 CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);CHECK(frame.gfx_flags==3 && frame.damage_count==1 && frame.damage[0].w==4);
 tinyrt_runtime_destroy(r);CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(!memcmp(out,want,sizeof(want)));tinyrt_frame_release(&frame);
 CHECK(tinyrt_gfx_memory_used()==0);tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);
 puts("gfx runtime atomicity, imports, framebuffer and lifetime passed");return 0;
}

