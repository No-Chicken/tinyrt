#include "tinyrt_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static union {uint32_t align;uint16_t pixels[16*8];} target;
static unsigned zero_bytes,copy_bytes;
static int tracked(const void *p,size_t n){uintptr_t a=(uintptr_t)p,b=(uintptr_t)target.pixels;return a>=b&&a-b<=sizeof(target.pixels)&&n<=sizeof(target.pixels)-(a-b);}
static void *measure_zero(void *p,int c,size_t n){if(tracked(p,n))zero_bytes+=(unsigned)n;return memset(p,c,n);}
static void *measure_copy(void *p,const void *s,size_t n){if(tracked(p,n))copy_bytes+=(unsigned)n;return memcpy(p,s,n);}
/* Instrument actual memory operations only in this isolated test executable.
 * Production source has no counters, hooks or test-only branches. */
#define memset measure_zero
#define memcpy measure_copy
#include "../../runtime/gfx/tinyrt_gfx.c"
#undef memset
#undef memcpy
static tinyrt_frame_t f;
int main(void){
 tinyrt_gfx_rect_t a={2,28,0,0,16,8,0xf800,255},b={2,28,0,0,16,8,0x07e0,255};
 memcpy(f.gfx_records,&a,28);memcpy(f.gfx_records+28,&b,28);f.gfx_bytes=56;f.gfx_scale=1;
 memset(target.pixels,0x55,sizeof(target.pixels));zero_bytes=copy_bytes=0;
 CHECK(!tinyrt_gfx_render_strip(&f,target.pixels,16,16,8,0,8,NULL,NULL));
 CHECK(zero_bytes==0);CHECK(copy_bytes==2*sizeof(target.pixels));
 for(unsigned i=0;i<128;i++)CHECK(target.pixels[i]==0x07e0);
 tinyrt_gfx_resources_t *r=tinyrt_gfx_resources_create(NULL,NULL);CHECK(r);
 const uint8_t palette[]={0,0,0,248};uint8_t indices[32];memset(indices,1,32);
 CHECK(!tinyrt_gfx_palette_upload(&r,0,0,2,palette));CHECK(!tinyrt_gfx_texture_upload(&r,0,1,8,4,indices,32));
 f.gfx_resources=r;tinyrt_gfx_resources_retain(r);
 tinyrt_gfx_sprite_t sprite={5,52,0,0,16,8,0,0,0,8,4,0,0,0};
 memcpy(f.gfx_records,&sprite,52);a=(tinyrt_gfx_rect_t){2,28,0,4,8,4,0x07e0,255};b=a;b.x=8;
 memcpy(f.gfx_records+52,&a,28);memcpy(f.gfx_records+80,&b,28);f.gfx_bytes=108;
 memset(target.pixels,0x55,sizeof(target.pixels));zero_bytes=copy_bytes=0;
 CHECK(!tinyrt_gfx_render_strip(&f,target.pixels,16,16,8,0,8,NULL,NULL));
 CHECK(zero_bytes==0);CHECK(copy_bytes==256);
 uint16_t reference[128];CHECK(!tinyrt_gfx_render_reference_strip(&f,reference,16,16,8,0,8,NULL,NULL));CHECK(!memcmp(target.pixels,reference,sizeof(reference)));
 tinyrt_frame_release(&f);for(unsigned i=0;i<32;i++)indices[i]=(uint8_t)(i%3?1:0);
 CHECK(!tinyrt_gfx_texture_upload(&r,0,1,8,4,indices,32));f.gfx_resources=r;tinyrt_gfx_resources_retain(r);
 /* First-record coverage is independent of rotation/scale, but transparency
  * and leading state records must preserve implicit clearing. */
 for(unsigned trial=0;trial<96;trial++){
  f.gfx_scale=1+trial/32;f.gfx_flags=0;
  sprite=(tinyrt_gfx_sprite_t){5,52,0,0,16,8,0,0,0,8,4,trial%32,0,0};memcpy(f.gfx_records,&sprite,52);f.gfx_bytes=52;
  memset(target.pixels,0xaa,sizeof(target.pixels));zero_bytes=copy_bytes=0;
  CHECK(!tinyrt_gfx_render_strip(&f,target.pixels,16,16,8,0,8,NULL,NULL));
  CHECK(zero_bytes==((trial&1)?sizeof(target.pixels):0));
  CHECK(!tinyrt_gfx_render_reference_strip(&f,reference,16,16,8,0,8,NULL,NULL));CHECK(!memcmp(target.pixels,reference,sizeof(reference)));
  for(unsigned i=0;i<128;i++)target.pixels[i]=reference[i]=(uint16_t)(0x3000u+i);
  zero_bytes=0;CHECK(!tinyrt_gfx_render_region(&f,target.pixels+16,16,16,8,3,1,7,5,NULL,NULL));CHECK(zero_bytes==((trial&1)?70u:0u));
  CHECK(!tinyrt_gfx_render_reference_region(&f,reference+16,16,16,8,3,1,7,5,NULL,NULL));CHECK(!memcmp(target.pixels,reference,sizeof(reference)));
 }
 uint32_t leading_clip[]={0x0014000d,1,1,4,3};memcpy(f.gfx_records,leading_clip,20);memcpy(f.gfx_records+20,&sprite,52);f.gfx_bytes=72;f.gfx_scale=1;
 zero_bytes=0;CHECK(!tinyrt_gfx_render_strip(&f,target.pixels,16,16,8,0,8,NULL,NULL));CHECK(zero_bytes==sizeof(target.pixels));
 CHECK(!tinyrt_gfx_render_reference_strip(&f,reference,16,16,8,0,8,NULL,NULL));CHECK(!memcmp(target.pixels,reference,sizeof(reference)));
 /* Mixed alpha, clipped/reset CLEAR and every sprite rotation/flip/mask.
  * Fast output equals the scalar renderer even when previous rows differ. */
 for(unsigned trial=0;trial<384;trial++){
  unsigned off=0;
  f.gfx_flags=trial%2;f.gfx_scale=1+trial%3;
  a=(tinyrt_gfx_rect_t){2,28,0,0,16,8,0x1234,128};memcpy(f.gfx_records+off,&a,28);off+=28;
  sprite=(tinyrt_gfx_sprite_t){5,52,-1,0,(trial&8)?4:8,(trial&8)?8:4,0,0,0,8,4,trial%32,0,0};memcpy(f.gfx_records+off,&sprite,52);off+=52;
  uint32_t clip[]={0x0014000d,1,1,4,3};memcpy(f.gfx_records+off,clip,20);off+=20;
  a=(tinyrt_gfx_rect_t){2,28,0,0,8,4,0x07e0,(trial&1)?128:255};memcpy(f.gfx_records+off,&a,28);off+=28;
  clip[3]=0;memcpy(f.gfx_records+off,clip,20);off+=20;
  sprite.x=(int32_t)(trial%8);sprite.y=0;sprite.flags=trial%32;memcpy(f.gfx_records+off,&sprite,52);off+=52;
  if(!(trial%8)){uint32_t clear[]={0x00080001,0xabcd};memcpy(f.gfx_records+off,clear,8);off+=8;}
  f.gfx_bytes=off;
  for(unsigned i=0;i<128;i++)target.pixels[i]=reference[i]=(uint16_t)(0x3000u+i);
  CHECK(!tinyrt_gfx_render_region(&f,target.pixels+16,16,16,8,3,1,7,5,NULL,NULL));
  CHECK(!tinyrt_gfx_render_reference_region(&f,reference+16,16,16,8,3,1,7,5,NULL,NULL));
  CHECK(!memcmp(target.pixels,reference,sizeof(reference)));
 }
 /* All earlier records remain drawn, irrespective of later opaque coverage. */
 f.gfx_scale=1;f.gfx_flags=0;
 a=(tinyrt_gfx_rect_t){2,28,0,0,16,8,0x1234,255};memcpy(f.gfx_records,&a,28);
 for(unsigned i=1;i<20;i++){a=(tinyrt_gfx_rect_t){2,28,(int32_t)(i%8)*2,(int32_t)(i/8)*2,1,1,0x07e0,255};memcpy(f.gfx_records+28*i,&a,28);}
 f.gfx_bytes=560;
 CHECK(!tinyrt_gfx_render_strip(&f,target.pixels,16,16,8,0,8,NULL,NULL));CHECK(!tinyrt_gfx_render_reference_strip(&f,reference,16,16,8,0,8,NULL,NULL));CHECK(!memcmp(target.pixels,reference,sizeof(reference)));
 a=(tinyrt_gfx_rect_t){2,28,0,0,1,1,0x07e0,255};for(unsigned i=0;i<33;i++)memcpy(f.gfx_records+28*i,&a,28);f.gfx_bytes=924;
 zero_bytes=copy_bytes=0;CHECK(!tinyrt_gfx_render_strip(&f,target.pixels,16,16,8,0,8,NULL,NULL));CHECK(zero_bytes==sizeof(target.pixels));
 tinyrt_frame_release(&f);tinyrt_gfx_resources_release(r);CHECK(!tinyrt_gfx_memory_used());
 puts("constant first-record clear elision: 96 rotation/scale/mask cases, 384 mixed reference cases, normal ordered replay passed");return 0;
}
