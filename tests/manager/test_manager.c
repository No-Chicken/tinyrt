#include "tinyrt_manager.h"
#include "fake_nor.h"
#include "test_package_verifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks,clears,saves;
static int fail_clear,fail_save;
static uint32_t kv_mask;
static uint32_t clock_ms=42;
static int32_t kv_values[16];
static fake_nor_t nor;
static uint8_t package[400];
static tinyrt_frame_t frame;
extern unsigned runtime_destroy_count;
extern int runtime_fail_event;
extern int runtime_stop_write;
extern int runtime_skip_write;
extern int runtime_skip_frame;
extern int metadata_swap;
extern int metadata_aot;
extern int metadata_cover;
extern unsigned runtime_aot_creates;
static tinyrt_status_t load(void*c,const char*id,uint32_t*m,int32_t v[16]) {(void)c;(void)id;*m=kv_mask;memcpy(v,kv_values,sizeof(kv_values));return TINYRT_OK;}
static tinyrt_status_t save(void*c,const char*id,uint32_t m,const int32_t v[16]) {(void)c;(void)id;if(fail_save)return TINYRT_IO_ERROR;kv_mask=m;memcpy(kv_values,v,sizeof(kv_values));saves++;return TINYRT_OK;}
static tinyrt_status_t clear(void*c,const char*id) {(void)c;(void)id;if(fail_clear)return TINYRT_IO_ERROR;kv_mask=0;memset(kv_values,0,sizeof(kv_values));clears++;return TINYRT_OK;}
static uint32_t now(void*c){(void)c;return clock_ms;}
static tinyrt_manager_t *open_manager(void) {
 tinyrt_store_io_t io=fake_nor_io(&nor);static tinyrt_package_verifier_t verifier={0};
 static tinyrt_package_aot_profile_t profile={true,false,5,"x86_64","",{1}};verifier.aot_profile=&profile;
 tinyrt_manager_storage_t storage={NULL,load,save,clear,now};tinyrt_manager_t*m=NULL;
 CHECK(tinyrt_manager_open(&io,&verifier,&storage,&m)==TINYRT_OK&&m);return m;
}
static tinyrt_status_t guard_arm(void *ctx,void (*cancel)(void *),void *arg,uint32_t ms) {(void)ctx;(void)cancel;(void)arg;(void)ms;return TINYRT_OK;}
static void guard_disarm(void *ctx) {(void)ctx;}
static tinyrt_app_info_t install(tinyrt_manager_t*m,uint32_t version) {
 tinyrt_app_info_t a;test_package_make(package,sizeof(package),"counter",version,13,&a);
 CHECK(tinyrt_manager_begin(m,&a)==TINYRT_OK);
 CHECK(tinyrt_manager_write(m,package,sizeof(package))==TINYRT_OK);
 CHECK(tinyrt_manager_finish(m)==TINYRT_OK);return a;
}
static void test_cover_ranges_and_identity(void) {
 fake_nor_init(&nor);tinyrt_manager_t*m=open_manager();tinyrt_app_info_t a=install(m,1);
 tinyrt_package_cover_t cover;uint8_t bytes[16];
 CHECK(tinyrt_manager_cover_query(m,&a.id,&cover)==TINYRT_NOT_FOUND && cover.size==0);
 tinyrt_manager_close(m);metadata_cover=1;m=open_manager();
 CHECK(tinyrt_manager_cover_query(m,&a.id,&cover)==TINYRT_OK && cover.offset==44 && cover.size==356 && cover.codec==1);
 CHECK(tinyrt_manager_cover_read(m,&a.id,0,bytes,sizeof(bytes))==TINYRT_OK && bytes[0]==13);
 CHECK(tinyrt_manager_cover_read(m,&a.id,355,bytes,1)==TINYRT_OK);
 CHECK(tinyrt_manager_cover_read(m,&a.id,356,bytes,1)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_manager_cover_read(m,&a.id,UINT32_MAX,bytes,1)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_manager_cover_read(m,&a.id,0,bytes,4097)==TINYRT_INVALID_ARGUMENT);
 tinyrt_package_id_t wrong=a.id;wrong.sha256[0]^=1;
 CHECK(tinyrt_manager_cover_query(m,&wrong,&cover)==TINYRT_NOT_FOUND && cover.size==0);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 CHECK(tinyrt_manager_cover_read(m,&a.id,0,bytes,1)==TINYRT_OK);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
 tinyrt_app_info_t next;test_package_make(package,sizeof(package),"counter",2,13,&next);
 CHECK(tinyrt_manager_begin(m,&next)==TINYRT_OK);
 CHECK(tinyrt_manager_cover_query(m,&a.id,&cover)==TINYRT_BUSY);
 tinyrt_manager_abort(m);tinyrt_manager_close(m);metadata_cover=0;
}
static void test_aot_selection(void) {
 fake_nor_init(&nor);metadata_aot=1;tinyrt_manager_t*m=open_manager();
 tinyrt_execution_guard_t guard={NULL,guard_arm,guard_disarm,75};
 CHECK(tinyrt_manager_set_execution_guard(m,&guard)==TINYRT_OK);
 tinyrt_app_info_t a=install(m,1);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK && runtime_aot_creates==1);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
 CHECK(tinyrt_manager_set_execution_guard(m,NULL)==TINYRT_OK);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_INVALID_ARGUMENT && frame.count==0);
 CHECK(runtime_aot_creates==1);
 tinyrt_manager_close(m);metadata_aot=0;m=open_manager();
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK && runtime_aot_creates==1);
 tinyrt_manager_close(m);saves=clears=runtime_destroy_count=0;kv_mask=0;memset(kv_values,0,sizeof(kv_values));
}
static void test_coalescing(void) {
 fake_nor_init(&nor);kv_mask=0;memset(kv_values,0,sizeof(kv_values));saves=0;clock_ms=UINT32_MAX-2000u;
 tinyrt_manager_t*m=open_manager();tinyrt_app_info_t a=install(m,1);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK);
 CHECK(saves==1&&kv_values[0]==1);
 for(unsigned i=0;i<49;++i) {
  clock_ms+=100;
  CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK);
 }
 CHECK(saves==1&&kv_values[0]==1&&frame.commands[0].rgb==50);
 clock_ms+=100;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK);
 CHECK(saves==2&&kv_values[0]==51);
 clock_ms+=1;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==2);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&saves==3&&kv_values[0]==52);
 /* Restart does not reset the periodic write allowance. Normal stop itself
  * remains an explicit host-controlled durability exception. */
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==3);
 clock_ms+=4999;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==3);
 clock_ms+=1;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==4&&kv_values[0]==55);
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==4);
 runtime_fail_event=1;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_VERIFY_FAILED);
 runtime_fail_event=0;
 CHECK(kv_values[0]==55&&saves==4);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK&&frame.commands[0].rgb==55);
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK);
 fail_save=1;clock_ms+=5000;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_IO_ERROR);
 CHECK(kv_values[0]==55&&saves==4);fail_save=0;
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==4);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&kv_values[0]==56&&saves==5);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&saves==5);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_OK&&saves==5);
 runtime_skip_write=1;clock_ms+=5000;
 CHECK(tinyrt_manager_event(m,2,0,0,0,&frame)==TINYRT_OK&&kv_values[0]==57&&saves==6);
 clock_ms+=5000;
 CHECK(tinyrt_manager_event(m,2,0,0,0,&frame)==TINYRT_OK&&saves==6);
 runtime_skip_write=0;
 tinyrt_manager_close(m);
 saves=0;clears=0;runtime_destroy_count=0;clock_ms=42;
}
static void test_large_catalog(void) {
 fake_nor_init(&nor);tinyrt_manager_t*m=open_manager();
 static tinyrt_app_info_t apps[TINYRT_STORE_MAX_APPS];
 static tinyrt_package_metadata_t listed[TINYRT_STORE_MAX_APPS];
 uint32_t count=0;uint64_t before=99,after=99;char id[32];
 CHECK(tinyrt_manager_generation(NULL,&before)==TINYRT_INVALID_ARGUMENT&&before==0);
 CHECK(tinyrt_manager_generation(m,&before)==TINYRT_OK&&before==0);
 tinyrt_store_stats_t stats,zero={0};
 memset(&stats,0xa5,sizeof(stats));
 CHECK(tinyrt_manager_stats(NULL,&stats)==TINYRT_INVALID_ARGUMENT&&!memcmp(&stats,&zero,sizeof(stats)));
 CHECK(tinyrt_manager_stats(m,&stats)==TINYRT_OK&&stats.installed_count==0);
 for(unsigned i=0;i<16;++i){
  snprintf(id,sizeof(id),"catalog.%02u",15-i);
  test_package_make(package,sizeof(package),id,1,13,&apps[i]);
  CHECK(tinyrt_manager_begin(m,&apps[i])==TINYRT_OK);
  CHECK(tinyrt_manager_stats(m,&stats)==TINYRT_BUSY);
  CHECK(tinyrt_manager_write(m,package,sizeof(package))==TINYRT_OK);
  CHECK(tinyrt_manager_finish(m)==TINYRT_OK);
 }
 CHECK(tinyrt_manager_generation(m,&before)==TINYRT_OK&&before>0);
 CHECK(tinyrt_manager_stats(m,&stats)==TINYRT_OK&&stats.generation==before&&stats.installed_count==16);
 CHECK(stats.package_bytes==6400&&stats.allocated_bytes==65536);
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==16);
 for(unsigned i=0;i<count;++i){
  snprintf(id,sizeof(id),"catalog.%02u",i);
  CHECK(!strcmp(listed[i].app.id.app_id,id));
 }
 tinyrt_app_info_t extra;
 test_package_make(package,sizeof(package),"catalog.16",1,13,&extra);
 unsigned mutations=nor.mutations;
 CHECK(tinyrt_manager_begin(m,&extra)==TINYRT_NO_SPACE&&nor.mutations==mutations);
 CHECK(tinyrt_manager_start(m,&apps[0].id,466,466,&frame)==TINYRT_OK);
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==16);
 CHECK(tinyrt_manager_generation(m,&after)==TINYRT_OK&&after==before);
 CHECK(tinyrt_manager_stats(m,&stats)==TINYRT_OK&&stats.generation==before);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
 tinyrt_manager_close(m);m=open_manager();
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==16);
 for(unsigned i=0;i<16;++i)CHECK(tinyrt_manager_uninstall(m,&apps[i].id)==TINYRT_OK);
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==0);
 tinyrt_manager_close(m);
 saves=0;clears=0;runtime_destroy_count=0;clock_ms=42;
}
static void test_skipped_frame_and_clock(void) {
 fake_nor_init(&nor);tinyrt_manager_t*m=open_manager();
 CHECK(tinyrt_manager_clock_interval_ms(NULL)==100&&tinyrt_manager_clock_interval_ms(m)==100);
 CHECK(tinyrt_manager_memory_used(NULL)==0&&tinyrt_manager_memory_peak(NULL)==0);
 tinyrt_app_info_t a=install(m,1);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK&&tinyrt_manager_clock_interval_ms(m)==17);
 CHECK(tinyrt_manager_memory_used(m)==256&&tinyrt_manager_memory_peak(m)==256);
 /* Caller sentinels differ from the manager's frame. Skip may only touch count. */
 memset(&frame,0xa5,sizeof(frame));static tinyrt_frame_t previous;previous=frame;previous.count=0;
 runtime_skip_frame=1;
 CHECK(tinyrt_manager_event(m,2,0,0,0,&frame)==TINYRT_OK);
 CHECK(!memcmp(&frame,&previous,sizeof(frame))&&saves==1&&kv_values[0]==1);
 CHECK(tinyrt_manager_clock_interval_ms(m)==17);
 CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&tinyrt_manager_clock_interval_ms(m)==100);
 tinyrt_manager_close(m);runtime_skip_frame=0;
 saves=0;clears=0;runtime_destroy_count=0;clock_ms=42;
}
static void test_output_ownership(void) {
 fake_nor_init(&nor);tinyrt_manager_t*m=open_manager();tinyrt_app_info_t a=install(m,1);
 static tinyrt_frame_t front,back,snapshot;
 memset(&front,0xa5,sizeof(front));
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&front)==TINYRT_OK&&front.count==1);
 CHECK(((uint8_t*)&front.commands[1])[0]==0xa5&&front.pixels[0]==0xa5);
 snapshot=front;memset(&back,0x5a,sizeof(back));
 CHECK(tinyrt_manager_event(m,1,0,0,0,&back)==TINYRT_OK&&back.count==1);
 CHECK(((uint8_t*)&back.commands[1])[0]==0x5a&&back.pixels[0]==0x5a);
 CHECK(!memcmp(&front,&snapshot,sizeof(front)));
 clock_ms+=5000;fail_save=1;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&back)==TINYRT_IO_ERROR&&back.count==0);
 CHECK(!memcmp(&front,&snapshot,sizeof(front))&&kv_values[0]==1);
 fail_save=0;back.count=99;
 CHECK(tinyrt_manager_event(m,1,0,0,0,&back)==TINYRT_NOT_FOUND&&back.count==0);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&back)==TINYRT_OK&&back.count==1);
 memset(&back,0xcc,sizeof(back));CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
 CHECK(back.count==0xcccccccc&&back.pixels[0]==0xcc);
 tinyrt_manager_close(m);
 CHECK(!memcmp(&front,&snapshot,sizeof(front)));
 saves=0;clears=0;runtime_destroy_count=0;clock_ms=42;
}
int main(void) {
 test_output_ownership();
 test_skipped_frame_and_clock();
 test_large_catalog();
 test_coalescing();
 fake_nor_init(&nor);tinyrt_manager_t*stop_test=open_manager();
 tinyrt_app_info_t stopped=install(stop_test,1);
 CHECK(tinyrt_manager_start(stop_test,&stopped.id,466,466,&frame)==TINYRT_OK);
 runtime_stop_write=1;tinyrt_manager_stop(stop_test);
 CHECK(kv_mask==1&&kv_values[0]==77&&saves==1);
 runtime_stop_write=0;tinyrt_manager_close(stop_test);
 clears=0;saves=0;runtime_destroy_count=0;
 fake_nor_init(&nor);tinyrt_manager_t*m=open_manager();
 kv_mask=1;kv_values[0]=999;tinyrt_app_info_t a=install(m,1);
 CHECK(clears==1&&kv_mask==0);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 tinyrt_package_metadata_t live_apps[TINYRT_STORE_MAX_APPS];uint32_t live_count=0;
 CHECK(tinyrt_manager_list(m,live_apps,&live_count)==TINYRT_OK&&live_count==1&&live_apps[0].app.id.version==1);
 CHECK(tinyrt_manager_event(m,1,1,1,0,&frame)==TINYRT_OK);
 CHECK(frame.commands[0].rgb==1&&kv_values[0]==1&&saves==1);
 runtime_fail_event=1;
 CHECK(tinyrt_manager_event(m,1,1,1,0,&frame)==TINYRT_VERIFY_FAILED);
 CHECK(kv_values[0]==1&&saves==1&&runtime_destroy_count==1);runtime_fail_event=0;
 tinyrt_app_info_t b=install(m,2);CHECK(clears==1&&kv_values[0]==1);
 CHECK(tinyrt_manager_uninstall(m,&a.id)==TINYRT_NOT_FOUND);
 CHECK(tinyrt_manager_start(m,&b.id,466,466,&frame)==TINYRT_OK&&frame.commands[0].rgb==1);
 clock_ms+=5000;fail_save=1;CHECK(tinyrt_manager_event(m,1,1,1,0,&frame)==TINYRT_IO_ERROR);
 CHECK(kv_values[0]==1&&frame.count==0);fail_save=0;
 CHECK(tinyrt_manager_uninstall(m,&b.id)==TINYRT_OK);
 kv_mask=1;kv_values[0]=777; /* Simulate cleanup not completing after uninstall. */
 fail_clear=1;test_package_make(package,sizeof(package),"counter",3,13,&a);
 unsigned writes=nor.mutations;
 CHECK(tinyrt_manager_begin(m,&a)==TINYRT_IO_ERROR&&nor.mutations==writes);
 fail_clear=0;b=install(m,3);CHECK(kv_mask==0&&kv_values[0]==0);
 tinyrt_package_metadata_t listed[TINYRT_STORE_MAX_APPS];uint32_t count=0;
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==1&&listed[0].app.id.version==3);
 metadata_swap=1;
 CHECK(tinyrt_manager_start(m,&b.id,466,466,&frame)==TINYRT_VERIFY_FAILED);
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_VERIFY_FAILED&&count==0);metadata_swap=0;
 tinyrt_manager_close(m);m=open_manager();tinyrt_app_info_t out;
 CHECK(tinyrt_manager_query(m,&b.id,&out)==TINYRT_OK);tinyrt_manager_close(m);
 test_cover_ranges_and_identity();
 test_aot_selection();
 printf("MANAGER_TESTS checks=%u PASS (real NOR/store, explicit runtime/crypto boundaries)\n",checks);return 0;
}
