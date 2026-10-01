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
static int32_t kv_values[16];
static fake_nor_t nor;
static uint8_t package[400];
static tinyrt_frame_t frame;
extern unsigned runtime_destroy_count;
extern int runtime_fail_event;
extern int metadata_swap;
static tinyrt_status_t load(void*c,const char*id,uint32_t*m,int32_t v[16]) {(void)c;(void)id;*m=kv_mask;memcpy(v,kv_values,sizeof(kv_values));return TINYRT_OK;}
static tinyrt_status_t save(void*c,const char*id,uint32_t m,const int32_t v[16]) {(void)c;(void)id;if(fail_save)return TINYRT_IO_ERROR;kv_mask=m;memcpy(kv_values,v,sizeof(kv_values));saves++;return TINYRT_OK;}
static tinyrt_status_t clear(void*c,const char*id) {(void)c;(void)id;if(fail_clear)return TINYRT_IO_ERROR;kv_mask=0;memset(kv_values,0,sizeof(kv_values));clears++;return TINYRT_OK;}
static uint32_t now(void*c){(void)c;return 42;}
static tinyrt_manager_t *open_manager(void) {
 tinyrt_store_io_t io=fake_nor_io(&nor);static tinyrt_package_verifier_t verifier={0};
 tinyrt_manager_storage_t storage={NULL,load,save,clear,now};tinyrt_manager_t*m=NULL;
 CHECK(tinyrt_manager_open(&io,&verifier,&storage,&m)==TINYRT_OK&&m);return m;
}
static tinyrt_app_info_t install(tinyrt_manager_t*m,uint32_t version) {
 tinyrt_app_info_t a;test_package_make(package,sizeof(package),"counter",version,13,&a);
 CHECK(tinyrt_manager_begin(m,&a)==TINYRT_OK);
 CHECK(tinyrt_manager_write(m,package,sizeof(package))==TINYRT_OK);
 CHECK(tinyrt_manager_finish(m)==TINYRT_OK);return a;
}
int main(void) {
 fake_nor_init(&nor);tinyrt_manager_t*m=open_manager();
 kv_mask=1;kv_values[0]=999;tinyrt_app_info_t a=install(m,1);
 CHECK(clears==1&&kv_mask==0);
 CHECK(tinyrt_manager_start(m,&a.id,466,466,&frame)==TINYRT_OK);
 tinyrt_package_metadata_t live_apps[2];uint32_t live_count=0;
 CHECK(tinyrt_manager_list(m,live_apps,&live_count)==TINYRT_OK&&live_count==1&&live_apps[0].app.id.version==1);
 CHECK(tinyrt_manager_event(m,1,1,1,0,&frame)==TINYRT_OK);
 CHECK(frame.commands[0].rgb==1&&kv_values[0]==1&&saves==1);
 runtime_fail_event=1;
 CHECK(tinyrt_manager_event(m,1,1,1,0,&frame)==TINYRT_VERIFY_FAILED);
 CHECK(kv_values[0]==1&&saves==1&&runtime_destroy_count==1);runtime_fail_event=0;
 tinyrt_app_info_t b=install(m,2);CHECK(clears==1&&kv_values[0]==1);
 CHECK(tinyrt_manager_uninstall(m,&a.id)==TINYRT_NOT_FOUND);
 CHECK(tinyrt_manager_start(m,&b.id,466,466,&frame)==TINYRT_OK&&frame.commands[0].rgb==1);
 fail_save=1;CHECK(tinyrt_manager_event(m,1,1,1,0,&frame)==TINYRT_IO_ERROR);
 CHECK(kv_values[0]==1&&frame.commands[0].rgb==1);fail_save=0;
 CHECK(tinyrt_manager_uninstall(m,&b.id)==TINYRT_OK);
 kv_mask=1;kv_values[0]=777; /* Simulate cleanup not completing after uninstall. */
 fail_clear=1;test_package_make(package,sizeof(package),"counter",3,13,&a);
 unsigned writes=nor.mutations;
 CHECK(tinyrt_manager_begin(m,&a)==TINYRT_IO_ERROR&&nor.mutations==writes);
 fail_clear=0;b=install(m,3);CHECK(kv_mask==0&&kv_values[0]==0);
 tinyrt_package_metadata_t listed[2];uint32_t count=0;
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==1&&listed[0].app.id.version==3);
 metadata_swap=1;
 CHECK(tinyrt_manager_start(m,&b.id,466,466,&frame)==TINYRT_VERIFY_FAILED);
 CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_VERIFY_FAILED&&count==0);metadata_swap=0;
 tinyrt_manager_close(m);m=open_manager();tinyrt_app_info_t out;
 CHECK(tinyrt_manager_query(m,&b.id,&out)==TINYRT_OK);tinyrt_manager_close(m);
 printf("MANAGER_TESTS checks=%u PASS (real NOR/store, explicit runtime/crypto boundaries)\n",checks);return 0;
}
