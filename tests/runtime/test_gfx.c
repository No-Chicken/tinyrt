#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static tinyrt_frame_t frame;
static unsigned text_calls;
static void text_capture(void *ctx,uint16_t *pixels,uint32_t stride,uint32_t width,uint32_t height,uint32_t y,uint32_t rows,const tinyrt_gfx_text_t *text,const char *utf8,const tinyrt_gfx_damage_t *clip,uint32_t scale) {
 (void)ctx;(void)pixels;(void)stride;(void)width;(void)height;(void)y;(void)rows;
 if(text->color==0xffff && text->length==1 && utf8[0]=='A' && clip->w==4 && scale==1)text_calls++;
}
#ifdef _WIN32
#include <windows.h>
static int clear_guard_page(void) {
 size_t rounded=(sizeof(tinyrt_frame_t)+4095u)&~(size_t)4095u;
 uint8_t *memory=VirtualAlloc(NULL,rounded+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(memory);
 DWORD prior;CHECK(VirtualProtect(memory+rounded,4096,PAGE_NOACCESS,&prior));
 tinyrt_frame_t *f=(tinyrt_frame_t *)(memory+rounded-sizeof(*f));memset(f,0,sizeof(*f));
 tinyrt_gfx_rect_t r={2,28,0,0,1,1,0xffff,255};
 for(unsigned i=0;i<2340;i++)memcpy(f->gfx_records+28*i,&r,28);
 const uint8_t clear[]={1,0,8,0,255,255,0,0};memcpy(f->gfx_records+65520,clear,8);memcpy(f->gfx_records+65528,clear,8);
 f->gfx_bytes=65536;f->gfx_scale=1;uint16_t row[466];
 CHECK(!tinyrt_gfx_render_strip(f,row,466,466,466,0,1,NULL,NULL));CHECK(row[0]==0xffff && row[465]==0xffff);
 CHECK(VirtualFree(memory,0,MEM_RELEASE));return 0;
}
#endif
int main(void) {
#ifdef _WIN32
 CHECK(clear_guard_page()==0);
#endif
 uint8_t good[]={1,0,8,0,0,0,0,0}; uint32_t n=0,e=999;
 CHECK(tinyrt_gfx_validate(good,sizeof(good),NULL,&n,&e)==0 && n==1);
 uint8_t bad[]={1,0,8,0,0,0,0,0,2,0,8,0,0,0,0,0};
 CHECK(tinyrt_gfx_validate(bad,sizeof(bad),NULL,&n,&e)==-1 && e==1);
 tinyrt_gfx_resources_t *r=tinyrt_gfx_resources_create(NULL,NULL);CHECK(r);
 const uint8_t palette[]={0,0,0,248,224,7,31,0};
 CHECK(tinyrt_gfx_palette_upload(&r,0,0,4,palette)==0);
 tinyrt_gfx_grid_t grid={9,44,0,0,2,2,2,2,1,0,0};
 memcpy(frame.gfx_records,&grid,40);frame.gfx_records[40]=1;frame.gfx_records[41]=2;frame.gfx_records[42]=3;frame.gfx_records[43]=0;
 frame.gfx_bytes=44;frame.gfx_scale=1;frame.gfx_resources=r;tinyrt_gfx_resources_retain(r);
 CHECK(tinyrt_gfx_validate(frame.gfx_records,44,r,&n,&e)==0);
 uint16_t out[16];memset(out,0xaa,sizeof(out));
 CHECK(tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL)==0);
 const uint16_t expected[16]={0xf800,0,0x07e0,0,0,0,0,0,0x001f,0,0,0,0,0,0,0};
 CHECK(!memcmp(out,expected,sizeof(out)));
 /* Palette updates after publication must not change queued frame pixels. */
 const uint8_t blue[]={31,0};CHECK(tinyrt_gfx_palette_upload(&r,0,1,1,blue)==0);
 CHECK(tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL)==0 && out[0]==0xf800);
 tinyrt_frame_release(&frame);tinyrt_gfx_resources_release(r);

 /* Non-square source and rotation independently expected, no shared mapper. */
 r=tinyrt_gfx_resources_create(NULL,NULL);CHECK(r);
 uint8_t colors[]={0,0,1,0,2,0,3,0,4,0,5,0,6,0};
 CHECK(!tinyrt_gfx_palette_upload(&r,0,0,7,colors));
 const uint8_t indices[]={1,2,3,4,5,6};
 CHECK(!tinyrt_gfx_texture_upload(&r,0,1,2,3,indices,6));
 tinyrt_gfx_sprite_t spr={5,52,0,0,3,2,0,0,0,2,3,8,0,0};
 memcpy(frame.gfx_records,&spr,sizeof(spr));frame.gfx_bytes=52;frame.gfx_scale=1;frame.gfx_resources=r;tinyrt_gfx_resources_retain(r);
 CHECK(!tinyrt_gfx_render_strip(&frame,out,3,3,2,0,2,NULL,NULL));
 const uint16_t rotated[]={5,3,1,6,4,2};CHECK(!memcmp(out,rotated,sizeof(rotated)));
 spr.flags=10;memcpy(frame.gfx_records,&spr,sizeof(spr));
 CHECK(!tinyrt_gfx_render_strip(&frame,out,3,3,2,0,2,NULL,NULL));
 const uint16_t flipped[]={6,4,2,5,3,1};CHECK(!memcmp(out,flipped,sizeof(flipped)));
 uint16_t separate[6];CHECK(!tinyrt_gfx_render_strip(&frame,separate,3,3,2,0,1,NULL,NULL));
 CHECK(!tinyrt_gfx_render_strip(&frame,separate+3,3,3,2,1,1,NULL,NULL));CHECK(!memcmp(separate,flipped,sizeof(flipped)));
 /* Replacing and freeing the session texture do not invalidate old frames. */
 const uint8_t changed[]={6,5,4,3,2,1};CHECK(!tinyrt_gfx_texture_upload(&r,0,1,2,3,changed,6));
 CHECK(!tinyrt_gfx_texture_free(&r,0));tinyrt_gfx_resources_release(r);
 CHECK(!tinyrt_gfx_render_strip(&frame,out,3,3,2,0,2,NULL,NULL));CHECK(!memcmp(out,flipped,sizeof(flipped)));
 tinyrt_frame_release(&frame);CHECK(tinyrt_gfx_memory_used()==0);
 /* RGB565 blending rounds each native channel, exact hand-calculated mid-grey. */
 tinyrt_gfx_rect_t rr={2,28,0,0,1,1,0xffff,128};memcpy(frame.gfx_records,&rr,sizeof(rr));frame.gfx_bytes=28;frame.gfx_scale=1;frame.gfx_flags=0;frame.count=0;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,1,1,1,0,1,NULL,NULL));CHECK(out[0]==0x8410);
 /* Count/corner clipping, zero fields and truncations reject safely. */
 for(uint32_t len=1;len<28;len++)CHECK(tinyrt_gfx_validate(frame.gfx_records,len,NULL,&n,&e)==-1);
 rr.w=0;memcpy(frame.gfx_records,&rr,sizeof(rr));CHECK(tinyrt_gfx_validate(frame.gfx_records,28,NULL,&n,&e)==-1);
 rr.w=1;rr.x=-1;rr.h=1;memcpy(frame.gfx_records,&rr,sizeof(rr));CHECK(!tinyrt_gfx_validate(frame.gfx_records,28,NULL,&n,&e));
 CHECK(!tinyrt_gfx_render_strip(&frame,out,1,1,1,0,1,NULL,NULL) && out[0]==0);
 /* Independent hard pool bound includes retained resource generations. */
 tinyrt_gfx_resources_t *held[16]={0};uint8_t *large=calloc(1,262144);CHECK(large);
 r=tinyrt_gfx_resources_create(NULL,NULL);CHECK(r);unsigned kept=0;int failed=0;
 for(unsigned i=0;i<16;i++){if(tinyrt_gfx_texture_upload(&r,0,1,512,512,large,262144)){failed=1;break;}held[kept++]=r;tinyrt_gfx_resources_retain(r);}
 CHECK(failed && kept>=3 && tinyrt_gfx_memory_used()<=1152u*1024u);
 tinyrt_gfx_resources_release(r);for(unsigned i=0;i<kept;i++)tinyrt_gfx_resources_release(held[i]);free(large);CHECK(tinyrt_gfx_memory_used()==0);
 memset(&frame,0,sizeof(frame));for(unsigned i=0;i<12;i++){tinyrt_gfx_damage_t d={(int32_t)i,0,1,1};tinyrt_gfx_damage_merge(&frame,&d);}
 CHECK(frame.damage_count<=8);int right=0;for(unsigned i=0;i<frame.damage_count;i++)if(frame.damage[i].x+frame.damage[i].w>right)right=frame.damage[i].x+frame.damage[i].w;CHECK(right==12 && frame.damage[0].x==0);

 /* Valid but expensive overdraw is rejected before touching the target. */
 memset(&frame,0,sizeof(frame));rr=(tinyrt_gfx_rect_t){2,28,0,0,4,4,0xffff,128};
 for(unsigned i=0;i<2340;i++)memcpy(frame.gfx_records+28*i,&rr,28);
 frame.gfx_bytes=65520;frame.gfx_scale=1;memset(out,0xab,sizeof(out));
 CHECK(tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL)==-1 && out[0]==0xabab);

 /* Circle/arc pixel centres must have symmetric cardinal edges. */
 memset(&frame,0,sizeof(frame));uint32_t arc[]={0x00200004,2,2,2,2,0,360,0xffff};memcpy(frame.gfx_records,arc,32);frame.gfx_bytes=32;frame.gfx_scale=1;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));
 const uint16_t circle[]={0,65535,65535,0,65535,65535,65535,65535,65535,65535,65535,65535,0,65535,65535,0};CHECK(!memcmp(out,circle,sizeof(circle)));

 /* Fast per-cell GRID equals the scalar pixel reference over clips/scales/masks. */
 r=tinyrt_gfx_resources_create(NULL,NULL);CHECK(r);CHECK(!tinyrt_gfx_palette_upload(&r,0,0,4,palette));
 memset(&frame,0,sizeof(frame));frame.gfx_resources=r;tinyrt_gfx_resources_retain(r);
 uint16_t fast[23*19],slow[23*19],strips[23*19];
 for(unsigned trial=0;trial<96;trial++){
  tinyrt_gfx_grid_t g={9,56,(int32_t)(trial%9)-4,(int32_t)(trial%7)-3,5,3,4,4,trial%4,0,trial%2};
  memcpy(frame.gfx_records,&g,40);for(unsigned j=0;j<16;j++)frame.gfx_records[40+j]=(uint8_t)((j+trial)%4);
  frame.gfx_bytes=56;frame.gfx_scale=1+trial%3;frame.gfx_flags=0;
  CHECK(!tinyrt_gfx_render_strip(&frame,fast,23,23,19,0,19,NULL,NULL));
  CHECK(!tinyrt_gfx_render_reference_strip(&frame,slow,23,23,19,0,19,NULL,NULL));if(memcmp(fast,slow,sizeof(fast)))fprintf(stderr,"property trial %u\n",trial);CHECK(!memcmp(fast,slow,sizeof(fast)));
  for(unsigned row=0;row<19;row++)CHECK(!tinyrt_gfx_render_strip(&frame,strips+row*23,23,23,19,row,1,NULL,NULL));CHECK(!memcmp(strips,slow,sizeof(strips)));
 }

 const uint8_t six[]={0,1,2,3,0,1};CHECK(!tinyrt_gfx_texture_upload(&r,0,1,2,3,six,6));
 tinyrt_frame_release(&frame);frame.gfx_resources=r;tinyrt_gfx_resources_retain(r);
 for(unsigned trial=0;trial<192;trial++){
  tinyrt_gfx_sprite_t t={(uint16_t)(5+trial%3),52,(int32_t)(trial%7)-3,(int32_t)(trial%5)-2,1+(int32_t)(trial%11),1+(int32_t)(trial%9),0,0,0,2,3,trial%32,0,0xf81f};
  memcpy(frame.gfx_records,&t,52);frame.gfx_bytes=52;frame.gfx_scale=1+trial%3;frame.gfx_flags=trial%2;
  memset(fast,0x5a,sizeof(fast));memset(slow,0x5a,sizeof(slow));memset(strips,0x5a,sizeof(strips));
  CHECK(!tinyrt_gfx_render_strip(&frame,fast,23,23,19,0,19,NULL,NULL));
  CHECK(!tinyrt_gfx_render_reference_strip(&frame,slow,23,23,19,0,19,NULL,NULL));if(memcmp(fast,slow,sizeof(fast)))fprintf(stderr,"property trial %u\n",trial);CHECK(!memcmp(fast,slow,sizeof(fast)));
  for(unsigned row=0;row<19;row++)CHECK(!tinyrt_gfx_render_strip(&frame,strips+row*23,23,23,19,row,1,NULL,NULL));CHECK(!memcmp(strips,slow,sizeof(strips)));
 }
 tinyrt_frame_release(&frame);tinyrt_gfx_resources_release(r);CHECK(tinyrt_gfx_memory_used()==0);

 r=tinyrt_gfx_resources_create(NULL,NULL);CHECK(r);CHECK(!tinyrt_gfx_palette_upload(&r,0,0,4,palette));
 CHECK(!tinyrt_gfx_texture_upload(&r,0,1,2,3,six,6));memset(&frame,0,sizeof(frame));frame.gfx_resources=r;tinyrt_gfx_resources_retain(r);frame.gfx_scale=1;
 uint32_t batch[]={0x003c0008,0,1,1,1,0,2,0,0,1,0,2,0,0,1};memcpy(frame.gfx_records,batch,60);frame.gfx_bytes=60;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(out[0]==0xf800 && out[1]==0 && out[2]==0x7e0);
 uint32_t tile[]={0x0034000a,0,0,2,2,1,1,0,0,0,1,0,0x04030201};memcpy(frame.gfx_records,tile,52);frame.gfx_bytes=52;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(out[0]==0xf800 && out[1]==0x7e0 && out[4]==31 && out[5]==0);
 uint32_t round[]={0x001c0003,0,0,4,4,2,0xffff};memcpy(frame.gfx_records,round,28);frame.gfx_bytes=28;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(!memcmp(out,circle,sizeof(circle)));
 tinyrt_gfx_text_t txt={11,40,0,0,4,4,18,0,0xffff,1};memcpy(frame.gfx_records,&txt,36);memset(frame.gfx_records+36,0,4);frame.gfx_records[36]='A';frame.gfx_bytes=40;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,text_capture,NULL));CHECK(text_calls==1);
 uint32_t clipclear[]={0x0014000d,1,1,2,2,0x00080001,0xffff};memcpy(frame.gfx_records,clipclear,28);frame.gfx_bytes=28;
 CHECK(!tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL));CHECK(out[0]==0 && out[5]==65535 && out[10]==65535 && out[15]==0);
 /* Mutate every byte of a valid batch; malformed sizes cannot overread. */
 uint8_t seed[60];memcpy(seed,batch,60);for(unsigned at=0;at<60;at++)for(unsigned val=0;val<256;val+=17){memcpy(frame.gfx_records,seed,60);frame.gfx_records[at]=(uint8_t)val;frame.gfx_bytes=60;
 int valid=tinyrt_gfx_validate(frame.gfx_records,60,r,&n,&e);if(!valid){int result=tinyrt_gfx_render_strip(&frame,out,4,4,4,0,4,NULL,NULL);CHECK(result==0 || result==-1);}else CHECK(valid==-1 || valid==-2);}
 tinyrt_frame_release(&frame);tinyrt_gfx_resources_release(r);CHECK(tinyrt_gfx_memory_used()==0);
 puts("gfx pixels, validation, rotation, blending, damage, work bound and snapshots passed");return 0;
}
