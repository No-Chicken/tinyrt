#include "tinyrt_manager.h"
#include "fake_nor.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks; if(!(x)) {fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); exit(1);}} while(0)
static fake_nor_t nor;
static tinyrt_package_trusted_key_t keys[2]={{.key_id=1,.app_id_prefix="demo."},{.key_id=2,.app_id_prefix="other."}};
static tinyrt_package_verifier_t verifier={keys,2,tinyrt_runtime_validate_wasm,NULL};
static tinyrt_frame_t frame,previous;
static unsigned saves,clears;
static unsigned guard_arms,guard_disarms;
static bool guard_active;
static tinyrt_status_t guard_arm(void *ctx,void (*cancel)(void *),void *arg,uint32_t timeout_ms) {
    (void)ctx;CHECK(cancel&&arg&&timeout_ms==3000&&!guard_active);
    guard_active=true;++guard_arms;return TINYRT_OK;
}
static void guard_disarm(void *ctx) {
    (void)ctx;CHECK(guard_active);guard_active=false;++guard_disarms;
}
static const tinyrt_execution_guard_t execution_guard={NULL,guard_arm,guard_disarm,3000};
static bool fail_clear,fail_save;
typedef struct {char id[32]; uint32_t mask; int32_t values[16];} kv_record_t;
static kv_record_t records[4];
static kv_record_t *record(const char *id) {
    for(unsigned i=0;i<4;++i) if(!strcmp(records[i].id,id)) return &records[i];
    for(unsigned i=0;i<4;++i) if(!records[i].id[0]) {
        CHECK(strlen(id)<sizeof(records[i].id));
        memcpy(records[i].id,id,strlen(id)+1); return &records[i];
    }
    CHECK(false); return NULL;
}
static tinyrt_status_t kv_load(void *ctx,const char *id,uint32_t *mask,int32_t values[16]) {
    (void)ctx; kv_record_t *r=record(id); *mask=r->mask; memcpy(values,r->values,sizeof(r->values)); return TINYRT_OK;
}
static tinyrt_status_t kv_save(void *ctx,const char *id,uint32_t mask,const int32_t values[16]) {
    (void)ctx; if(fail_save) return TINYRT_IO_ERROR;
    kv_record_t *r=record(id); r->mask=mask; memcpy(r->values,values,sizeof(r->values)); ++saves; return TINYRT_OK;
}
static tinyrt_status_t kv_clear(void *ctx,const char *id) {
    (void)ctx; if(fail_clear) return TINYRT_IO_ERROR;
    kv_record_t *r=record(id); r->mask=0; memset(r->values,0,sizeof(r->values)); ++clears; return TINYRT_OK;
}
static uint32_t clock_ms=1234;
static uint32_t now(void *ctx) {(void)ctx; return clock_ms;}
static tinyrt_manager_t *open_manager(void) {
    tinyrt_manager_t *m=NULL; tinyrt_store_io_t io=fake_nor_io(&nor);
    tinyrt_manager_storage_t kv={NULL,kv_load,kv_save,kv_clear,now};
    tinyrt_status_t result=tinyrt_manager_open(&io,&verifier,&kv,&m);
    if(result!=TINYRT_OK) fprintf(stderr,"open status=%d\n",result);
    CHECK(result==TINYRT_OK && m);
    CHECK(tinyrt_manager_set_execution_guard(m,&execution_guard)==TINYRT_OK);return m;
}
static size_t read_file(const char *path,void *out,size_t capacity) {
    FILE *f=fopen(path,"rb"); CHECK(f!=NULL);
    size_t n=fread(out,1,capacity,f); CHECK(!ferror(f) && fgetc(f)==EOF); fclose(f); return n;
}
static uint32_t get32(const uint8_t *b) {return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static uint8_t package[TINYRT_STORE_MAX_PACKAGE_SIZE+1];
static tinyrt_app_info_t load_package(const char *dir,const char *name,uint32_t *size) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.trpkg",dir,name);
    *size=(uint32_t)read_file(path,package,sizeof(package));
    uint8_t identity[73]; snprintf(path,sizeof(path),"%s/%s.identity",dir,name);
    CHECK(read_file(path,identity,sizeof(identity))==72);
    tinyrt_app_info_t app={0}; memcpy(app.id.app_id,identity,32); app.id.version=get32(identity+32);
    memcpy(app.id.sha256,identity+36,32); app.package_size=get32(identity+68);
    CHECK(app.package_size==*size); return app;
}
static tinyrt_app_info_t install(tinyrt_manager_t *m,const char *dir,const char *name,tinyrt_status_t expected) {
    uint32_t size; tinyrt_app_info_t app=load_package(dir,name,&size);
    tinyrt_status_t result=tinyrt_manager_begin(m,&app);
    CHECK(result==TINYRT_OK);
    for(uint32_t offset=0;offset<size;) {
        uint32_t n=size-offset; if(n>777) n=777;
        CHECK(tinyrt_manager_write(m,package+offset,n)==TINYRT_OK); offset+=n;
    }
    result=tinyrt_manager_finish(m);
    if(result!=expected) fprintf(stderr,"install %s status=%d expected=%d\n",name,result,expected);
    CHECK(result==expected); return app;
}
static void counter_frame(const char *version,const char *number,uint32_t background) {
    CHECK(frame.count==5 && frame.commands[0].kind==TINYRT_DRAW_CLEAR);
    CHECK(frame.commands[0].rgb==background);
    CHECK(!strcmp(frame.commands[2].text,version));
    CHECK(!strcmp(frame.commands[3].text,number));
}
static tinyrt_manager_t *reboot(tinyrt_manager_t *m) {
    tinyrt_manager_close(m); tinyrt_runtime_system_shutdown();
    CHECK(tinyrt_runtime_memory_used()==0);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    return open_manager(); /* NOR bytes and simulated persistent KV survive. */
}
static void corrupt_package(const char *dir,const char *name,uint32_t byte) {
    uint32_t size;(void)load_package(dir,name,&size);
    for(uint32_t off=2*TINYRT_STORE_SECTOR_SIZE;off<=TINYRT_STORE_SIZE-size;off+=TINYRT_STORE_SECTOR_SIZE) {
        if(!memcmp(nor.bytes+off,package,size)) {
            CHECK(byte<size);nor.bytes[off+byte]^=1;return;
        }
    }
    CHECK(false);
}
#ifdef TINYRT_TEST_SIGNED_AOT
static void test_signed_aot(const char *dir) {
    tinyrt_package_aot_profile_t profile={true,false,5,"x86_64","generic",{1}};
    tinyrt_runtime_aot_config_t config={&profile,&execution_guard};
    verifier.aot_profile=&profile;verifier.validate_aot=tinyrt_runtime_validate_aot;verifier.aot_ctx=&config;
    keys[0].aot_authority=TINYRT_AOT_AUTHORITY_RELEASE;
    fake_nor_init(&nor);memset(records,0,sizeof(records));tinyrt_manager_t *m=open_manager();
    size_t base=tinyrt_runtime_memory_used();
    tinyrt_app_info_t app=install(m,dir,"counter-aot",TINYRT_OK);
    tinyrt_package_metadata_t catalog[TINYRT_STORE_MAX_APPS];uint32_t count=0;
    CHECK(tinyrt_manager_list(m,catalog,&count)==TINYRT_OK && count==1 && catalog[0].execution_kind==TINYRT_PACKAGE_EXEC_AOT);
    CHECK(tinyrt_manager_start(m,&app.id,466,466,&frame)==TINYRT_OK);counter_frame("COUNTER V1","0",0x181818);
    previous=frame;fail_save=true;
    CHECK(tinyrt_manager_event(m,1,10,10,0,&frame)==TINYRT_IO_ERROR && frame.count==0);
    CHECK(previous.count==5 && previous.commands[0].rgb==0x181818 && !strcmp(previous.commands[3].text,"0"));
    CHECK(tinyrt_runtime_memory_used()==base);fail_save=false;
    CHECK(tinyrt_manager_start(m,&app.id,466,466,&frame)==TINYRT_OK);counter_frame("COUNTER V1","0",0x181818);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
    install(m,dir,"counter-aot-invalid",TINYRT_VERIFY_FAILED);
    app=install(m,dir,"counter-aot-error",TINYRT_OK);
    CHECK(tinyrt_manager_start(m,&app.id,466,466,&frame)==TINYRT_OK);
    CHECK(tinyrt_manager_event(m,1,0,0,0,&frame)==TINYRT_VERIFY_FAILED && frame.count==0);
    CHECK(record("demo.counter")->values[0]==0 && tinyrt_runtime_memory_used()==base);
    tinyrt_manager_close(m);
    /* The same signed package explicitly falls back to Wasm when disabled. */
    profile.enabled=false;m=open_manager();
    CHECK(tinyrt_manager_list(m,catalog,&count)==TINYRT_OK && count==1 && catalog[0].execution_kind==TINYRT_PACKAGE_EXEC_WASM
          && catalog[0].fallback_reason==TINYRT_PACKAGE_FALLBACK_DISABLED);
    CHECK(tinyrt_manager_start(m,&app.id,466,466,&frame)==TINYRT_OK);
    tinyrt_manager_close(m);verifier.aot_profile=NULL;verifier.validate_aot=NULL;verifier.aot_ctx=NULL;
    keys[0].aot_authority=TINYRT_AOT_AUTHORITY_NONE;
}
#endif
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    CHECK(read_file(PUBLIC_KEY_FILE,keys[0].public_key,65)==65);
    char public2[1024];snprintf(public2,sizeof(public2),"%s/publisher2.bin",argv[1]);
    CHECK(read_file(public2,keys[1].public_key,65)==65);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    fake_nor_init(&nor);
    kv_record_t *kv=record("demo.counter"); kv->mask=1; kv->values[0]=999;
    tinyrt_manager_t *m=open_manager();
    tinyrt_app_info_t v1=install(m,argv[1],"counter-v1",TINYRT_OK);
    CHECK(clears==1 && kv->mask==0 && kv->values[0]==0);
    CHECK(tinyrt_manager_start(m,&v1.id,466,466,&frame)==TINYRT_OK);
    CHECK(guard_arms==2&&guard_disarms==2&&!guard_active);
    CHECK(tinyrt_manager_set_execution_guard(m,NULL)==TINYRT_BUSY);
    CHECK(tinyrt_manager_input_events(m)==0);
    counter_frame("COUNTER V1","0",0x181818);
    CHECK(tinyrt_manager_event(m,1,30,40,0,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","1",0x181818);
    CHECK(kv->mask==1 && kv->values[0]==1 && saves==1);
    /* Listing/querying a running app must not seize a second WAMR instance. */
    tinyrt_package_metadata_t listed[TINYRT_STORE_MAX_APPS]; uint32_t count=0;
    tinyrt_status_t list_status=tinyrt_manager_list(m,listed,&count);
    if(list_status!=TINYRT_OK) fprintf(stderr,"live list status=%d\n",list_status);
    CHECK(list_status==TINYRT_OK && count==1 && listed[0].app.id.version==1);
    tinyrt_app_info_t found;
    CHECK(tinyrt_manager_query(m,&v1.id,&found)==TINYRT_OK);
    tinyrt_app_info_t bad=install(m,argv[1],"invalid-signature",TINYRT_VERIFY_FAILED);
    CHECK(kv->values[0]==1 && saves==1);
    CHECK(tinyrt_manager_query(m,&v1.id,&found)==TINYRT_OK);
    CHECK(tinyrt_manager_query(m,&bad.id,&found)==TINYRT_NOT_FOUND);
    m=reboot(m);
    CHECK(tinyrt_manager_start(m,&v1.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","1",0x181818);
    unsigned old_clears=clears;
    tinyrt_app_info_t v2=install(m,argv[1],"counter-v2",TINYRT_OK);
    CHECK(clears==old_clears && kv->values[0]==1);
    CHECK(tinyrt_manager_query(m,&v1.id,&found)==TINYRT_NOT_FOUND);
    CHECK(tinyrt_manager_start(m,&v2.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V2","1",0x102a43);
    CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK && count==1 && listed[0].app.id.version==2);
    CHECK(tinyrt_manager_event(m,1,40,40,0,&frame)==TINYRT_OK);
    counter_frame("COUNTER V2","2",0x102a43);
    CHECK(kv->values[0]==2 && saves==2);
    tinyrt_app_info_t foreign=install(m,argv[1],"counter-cross-publisher",TINYRT_VERIFY_FAILED);
    CHECK(tinyrt_manager_query(m,&v2.id,&found)==TINYRT_OK);
    CHECK(tinyrt_manager_query(m,&foreign.id,&found)==TINYRT_NOT_FOUND);
    CHECK(kv->values[0]==2&&saves==2);
    CHECK(tinyrt_manager_start(m,&v2.id,466,466,&frame)==TINYRT_OK);
    /* A failed back frame cannot be published; the caller's front stays owned. */
    previous=frame; fail_save=true; clock_ms+=5000;
    CHECK(tinyrt_manager_event(m,1,40,40,0,&frame)==TINYRT_IO_ERROR);
    CHECK(kv->values[0]==2 && saves==2 && frame.count==0);
    CHECK(previous.count==5 && !strcmp(previous.commands[3].text,"2"));
    fail_save=false;
    CHECK(tinyrt_manager_start(m,&v2.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V2","2",0x102a43);
    tinyrt_app_info_t error=install(m,argv[1],"counter-error",TINYRT_OK);
    CHECK(kv->values[0]==2 && clears==old_clears);
    CHECK(tinyrt_manager_start(m,&error.id,466,466,&frame)==TINYRT_OK);
    previous=frame; unsigned saves_before=saves;
    CHECK(tinyrt_manager_event(m,1,30,40,0,&frame)==TINYRT_VERIFY_FAILED);
    CHECK(kv->values[0]==2 && saves==saves_before && frame.count==0);
    CHECK(tinyrt_manager_error(m)[0]!=0);
    CHECK(tinyrt_manager_event(m,1,30,40,0,&frame)==TINYRT_NOT_FOUND);
    fail_clear=true;
    CHECK(tinyrt_manager_uninstall(m,&error.id)==TINYRT_OK); /* Simulate cleanup failure. */
    CHECK(kv->values[0]==2);
    CHECK(tinyrt_manager_query(m,&error.id,&found)==TINYRT_NOT_FOUND);
    uint32_t size; v1=load_package(argv[1],"counter-v1",&size);
    unsigned mutations=nor.mutations;
    CHECK(tinyrt_manager_begin(m,&v1)==TINYRT_IO_ERROR && nor.mutations==mutations);
    fail_clear=false;
    v1=install(m,argv[1],"counter-v1",TINYRT_OK);
    CHECK(kv->mask==0 && kv->values[0]==0);
    CHECK(tinyrt_manager_start(m,&v1.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","0",0x181818);
    (void)install(m,argv[1],"invalid-wasm",TINYRT_VERIFY_FAILED);
    CHECK(tinyrt_manager_query(m,&v1.id,&found)==TINYRT_OK);
    m=reboot(m);
    CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK && count==1 && listed[0].app.id.version==1);
    CHECK(tinyrt_manager_start(m,&v1.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","0",0x181818);
    /* Real signed optional-stop guests: no render, failed callback cannot
     * persist its host RAM changes, and every outcome releases the instance. */
    tinyrt_app_info_t stop=install(m,argv[1],"stop_valid",TINYRT_OK);
    kv_record_t *stop_kv=record("demo.stop");unsigned stop_saves=saves;
    CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_OK);
    CHECK(stop_kv->mask==0 && saves==stop_saves);
    previous=frame;
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
    CHECK(stop_kv->mask==1 && stop_kv->values[0]==77 && saves==stop_saves+1);
    CHECK(!memcmp(&frame,&previous,sizeof(frame)));
    CHECK(tinyrt_manager_event(m,2,0,0,0,&frame)==TINYRT_NOT_FOUND);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK && saves==stop_saves+1);
    CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_OK);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK && saves==stop_saves+1); /* same value */
    /* Simulated transactional NVS failure, including error preservation. */
    stop_kv->values[0]=12;stop_saves=saves;
    CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_OK);
    fail_save=true;
    CHECK(tinyrt_manager_stop(m)==TINYRT_IO_ERROR);
    CHECK(stop_kv->values[0]==12 && saves==stop_saves);
    CHECK(strstr(tinyrt_manager_error(m),"persistence")!=NULL);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK && tinyrt_manager_error(m)[0]);fail_save=false;
    const char *stop_errors[]={"stop_failure","stop_trap","stop_spin"};
    for(unsigned i=0;i<3;++i){
        stop=install(m,argv[1],stop_errors[i],TINYRT_OK);
        CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_OK);
        CHECK(tinyrt_manager_stop(m)==TINYRT_VERIFY_FAILED);
        CHECK(stop_kv->values[0]==12 && saves==stop_saves);
        CHECK(tinyrt_manager_error(m)[0]);
        CHECK(tinyrt_manager_event(m,2,0,0,0,&frame)==TINYRT_NOT_FOUND);
    }
    (void)install(m,argv[1],"stop_bad_signature",TINYRT_VERIFY_FAILED);
    CHECK(tinyrt_manager_query(m,&stop.id,&found)==TINYRT_OK);
    /* Only one real signed package is corrupted. Reopening must preserve the
     * healthy app, committed bad identity and original anti-downgrade floor. */
    corrupt_package(argv[1],"stop_spin",200);
    CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_VERIFY_FAILED);
    CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==1);
    unsigned mutations_before=nor.mutations;m=reboot(m);
    CHECK(nor.mutations==mutations_before);
    CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==1);
    CHECK(!strcmp(listed[0].app.id.app_id,"demo.counter"));
    tinyrt_app_info_t quarantined[TINYRT_STORE_MAX_APPS];
    CHECK(tinyrt_manager_list_quarantined(m,quarantined,&count)==TINYRT_OK&&count==1);
    CHECK(!memcmp(&quarantined[0].id,&stop.id,sizeof(stop.id)));
    memset(&found,0xa5,sizeof(found));
    CHECK(tinyrt_manager_query(m,&stop.id,&found)==TINYRT_VERIFY_FAILED&&found.id.version==0);
    CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_VERIFY_FAILED);
    CHECK(tinyrt_manager_start(m,&v1.id,466,466,&frame)==TINYRT_OK);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
    tinyrt_package_id_t wrong=stop.id;wrong.sha256[0]^=1;
    CHECK(tinyrt_manager_uninstall(m,&wrong)==TINYRT_NOT_FOUND);
    unsigned repair_clears=clears;
    stop=install(m,argv[1],"stop_spin",TINYRT_OK);
    CHECK(clears==repair_clears&&stop_kv->values[0]==12);
    CHECK(tinyrt_manager_list_quarantined(m,quarantined,&count)==TINYRT_OK&&count==0);
    corrupt_package(argv[1],"stop_spin",260);m=reboot(m);
    CHECK(tinyrt_manager_query(m,&stop.id,&found)==TINYRT_VERIFY_FAILED);
    stop=install(m,argv[1],"stop_repair",TINYRT_OK);
    CHECK(clears==repair_clears&&stop_kv->values[0]==12);
    CHECK(tinyrt_manager_start(m,&stop.id,466,466,&frame)==TINYRT_OK);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&stop_kv->values[0]==77);
    unsigned switch_saves=saves;
    CHECK(tinyrt_manager_start(m,&v1.id,466,466,&frame)==TINYRT_OK);
    CHECK(tinyrt_manager_event(m,1,40,40,0,&frame)==TINYRT_OK);
    CHECK(saves==switch_saves&&kv->values[0]==0); /* Same periodic timer across apps. */
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&saves==switch_saves+1&&kv->values[0]==1);
    corrupt_package(argv[1],"stop_repair",200);m=reboot(m);
    CHECK(tinyrt_manager_uninstall(m,&stop.id)==TINYRT_OK);
    CHECK(tinyrt_manager_query(m,&stop.id,&found)==TINYRT_NOT_FOUND);
    m=reboot(m);
    CHECK(tinyrt_manager_list(m,listed,&count)==TINYRT_OK&&count==1);
    CHECK(tinyrt_manager_list_quarantined(m,quarantined,&count)==TINYRT_OK&&count==0);
    tinyrt_manager_close(m);
    /* An unavailable real Wasm validator is a host lifecycle error. It must
     * neither create a quarantined catalog on open nor change existing health. */
    tinyrt_store_io_t io=fake_nor_io(&nor);
    tinyrt_store_t *healthy_store=NULL,*unavailable_store=(void *)1;
    CHECK(tinyrt_store_open(&io,tinyrt_package_verify,&verifier,&healthy_store)==TINYRT_OK);
    tinyrt_runtime_system_shutdown();
    unsigned unavailable_mutations=nor.mutations;
    CHECK(tinyrt_store_open(&io,tinyrt_package_verify,&verifier,&unavailable_store)==TINYRT_INVALID_ARGUMENT);
    CHECK(unavailable_store==NULL&&nor.mutations==unavailable_mutations);
    CHECK(tinyrt_store_recheck(healthy_store,&v1.id)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_store_query(healthy_store,&v1.id,&found)==TINYRT_OK);
    CHECK(tinyrt_store_list_quarantined(healthy_store,quarantined,2,&count)==TINYRT_OK&&count==0);
    tinyrt_store_close(healthy_store);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    CHECK(tinyrt_store_open(&io,tinyrt_package_verify,&verifier,&healthy_store)==TINYRT_OK);
    CHECK(tinyrt_store_query(healthy_store,&v1.id,&found)==TINYRT_OK);
    CHECK(tinyrt_store_list(healthy_store,quarantined,2,&count)==TINYRT_OK&&count==1);
    CHECK(tinyrt_store_list_quarantined(healthy_store,quarantined,2,&count)==TINYRT_OK&&count==0);
    CHECK(nor.mutations==unavailable_mutations);
    tinyrt_store_close(healthy_store);
    /* The signed limit includes assets; only its real Wasm section becomes
     * guest memory. Exercise the whole maximum-size store/manager path. */
    fake_nor_init(&nor);memset(records,0,sizeof(records));
    m=open_manager();
    tinyrt_app_info_t maximum=install(m,argv[1],"counter-maximum",TINYRT_OK);
    CHECK(maximum.package_size==0x200000);
    CHECK(tinyrt_manager_start(m,&maximum.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","0",0x181818);
    CHECK(tinyrt_manager_event(m,1,40,40,0,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","1",0x181818);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
    m=reboot(m);
    CHECK(tinyrt_manager_query(m,&maximum.id,&found)==TINYRT_OK&&found.package_size==0x200000);
    CHECK(tinyrt_manager_start(m,&maximum.id,466,466,&frame)==TINYRT_OK);
    counter_frame("COUNTER V1","1",0x181818);
    tinyrt_manager_close(m);
    fake_nor_init(&nor);memset(records,0,sizeof(records));m=open_manager();
    CHECK(tinyrt_manager_clock_interval_ms(m)==100);
    tinyrt_app_info_t pixels=install(m,argv[1],"pixel_then_skip",TINYRT_OK);
    CHECK(tinyrt_manager_start(m,&pixels.id,466,466,&frame)==TINYRT_OK&&frame.pixel_bytes==8);
    CHECK(frame.count==2&&frame.commands[1].kind==TINYRT_DRAW_RGB565&&frame.pixels[1]==0xf8);
    memset(&frame,0xa5,sizeof(frame));previous=frame;previous.count=0;
    CHECK(tinyrt_manager_event(m,2,0,0,0,&frame)==TINYRT_OK&&!memcmp(&frame,&previous,sizeof(frame)));
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK);
    tinyrt_app_info_t clock=install(m,argv[1],"clock_valid",TINYRT_OK);
    CHECK(tinyrt_manager_start(m,&clock.id,466,466,&frame)==TINYRT_OK&&tinyrt_manager_clock_interval_ms(m)==1);
    CHECK(tinyrt_manager_event(m,2,0,0,1000,&frame)==TINYRT_OK&&tinyrt_manager_clock_interval_ms(m)==1000);
    CHECK(tinyrt_manager_stop(m)==TINYRT_OK&&tinyrt_manager_clock_interval_ms(m)==100);
    tinyrt_manager_close(m);
#ifdef TINYRT_TEST_SIGNED_AOT
    test_signed_aot(argv[1]);
#endif
    tinyrt_runtime_system_shutdown();
    CHECK(tinyrt_runtime_memory_used()==0);
    printf("INTEGRATION checks=%u PASS (real SHA256/P256/store/manager/WAMR; simulated NOR/KV)\n",checks);
    return 0;
}
