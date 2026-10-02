#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);} } while(0)
static uint8_t bytes[131072];
static uint32_t size;
static tinyrt_frame_t frame,previous;
static tinyrt_package_policy_t policy={1,15,2,100000};
static tinyrt_runtime_host_t host;
static tinyrt_runtime_t *create(const char *name);
static void close_guest(tinyrt_runtime_t*r,size_t baseline);
static tinyrt_status_t resource_read(void *ctx,uint32_t off,void *out,uint32_t n) {
 (void)ctx;static const uint8_t resource[]={0,0xf8,0xe0,7,0x1f,0,0xff,0xff};
 if(off>sizeof(resource)||n>sizeof(resource)-off)return TINYRT_INVALID_ARGUMENT;
 memcpy(out,resource+off,n);return TINYRT_OK;
}
static void scaling_resources(void) {
 size_t baseline=tinyrt_runtime_memory_used();tinyrt_runtime_t*r=create("scaled_valid");
 if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
  CHECK(frame.pixel_bytes==8&&frame.commands[1].w==340&&frame.commands[1].h==319);close_guest(r,baseline);}
 const char *scales[]={"scaled_right","scaled_bottom","scaled_source","scaled_length","scaled_pointer"};
 for(unsigned i=0;i<5;++i){r=create(scales[i]);if(!r)continue;CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
  CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED);close_guest(r,baseline);}
 host.asset_read=resource_read;r=create("asset_valid");
 if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK);
  CHECK(frame.pixel_bytes==8&&frame.pixels[1]==0xf8&&frame.pixels[2]==0xe0);close_guest(r,baseline);}
 const char *resources[]={"asset_range","asset_wrap","asset_pointer","asset_limit"};
 for(unsigned i=0;i<4;++i){r=create(resources[i]);if(!r)continue;CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED);close_guest(r,baseline);}
 host.asset_read=NULL;
}
static void fixture(const char *name) {
 char path[1024];snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,name);
 FILE*f=fopen(path,"rb");if(!f)exit(2);
 size=(uint32_t)fread(bytes,1,sizeof(bytes),f);CHECK(!ferror(f)&&fgetc(f)==EOF);fclose(f);
}
static tinyrt_runtime_t *create(const char *name) {
 fixture(name);tinyrt_runtime_t*r=NULL;
 CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&r)==TINYRT_OK);return r;
}
static void close_guest(tinyrt_runtime_t*r,size_t baseline) {
 size_t peak=tinyrt_runtime_memory_peak();CHECK(peak>=tinyrt_runtime_memory_used()&&peak<=TINYRT_RUNTIME_HEAP_LIMIT);
 tinyrt_runtime_destroy(r);CHECK(tinyrt_runtime_memory_used()==baseline&&tinyrt_runtime_memory_peak()==peak);
}
static void pixels(void) {
 size_t baseline=tinyrt_runtime_memory_used();tinyrt_runtime_t*r=create("pixel_valid");
 if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
  CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.count==2&&frame.pixel_bytes==8);
  const uint8_t expected[]={0,0xf8,0xe0,7,0x1f,0,0xff,0xff};
  CHECK(!memcmp(frame.pixels,expected,8));
  CHECK(frame.commands[1].kind==TINYRT_DRAW_RGB565&&frame.commands[1].x==10&&frame.commands[1].y==20&&frame.commands[1].w==2&&frame.commands[1].h==2);
  previous=frame;CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.pixels[0]==0x99);
  CHECK(!memcmp(previous.pixels,expected,8));close_guest(r,baseline);}
 const char*good[]={"pixel_maximum","pixel_edge_valid"};
 for(unsigned i=0;i<2;++i){r=create(good[i]);if(!r)continue;
  CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
  CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.count==2);
  CHECK(frame.pixel_bytes==(i==0?122880u:8u));
  if(i==0)for(unsigned k=0;k<122880;++k)CHECK(frame.pixels[k]==(uint8_t)k);
  close_guest(r,baseline);}
 const char*bad[]={"negative_x","negative_y","overflow_x","overflow_y","edge_x","edge_y",
  "width_zero","height_zero","width_negative","height_negative","width_overflow","height_overflow",
  "width257","height241","length_zero","length_short","length_long","length_wrap","pointer_wrap",
  "pointer_end","twice","noclear","cap"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){char name[80];snprintf(name,sizeof(name),"pixel_%s",bad[i]);
  r=create(name);if(!r)continue;CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
  memset(&frame,0xa5,sizeof(frame));previous=frame;
  CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED);
  CHECK(frame.count==0&&tinyrt_runtime_last_error(r)[0]);
  CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);close_guest(r,baseline);}
}
static void skipping(void) {
 size_t baseline=tinyrt_runtime_memory_used();
 const char*good[]={"skip_valid","pixel_then_skip"};
 for(unsigned i=0;i<2;++i){tinyrt_runtime_t*r=create(good[i]);if(!r)continue;
  CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);memset(&frame,0xa5,sizeof(frame));
  if(i){CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.count==2);
   CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);}
  previous=frame;previous.count=0;
  CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.count==0);
  CHECK(!memcmp(&frame,&previous,sizeof(frame)));
  if(i){CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_OK);
   CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_OK&&frame.count==2&&frame.pixel_bytes==8);}
  close_guest(r,baseline);}
 const char*bad[]={"skip_clear","clear_skip","skip_pixel","skip_twice"};
 for(unsigned i=0;i<4;++i){tinyrt_runtime_t*r=create(bad[i]);if(!r)continue;
  CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);memset(&frame,0xa5,sizeof(frame));previous=frame;
  CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED&&frame.count==0);
  close_guest(r,baseline);}
}
static void stages_and_permissions(void) {
 size_t baseline=tinyrt_runtime_memory_used();
 const char*names[]={"pixel_init","skip_init","pixel_event","skip_event","pixel_stop","skip_stop","clock_render","clock_stop"};
 for(unsigned i=0;i<8;++i){tinyrt_runtime_t*r=create(names[i]);if(!r)continue;
  if(i<2)CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED);
  else {CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
   if(i<4)CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_VERIFY_FAILED);
   else if(i==6)CHECK(tinyrt_runtime_render(r,&frame)==TINYRT_VERIFY_FAILED);
   else CHECK(tinyrt_runtime_stop(r)==TINYRT_VERIFY_FAILED);}
  close_guest(r,baseline);}
 const char*imports[]={"draw_rgb565","draw_skip","clock_interval"};
 for(unsigned i=0;i<3;++i){char name[80];tinyrt_runtime_t*r=NULL;
  snprintf(name,sizeof(name),"permission_%s",imports[i]);fixture(name);
  tinyrt_package_policy_t denied=policy;denied.permissions=i==2?7:14;
  CHECK(tinyrt_runtime_create(bytes,size,&denied,&host,&r)==TINYRT_VERIFY_FAILED&&!r);
  snprintf(name,sizeof(name),"signature_%s",imports[i]);fixture(name);
  CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&r)==TINYRT_VERIFY_FAILED&&!r);
  CHECK(tinyrt_runtime_memory_used()==baseline);}
}
static void clocks(void) {
 size_t baseline=tinyrt_runtime_memory_used();CHECK(tinyrt_runtime_clock_interval_ms(NULL)==100);
 tinyrt_runtime_t*r=create("clock_valid");if(r){CHECK(tinyrt_runtime_clock_interval_ms(r)==100);
  CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK&&tinyrt_runtime_clock_interval_ms(r)==1);
  CHECK(tinyrt_runtime_event(r,2,0,0,1000)==TINYRT_OK&&tinyrt_runtime_clock_interval_ms(r)==1000);
  CHECK(tinyrt_runtime_event(r,2,0,0,17)==TINYRT_OK&&tinyrt_runtime_clock_interval_ms(r)==17);
  CHECK(tinyrt_runtime_stop(r)==TINYRT_OK&&tinyrt_runtime_clock_interval_ms(r)==100);close_guest(r,baseline);}
 const char*bad[]={"clock_bad_0","clock_bad_-1","clock_bad_1001","clock_bad_2147483647"};
 for(unsigned i=0;i<4;++i){r=create(bad[i]);if(!r)continue;
  CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_VERIFY_FAILED&&tinyrt_runtime_clock_interval_ms(r)==100);close_guest(r,baseline);}
 r=create("clock_valid");if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK);
  CHECK(tinyrt_runtime_event(r,2,0,0,0)==TINYRT_VERIFY_FAILED&&tinyrt_runtime_clock_interval_ms(r)==100);close_guest(r,baseline);}
 r=create("pixel_valid");if(r){CHECK(tinyrt_runtime_init(r,466,466)==TINYRT_OK&&tinyrt_runtime_clock_interval_ms(r)==100);close_guest(r,baseline);}
}
int main(void) {
 CHECK(tinyrt_runtime_system_init()==TINYRT_OK);pixels();skipping();stages_and_permissions();clocks();scaling_resources();
 size_t peak=tinyrt_runtime_memory_peak();CHECK(peak>122880);
 tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0&&tinyrt_runtime_memory_peak()==peak);
 CHECK(tinyrt_runtime_system_init()==TINYRT_OK&&tinyrt_runtime_memory_peak()<peak);
 tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);
 printf("PIXELS_CLOCK checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
