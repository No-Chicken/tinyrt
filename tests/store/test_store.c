#include "fake_nor.h"
#include "test_package_verifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, tests;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
#define RUN(fn) do { fn(); ++tests; printf("PASS %s\n", #fn); } while (0)
static fake_nor_t nor;
static void test_blank_open(void) {
    tinyrt_store_t *s = NULL; uint32_t count = 99;
    fake_nor_init(&nor); tinyrt_store_io_t io = fake_nor_io(&nor);
    CHECK(tinyrt_store_open(&io, test_package_verify, NULL, &s) == TINYRT_OK);
    CHECK(tinyrt_store_list(s, NULL, 0, &count) == TINYRT_OK && count == 0);
    CHECK(nor.mutations == 0); tinyrt_store_close(s);
}
static void test_slot_bounds(void) {
    uint8_t b = 0; fake_nor_init(&nor); tinyrt_store_io_t io = fake_nor_io(&nor);
    CHECK(io.program(io.ctx, UINT32_MAX, &b, 2) == TINYRT_INVALID_ARGUMENT);
    CHECK(io.erase(io.ctx, TINYRT_STORE_SIZE, 4096) == TINYRT_INVALID_ARGUMENT);
    CHECK(io.read(io.ctx, TINYRT_STORE_SIZE, &b, 1) == TINYRT_INVALID_ARGUMENT);
    CHECK(nor.mutations == 0);
}
static void test_program_requires_erase(void) {
    uint8_t b = 0; fake_nor_init(&nor); tinyrt_store_io_t io = fake_nor_io(&nor);
    CHECK(io.program(io.ctx, 0x2000, &b, 1) == TINYRT_OK);
    b = 255; CHECK(io.program(io.ctx, 0x2000, &b, 1) == TINYRT_IO_ERROR);
    CHECK(io.erase(io.ctx, 0x2000, 4096) == TINYRT_OK);
    CHECK(io.program(io.ctx, 0x2000, &b, 1) == TINYRT_OK);
}

static uint8_t pkg[TINYRT_STORE_SLOT_SIZE];
static void put32(uint8_t *b,uint32_t v) {for(unsigned i=0;i<4;++i) b[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *b,uint64_t v) {for(unsigned i=0;i<8;++i) b[i]=(uint8_t)(v>>(8*i));}
static uint32_t crc32(const uint8_t *b,uint32_t n) {
 uint32_t c=UINT32_MAX;
 for(uint32_t i=0;i<n;++i) {c^=b[i];for(unsigned j=0;j<8;++j) c=(c>>1)^(UINT32_C(0xedb88320)&(0u-(c&1u)));}
 return ~c;
}
static uint32_t slot_off(unsigned i) {return 0x2000u+i*TINYRT_STORE_SLOT_SIZE;}
static tinyrt_app_info_t fixture(unsigned slot,const char *id,uint32_t ver) {
 tinyrt_app_info_t a;test_package_make(pkg,300,id,ver,17,&a);memcpy(nor.bytes+slot_off(slot),pkg,300);return a;
}
/* Independent on-media fixture builder; does not call the production encoder. */
static void directory(unsigned sector,uint64_t gen,const tinyrt_app_info_t *a,const unsigned *slots,unsigned n) {
 uint8_t *b=nor.bytes+sector*4096;memset(b,255,4096);
 memcpy(b,"TRDIR001",8);put32(b+8,1);put32(b+12,n);put64(b+16,gen);memset(b+24,0,8);
 for(unsigned i=0;i<n;++i) {
  uint8_t *p=b+32+80*i;memcpy(p,a[i].id.app_id,32);put32(p+32,a[i].id.version);
  put32(p+36,slots[i]);put32(p+40,a[i].package_size);memcpy(p+44,a[i].id.sha256,32);put32(p+76,0);
 }
 put32(b+4088,crc32(b,4088));put32(b+4092,0);
}
static tinyrt_status_t open_store(tinyrt_store_t **s) {
 tinyrt_store_io_t io=fake_nor_io(&nor);return tinyrt_store_open(&io,test_package_verify,NULL,s);
}
static void test_directory_roundtrip(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a[2]={fixture(0,"z.app",1),fixture(1,"a.app",2)};
 unsigned slots[2]={0,1};directory(0,7,a,slots,2);tinyrt_store_t *s=NULL;uint32_t count=0;
 CHECK(crc32((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
 CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t out[2];CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK && count==2);
 CHECK(!strcmp(out[0].id.app_id,"a.app") && !strcmp(out[1].id.app_id,"z.app"));
 CHECK(tinyrt_store_query(s,&a[0].id,&out[0])==TINYRT_OK);
 CHECK(out[0].package_size==300 && !memcmp(out[0].id.sha256,a[0].id.sha256,32));
 CHECK(tinyrt_store_list(s,out,1,&count)==TINYRT_NO_SPACE && count==2);
 CHECK(nor.mutations==0);tinyrt_store_close(s);
}
static void test_reject_torn_commit(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a=fixture(0,"app",1);unsigned slot=0;
 directory(0,1,NULL,NULL,0);directory(1,2,&a,&slot,1);nor.bytes[4096+4093]=255;
 tinyrt_store_t *s=NULL;uint32_t count=99;CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_list(s,NULL,0,&count)==TINYRT_OK && count==0);tinyrt_store_close(s);
}
static void test_invalid_referenced_package(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a=fixture(0,"app",1);unsigned slot=0;directory(0,1,&a,&slot,1);
 nor.bytes[slot_off(0)+100]^=1;tinyrt_store_t *s=(void *)1;
 CHECK(open_store(&s)==TINYRT_OK && s!=NULL);CHECK(nor.mutations==0);
 tinyrt_app_info_t out[2];uint32_t count=99;
 CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK && count==0);
 CHECK(tinyrt_store_query(s,&a.id,out)==TINYRT_VERIFY_FAILED);
 tinyrt_store_close(s);
}
static void test_quarantine_keeps_newest_directory(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a[2]={fixture(0,"healthy",1),fixture(1,"broken",2)};
 unsigned slots[2]={0,1};directory(0,10,a,slots,2);
 /* Newest generation uninstalled healthy. Corrupting broken must not resurrect it. */
 directory(1,11,a+1,slots+1,1);nor.bytes[slot_off(1)+100]^=1;
 tinyrt_store_t*s=NULL;tinyrt_app_info_t out[2];uint32_t count=99;
 CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK&&count==0);
 CHECK(tinyrt_store_query(s,&a[0].id,out)==TINYRT_NOT_FOUND);
 CHECK(tinyrt_store_query(s,&a[1].id,out)==TINYRT_VERIFY_FAILED);
 CHECK(nor.mutations==0);tinyrt_store_close(s);
 CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_uninstall(s,"broken")==TINYRT_OK);tinyrt_store_close(s);
 CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK&&count==0);tinyrt_store_close(s);
}
static void test_quarantine_does_not_rollback_version(void) {
 fake_nor_init(&nor);tinyrt_app_info_t old=fixture(0,"app",1),latest=fixture(1,"app",2);
 unsigned slot=0;directory(0,1,&old,&slot,1);slot=1;directory(1,2,&latest,&slot,1);
 nor.bytes[slot_off(1)+100]^=1;tinyrt_store_t*s=NULL;tinyrt_app_info_t out;
 CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&old.id,&out)==TINYRT_NOT_FOUND);
 CHECK(tinyrt_store_query(s,&latest.id,&out)==TINYRT_VERIFY_FAILED);
 tinyrt_install_t*h=NULL;CHECK(tinyrt_store_begin(s,&old,&h)==TINYRT_CONFLICT&&h==NULL);
 /* Same identity repair stages into the spare slot and re-verifies all bytes. */
 test_package_make(pkg,300,"app",2,17,&latest);
 CHECK(tinyrt_store_begin(s,&latest,&h)==TINYRT_OK);
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_OK);
 CHECK(tinyrt_store_commit(h)==TINYRT_OK);tinyrt_store_abort(h);tinyrt_store_close(s);
 CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&latest.id,&out)==TINYRT_OK);tinyrt_store_close(s);
}
static tinyrt_status_t injected_verify_status;
static tinyrt_status_t injected_verify(void*c,const tinyrt_store_io_t*io,uint32_t off,uint32_t n,tinyrt_app_info_t*a) {
 if(off==slot_off(1)&&injected_verify_status!=TINYRT_OK)return injected_verify_status;
 return test_package_verify(c,io,off,n,a);
}
static void test_resource_failure_is_not_quarantine(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a[2]={fixture(0,"healthy",1),fixture(1,"other",2)};
 unsigned slots[2]={0,1};directory(0,10,a,slots,1);directory(1,11,a,slots,2);
 tinyrt_store_io_t io=fake_nor_io(&nor);
 const tinyrt_status_t errors[]={TINYRT_IO_ERROR,TINYRT_NO_MEMORY,TINYRT_BUSY,TINYRT_INVALID_ARGUMENT};
 for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
  injected_verify_status=errors[i];tinyrt_store_t*s=(void*)1;
  CHECK(tinyrt_store_open(&io,injected_verify,NULL,&s)==errors[i]&&s==NULL);
  CHECK(nor.mutations==0);
 }
 tinyrt_store_t*s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t out[2];uint32_t count=0;
 CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK&&count==2);tinyrt_store_close(s);
 injected_verify_status=TINYRT_OK;
 CHECK(tinyrt_store_open(&io,injected_verify,NULL,&s)==TINYRT_OK);
 for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
  injected_verify_status=errors[i];CHECK(tinyrt_store_recheck(s,&a[1].id)==errors[i]);
  CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK&&count==2);
  CHECK(tinyrt_store_list_quarantined(s,out,2,&count)==TINYRT_OK&&count==0);
 }
 injected_verify_status=TINYRT_OK;nor.bytes[slot_off(1)+100]^=1;
 CHECK(tinyrt_store_recheck(s,&a[1].id)==TINYRT_VERIFY_FAILED);
 CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK&&count==1&&!strcmp(out[0].id.app_id,"healthy"));
 CHECK(tinyrt_store_list_quarantined(s,NULL,0,&count)==TINYRT_NO_SPACE&&count==1);
 CHECK(tinyrt_store_list_quarantined(s,out,2,&count)==TINYRT_OK&&count==1);
 CHECK(!strcmp(out[0].id.app_id,"other"));
 uint8_t byte=0;CHECK(tinyrt_store_read(s,&a[1].id,0,&byte,1)==TINYRT_VERIFY_FAILED);
 for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
  injected_verify_status=errors[i];CHECK(tinyrt_store_recheck(s,&a[1].id)==errors[i]);
  CHECK(tinyrt_store_list_quarantined(s,out,2,&count)==TINYRT_OK&&count==1);
 }
 CHECK(nor.mutations==0);tinyrt_store_close(s);
}
static void test_equal_generation_conflict(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a=fixture(0,"app",1);unsigned slot=0;
 directory(0,1,NULL,NULL,0);directory(1,1,&a,&slot,1);tinyrt_store_t *s=NULL;
 CHECK(open_store(&s)==TINYRT_CORRUPT);
}
static void test_initialization_recovery(void) {
 fake_nor_init(&nor);directory(0,1,NULL,NULL,0);
 memset(nor.bytes+50,255,4096-50);tinyrt_store_t *s=NULL;
 CHECK(open_store(&s)==TINYRT_OK);tinyrt_store_close(s);
 nor.bytes[slot_off(0)+30]=0;
 CHECK(open_store(&s)==TINYRT_CORRUPT && s==NULL);
}
static void test_read_error_not_fallback(void) {
 fake_nor_init(&nor);directory(0,1,NULL,NULL,0);nor.fail_read=1;tinyrt_store_t *s=NULL;
 CHECK(open_store(&s)==TINYRT_IO_ERROR && s==NULL);CHECK(nor.mutations==0);
}


static tinyrt_app_info_t install_package(tinyrt_store_t *s,const char *id,uint32_t version,uint32_t size) {
 tinyrt_app_info_t a;tinyrt_install_t *h=NULL;
 test_package_make(pkg,size,id,version,31,&a);
 CHECK(tinyrt_store_begin(s,&a,&h)==TINYRT_OK && h!=NULL);
 for(uint32_t pos=0;pos<size;) {
  uint32_t n=size-pos;if(n>777) n=777;
  CHECK(tinyrt_store_write(h,pos,pkg+pos,n)==TINYRT_OK);pos+=n;
 }
 CHECK(tinyrt_store_commit(h)==TINYRT_OK);tinyrt_store_abort(h);return a;
}
static void test_install_update_reopen(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a1=install_package(s,"app.a",1,300),b1=install_package(s,"app.b",1,400);
 tinyrt_app_info_t a2=install_package(s,"app.a",2,500),out;
 tinyrt_store_close(s);CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&a2.id,&out)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&b1.id,&out)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&a1.id,&out)==TINYRT_NOT_FOUND);tinyrt_store_close(s);
}
static void test_idempotent_commit_and_busy(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a;test_package_make(pkg,300,"app",1,9,&a);tinyrt_install_t *h=NULL,*other=(void *)1;
 CHECK(tinyrt_store_begin(s,&a,&h)==TINYRT_OK);
 CHECK(tinyrt_store_begin(s,&a,&other)==TINYRT_BUSY && other==NULL);
 CHECK(tinyrt_store_uninstall(s,"app")==TINYRT_BUSY);
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_OK);CHECK(tinyrt_store_commit(h)==TINYRT_OK);
 unsigned mutations=nor.mutations;
 CHECK(tinyrt_store_commit(h)==TINYRT_ALREADY_INSTALLED && nor.mutations==mutations);
 CHECK(tinyrt_store_uninstall(s,"app")==TINYRT_BUSY);
 tinyrt_store_abort(h);CHECK(tinyrt_store_begin(s,&a,&other)==TINYRT_ALREADY_INSTALLED && other==NULL);
 CHECK(nor.mutations==mutations);tinyrt_store_close(s);CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t out;CHECK(tinyrt_store_query(s,&a.id,&out)==TINYRT_OK);tinyrt_store_close(s);
}
static void test_capacity_version_and_order(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"a",2,300);
 (void)install_package(s,"b",1,300);
 tinyrt_app_info_t c;test_package_make(pkg,300,"c",1,1,&c);tinyrt_install_t *h=NULL;
 unsigned mutations=nor.mutations;CHECK(tinyrt_store_begin(s,&c,&h)==TINYRT_NO_SPACE);
 c=a;c.package_size=TINYRT_STORE_SLOT_SIZE+1;
 CHECK(tinyrt_store_begin(s,&c,&h)==TINYRT_INVALID_ARGUMENT);
 c=a;c.id.version=1;CHECK(tinyrt_store_begin(s,&c,&h)==TINYRT_CONFLICT);
 c=a;c.id.sha256[0]^=1;CHECK(tinyrt_store_begin(s,&c,&h)==TINYRT_CONFLICT);
 CHECK(nor.mutations==mutations);
 test_package_make(pkg,300,"a",3,1,&c);CHECK(tinyrt_store_begin(s,&c,&h)==TINYRT_OK);
 CHECK(tinyrt_store_write(h,1,pkg,1)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_store_write(h,0,pkg,100)==TINYRT_OK);
 CHECK(tinyrt_store_write(h,0,pkg,100)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_store_write(h,UINT32_MAX,pkg,100)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_store_commit(h)==TINYRT_INVALID_ARGUMENT);
 tinyrt_store_abort(h);tinyrt_app_info_t out;CHECK(tinyrt_store_query(s,&a.id,&out)==TINYRT_OK);
 tinyrt_store_close(s);
}
static void test_verify_failure_and_uninstall(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"app",1,300),v2,out;tinyrt_install_t *h=NULL;
 test_package_make(pkg,300,"app",2,3,&v2);pkg[200]^=1;
 CHECK(tinyrt_store_begin(s,&v2,&h)==TINYRT_OK);
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_OK);
 CHECK(tinyrt_store_commit(h)==TINYRT_VERIFY_FAILED);
 CHECK(tinyrt_store_query(s,&a.id,&out)==TINYRT_OK);tinyrt_store_abort(h);
 CHECK(tinyrt_store_uninstall(s,"app")==TINYRT_OK);CHECK(tinyrt_store_uninstall(s,"app")==TINYRT_NOT_FOUND);
 tinyrt_store_close(s);CHECK(open_store(&s)==TINYRT_OK);
 uint32_t count=99;CHECK(tinyrt_store_list(s,NULL,0,&count)==TINYRT_OK && count==0);
 (void)install_package(s,"new",1,300);tinyrt_store_close(s);tinyrt_store_abort(NULL);
}
static void test_maximum_package_and_generation(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"max",1,TINYRT_STORE_SLOT_SIZE),out;
 tinyrt_store_close(s);CHECK(open_store(&s)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&a.id,&out)==TINYRT_OK);tinyrt_store_close(s);
 fake_nor_init(&nor);a=fixture(0,"app",1);unsigned slot=0;directory(0,UINT64_MAX,&a,&slot,1);
 CHECK(open_store(&s)==TINYRT_OK);tinyrt_install_t *h=NULL;a.id.version=2;
 CHECK(tinyrt_store_begin(s,&a,&h)==TINYRT_CONFLICT);
 CHECK(tinyrt_store_uninstall(s,"app")==TINYRT_CONFLICT);CHECK(nor.mutations==0);tinyrt_store_close(s);
}


static uint8_t baseline[TINYRT_STORE_SIZE];
static tinyrt_app_info_t before_apps[2],after_apps[2];
static uint32_t before_count,after_count;
static unsigned fault_cases;
static void capture_apps(tinyrt_store_t *s,tinyrt_app_info_t *out,uint32_t *n) {
 CHECK(tinyrt_store_list(s,out,2,n)==TINYRT_OK);
}
static int apps_equal(const tinyrt_app_info_t *a,uint32_t n,const tinyrt_app_info_t *b,uint32_t m) {
 if(n!=m) return 0;
 for(uint32_t i=0;i<n;++i)
  if(strcmp(a[i].id.app_id,b[i].id.app_id)||a[i].id.version!=b[i].id.version||
     a[i].package_size!=b[i].package_size||memcmp(a[i].id.sha256,b[i].id.sha256,32)) return 0;
 return 1;
}
static tinyrt_status_t action(tinyrt_store_t *s,unsigned scenario) {
 if(scenario==3) return tinyrt_store_uninstall(s,"a");
 tinyrt_app_info_t a;tinyrt_install_t *h=NULL;
 const char *id=scenario==1?"b":scenario==4?"c":"a";
 test_package_make(pkg,513,id,scenario==2?2:1,31,&a);
 tinyrt_status_t r=tinyrt_store_begin(s,&a,&h);
 if(r==TINYRT_OK) r=tinyrt_store_write(h,0,pkg,111);
 if(r==TINYRT_OK) r=tinyrt_store_write(h,111,pkg+111,402);
 if(r==TINYRT_OK) r=tinyrt_store_commit(h);
 tinyrt_store_abort(h);return r;
}
static void test_power_cut_matrix(void) {
 for(unsigned scenario=0;scenario<5;++scenario) {
  fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
  if(scenario>0) (void)install_package(s,"a",1,300);
  if(scenario>1) (void)install_package(s,"b",1,400);
  if(scenario==4) CHECK(tinyrt_store_uninstall(s,"a")==TINYRT_OK);
  capture_apps(s,before_apps,&before_count);memcpy(baseline,nor.bytes,sizeof(baseline));
  fake_nor_reset_log(&nor);CHECK(action(s,scenario)==TINYRT_OK);
  capture_apps(s,after_apps,&after_count);
  unsigned operations=nor.mutations;fake_nor_op_t trace[128];CHECK(operations<128);
  memcpy(trace,nor.log,operations*sizeof(trace[0]));tinyrt_store_close(s);
  for(unsigned mode=0;mode<2;++mode) {
  for(unsigned op=1;op<=operations;++op) {
   uint32_t len=trace[op-1].len,points[5]={0,1,len/2,len?len-1:0,len};
   for(unsigned j=0;j<5;++j) {
    if(points[j]>len) continue;
    int repeated=0;for(unsigned k=0;k<j;++k) if(points[k]==points[j]) repeated=1;
    if(repeated) continue;
    memcpy(nor.bytes,baseline,sizeof(baseline));fake_nor_reset_log(&nor);
    CHECK(open_store(&s)==TINYRT_OK);nor.fail_at=op;nor.partial_bytes=points[j];nor.stay_on_after_failure=(int)mode;
    tinyrt_status_t result=action(s,scenario);CHECK(result!=TINYRT_OK);
    if(mode) {
     tinyrt_app_info_t live[2];uint32_t live_count=0;capture_apps(s,live,&live_count);
     CHECK(apps_equal(live,live_count,before_apps,before_count)||apps_equal(live,live_count,after_apps,after_count));
     /* Same object must safely recover and allow a retry after failed IO. */
     fake_nor_reset_log(&nor);result=action(s,scenario);
     CHECK(result==TINYRT_OK||result==TINYRT_ALREADY_INSTALLED||(scenario==3&&result==TINYRT_NOT_FOUND));
     capture_apps(s,live,&live_count);CHECK(apps_equal(live,live_count,after_apps,after_count));
    }
    tinyrt_store_close(s);fake_nor_reset_log(&nor);
    CHECK(open_store(&s)==TINYRT_OK);
    tinyrt_app_info_t actual[2];uint32_t count=0;capture_apps(s,actual,&count);
    CHECK(apps_equal(actual,count,before_apps,before_count)||apps_equal(actual,count,after_apps,after_count));
    tinyrt_store_close(s);++fault_cases;
   }
  }
  }
  printf("MATRIX scenario=%u operations=%u\n",scenario,operations);
 }
}
static void test_reported_error_after_commit(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"app",1,300),v2,out;tinyrt_install_t *h=NULL;
 test_package_make(pkg,300,"app",2,31,&v2);CHECK(tinyrt_store_begin(s,&v2,&h)==TINYRT_OK);
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_OK);
 fake_nor_reset_log(&nor);
 /* sync package, erase directory, program directory, sync, program marker */
 nor.fail_at=5;nor.partial_bytes=4;nor.stay_on_after_failure=1;
 CHECK(tinyrt_store_commit(h)==TINYRT_IO_ERROR);
 CHECK(tinyrt_store_query(s,&v2.id,&out)==TINYRT_OK);
 CHECK(tinyrt_store_query(s,&a.id,&out)==TINYRT_NOT_FOUND);
 tinyrt_store_abort(h);
 unsigned mutations=nor.mutations;
 CHECK(tinyrt_store_begin(s,&v2,&h)==TINYRT_ALREADY_INSTALLED && h==NULL);
 CHECK(nor.mutations==mutations);tinyrt_store_close(s);
}
static void test_recovery_read_failure_blocks_mutation(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 (void)install_package(s,"app",1,300);
 tinyrt_app_info_t v2,out;tinyrt_install_t *h=NULL;
 test_package_make(pkg,300,"app",2,31,&v2);CHECK(tinyrt_store_begin(s,&v2,&h)==TINYRT_OK);
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_OK);fake_nor_reset_log(&nor);
 nor.fail_at=5;nor.partial_bytes=4;nor.stay_on_after_failure=1;nor.fail_reads_on_failure=1;
 CHECK(tinyrt_store_commit(h)==TINYRT_IO_ERROR);tinyrt_store_abort(h);
 unsigned mutations=nor.mutations;
 CHECK(tinyrt_store_begin(s,&v2,&h)==TINYRT_IO_ERROR && h==NULL);
 CHECK(tinyrt_store_uninstall(s,"app")==TINYRT_IO_ERROR);CHECK(nor.mutations==mutations);
 nor.fail_read=0;
 CHECK(tinyrt_store_query(s,&v2.id,&out)==TINYRT_OK);tinyrt_store_close(s);
}
static void test_reference_io_error_not_fallback(void) {
 fake_nor_init(&nor);tinyrt_app_info_t a1=fixture(0,"app",1),a2=fixture(1,"app",2);unsigned slot=0;
 directory(0,1,&a1,&slot,1);slot=1;directory(1,2,&a2,&slot,1);
 nor.fail_read_offset=slot_off(1);
 tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_IO_ERROR && s==NULL);CHECK(nor.mutations==0);
}
static void test_stale_directory_is_not_snapshot(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 (void)install_package(s,"a",1,300);(void)install_package(s,"b",1,300);
 (void)install_package(s,"a",2,300);(void)install_package(s,"b",2,300);
 tinyrt_app_info_t a3;tinyrt_install_t *h=NULL;test_package_make(pkg,300,"a",3,31,&a3);
 CHECK(tinyrt_store_begin(s,&a3,&h)==TINYRT_OK);CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_OK);
 tinyrt_store_abort(h);tinyrt_store_close(s);
 /* Current generation is 5 in A; older B refers to the slot just reused by A3.
  * Only structural directory damage permits falling back. The reused identity
  * is quarantined; the other old-directory record remains independently usable. */
 nor.bytes[0]^=1;CHECK(open_store(&s)==TINYRT_OK && s!=NULL);
 tinyrt_app_info_t out[2];uint32_t count=0;
 CHECK(tinyrt_store_list(s,out,2,&count)==TINYRT_OK&&count==1);
 CHECK(!strcmp(out[0].id.app_id,"a")&&out[0].id.version==2);tinyrt_store_close(s);
}
static void test_initial_recovery_is_narrow(void) {
 fake_nor_init(&nor);nor.bytes[0]=0;tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_CORRUPT);
 fake_nor_init(&nor);nor.bytes[4096]=0;CHECK(open_store(&s)==TINYRT_CORRUPT);
 fake_nor_init(&nor);nor.bytes[slot_off(2)+100]=0;CHECK(open_store(&s)==TINYRT_CORRUPT);
 CHECK(nor.mutations==0);
}


static void test_directory_and_api_rejections(void) {
 tinyrt_store_t *s=NULL;tinyrt_install_t *h=NULL;
 fake_nor_init(&nor);tinyrt_app_info_t a[2]={fixture(0,"a",1),fixture(1,"b",1)};
 unsigned slots[2]={0,0};directory(0,1,a,slots,2);CHECK(open_store(&s)==TINYRT_CORRUPT);
 slots[1]=1;a[1].id=a[0].id;directory(0,1,a,slots,2);CHECK(open_store(&s)==TINYRT_CORRUPT);
 fake_nor_init(&nor);CHECK(open_store(&s)==TINYRT_OK);tinyrt_app_info_t bad={0};
 CHECK(tinyrt_store_begin(s,&bad,&h)==TINYRT_INVALID_ARGUMENT);
 memset(bad.id.app_id,'a',32);bad.id.version=1;bad.package_size=100;
 CHECK(tinyrt_store_begin(s,&bad,&h)==TINYRT_INVALID_ARGUMENT);
 memcpy(bad.id.app_id,"../UPPER",9);CHECK(tinyrt_store_begin(s,&bad,&h)==TINYRT_INVALID_ARGUMENT);
 CHECK(nor.mutations==0);tinyrt_store_close(s);
}
static void test_failed_write_is_terminal(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a;test_package_make(pkg,300,"a",1,31,&a);tinyrt_install_t *h=NULL;
 CHECK(tinyrt_store_begin(s,&a,&h)==TINYRT_OK);fake_nor_reset_log(&nor);
 nor.fail_at=1;nor.partial_bytes=100;nor.stay_on_after_failure=1;
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_IO_ERROR);unsigned mutations=nor.mutations;
 CHECK(tinyrt_store_write(h,0,pkg,300)==TINYRT_IO_ERROR);CHECK(tinyrt_store_commit(h)==TINYRT_IO_ERROR);
 CHECK(nor.mutations==mutations);tinyrt_store_abort(h);tinyrt_store_close(s);
}


static void test_query_aliases_input_output(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"app",1,300),expected=a;
 CHECK(tinyrt_store_query(s,&a.id,&a)==TINYRT_OK);
 CHECK(apps_equal(&a,1,&expected,1));tinyrt_store_close(s);
}

static void test_committed_read(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"reader",1,10003);uint8_t out[10003];
 unsigned mutations=nor.mutations;
 CHECK(tinyrt_store_read(s,&a.id,0,out,sizeof(out))==TINYRT_OK);
 CHECK(!memcmp(out,pkg,sizeof(out)));CHECK(nor.mutations==mutations);
 CHECK(tinyrt_store_read(s,&a.id,10000,out,3)==TINYRT_OK && !memcmp(out,pkg+10000,3));
 CHECK(tinyrt_store_read(s,&a.id,10000,out,4)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_store_read(s,&a.id,UINT32_MAX,out,2)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_store_read(s,&a.id,0,out,0)==TINYRT_INVALID_ARGUMENT);
 CHECK(tinyrt_store_read(s,&a.id,0,NULL,1)==TINYRT_INVALID_ARGUMENT);
 nor.fail_read=1;CHECK(tinyrt_store_read(s,&a.id,0,out,1)==TINYRT_IO_ERROR);nor.fail_read=0;
 tinyrt_store_close(s);
}
static void test_read_identity_and_install_exclusion(void) {
 fake_nor_init(&nor);tinyrt_store_t *s=NULL;CHECK(open_store(&s)==TINYRT_OK);
 tinyrt_app_info_t a=install_package(s,"reader",1,300),b;
 uint8_t out[300];tinyrt_package_id_t wrong=a.id;wrong.sha256[0]^=1;
 CHECK(tinyrt_store_read(s,&wrong,0,out,300)==TINYRT_NOT_FOUND);
 test_package_make(pkg,400,"other",1,8,&b);tinyrt_install_t *h=NULL;
 CHECK(tinyrt_store_begin(s,&b,&h)==TINYRT_OK);
 CHECK(tinyrt_store_read(s,&a.id,0,out,300)==TINYRT_BUSY);
 tinyrt_store_abort(h);CHECK(tinyrt_store_read(s,&a.id,0,out,300)==TINYRT_OK);
 (void)install_package(s,"reader",2,300);
 CHECK(tinyrt_store_read(s,&a.id,0,out,300)==TINYRT_NOT_FOUND);
 tinyrt_store_close(s);
}

int main(void) {
    RUN(test_quarantine_keeps_newest_directory); RUN(test_quarantine_does_not_rollback_version);
    RUN(test_resource_failure_is_not_quarantine);
    RUN(test_committed_read); RUN(test_read_identity_and_install_exclusion);
    RUN(test_blank_open); RUN(test_slot_bounds); RUN(test_program_requires_erase);
    RUN(test_directory_roundtrip); RUN(test_reject_torn_commit); RUN(test_invalid_referenced_package);
    RUN(test_equal_generation_conflict); RUN(test_initialization_recovery); RUN(test_read_error_not_fallback);
    RUN(test_install_update_reopen); RUN(test_idempotent_commit_and_busy);
    RUN(test_capacity_version_and_order); RUN(test_verify_failure_and_uninstall); RUN(test_maximum_package_and_generation);
    RUN(test_power_cut_matrix); RUN(test_reported_error_after_commit); RUN(test_recovery_read_failure_blocks_mutation);
    RUN(test_reference_io_error_not_fallback); RUN(test_stale_directory_is_not_snapshot); RUN(test_initial_recovery_is_narrow);
    RUN(test_directory_and_api_rejections); RUN(test_failed_write_is_terminal);
    RUN(test_query_aliases_input_output);
    printf("FAULT_CASES=%u\n",fault_cases);
    printf("PASS tests=%u checks=%u (TEST verifier, no cryptographic validation)\n", tests, checks);
    return 0;
}
