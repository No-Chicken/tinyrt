#include "tinyrt_package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while(0)
static uint8_t data[TINYRT_STORE_SLOT_SIZE+512];
static uint32_t data_size,read_calls,fail_read,bad_reads,wasm_calls;
static tinyrt_status_t wasm_result;
static const uint32_t base=17;
static tinyrt_status_t read_data(void *ctx,uint32_t off,void *out,uint32_t n) {
    (void)ctx; ++read_calls;
    if(fail_read && read_calls==fail_read) return TINYRT_IO_ERROR;
    if(n>1024 || off<base || off-base>data_size || n>data_size-(off-base)) {
        ++bad_reads; return TINYRT_IO_ERROR;
    }
    memcpy(out,data+off-base,n); return TINYRT_OK;
}
/* Explicit test stub. The runtime module owns real structure/import validation. */
static tinyrt_status_t wasm_stub(void *ctx,const tinyrt_store_io_t *io,
    uint32_t off,uint32_t n,const tinyrt_package_policy_t *p) {
    (void)ctx; (void)io; ++wasm_calls;
    CHECK(off==base+256 && n==15 && p->permissions==15 && p->max_memory_pages==8);
    return wasm_result;
}
static size_t load(const char *dir,const char *name,void *out,size_t capacity) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"rb"); if(!f) {perror(path); exit(2);}
    size_t n=fread(out,1,capacity,f); CHECK(!ferror(f)); fclose(f); return n;
}
static void fixture(const char *dir,const char *name) {
    data_size=(uint32_t)load(dir,name,data,sizeof(data));
    read_calls=fail_read=bad_reads=wasm_calls=0; wasm_result=TINYRT_OK;
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    tinyrt_package_trusted_key_t key={.key_id=7};
    CHECK(load(argv[1],"public.bin",key.public_key,65)==65);
    tinyrt_package_verifier_t config={&key,1,wasm_stub,NULL};
    tinyrt_store_io_t io={.read=read_data};
    tinyrt_package_metadata_t metadata;
    fixture(argv[1],"valid.pkg");
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(!strcmp(metadata.app.id.app_id,"demo.app") && metadata.app.id.version==3);
    CHECK(metadata.app.package_size==2575 && metadata.wasm_offset==256 && metadata.wasm_size==15);
    CHECK(metadata.assets_offset==271 && metadata.assets_size==2304 && metadata.signing_key_id==7);
    CHECK(metadata.policy.abi_version==1 && metadata.policy.permissions==15 &&
          metadata.policy.max_memory_pages==8 && metadata.policy.instruction_budget==12345);
    CHECK(!strcmp(metadata.title,"\xe8\xae\xa1\xe6\x95\xb0\xe5\x99\xa8"));
    uint8_t expected[32]; CHECK(load(argv[1],"expected.sha256",expected,32)==32);
    CHECK(!memcmp(expected,metadata.app.id.sha256,32) && wasm_calls==1 && !bad_reads);
    tinyrt_app_info_t app;
    CHECK(tinyrt_package_verify(&config,&io,base,data_size,&app)==TINYRT_OK);
    CHECK(!memcmp(expected,app.id.sha256,32) && app.package_size==data_size);
    const char *valid_names[]={"no_assets","maximum","reference"};
    for(unsigned k=0;k<3;++k) {
        char file[64]; snprintf(file,sizeof(file),"%s.pkg",valid_names[k]); fixture(argv[1],file);
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
        CHECK(metadata.app.package_size==data_size && metadata.wasm_size==15 && !bad_reads);
        snprintf(file,sizeof(file),"%s.sha256",valid_names[k]); CHECK(load(argv[1],file,expected,32)==32);
        CHECK(!memcmp(metadata.app.id.sha256,expected,32));
    }
    fixture(argv[1],"valid.pkg");
    key.public_key[64]^=1;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    key.public_key[64]^=1;
    tinyrt_package_trusted_key_t duplicate[2]={key,key};
    config.trusted_keys=duplicate; config.trusted_key_count=2;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    config.trusted_keys=&key; config.trusted_key_count=1;
    const struct { const char *scope; tinyrt_status_t expected; } scopes[]={
        {NULL,TINYRT_OK},{"",TINYRT_OK},{"demo.",TINYRT_OK},
        {"demo.app",TINYRT_OK},{"demo",TINYRT_VERIFY_FAILED},
        {"dem",TINYRT_VERIFY_FAILED},{"demo.app.",TINYRT_VERIFY_FAILED},
        {"other.",TINYRT_VERIFY_FAILED},{"Demo.",TINYRT_INVALID_ARGUMENT},
        {"demo/",TINYRT_INVALID_ARGUMENT},
        {"abcdefghijklmnopqrstuvwxyz123456",TINYRT_INVALID_ARGUMENT}};
    for(size_t i=0;i<sizeof(scopes)/sizeof(scopes[0]);++i) {
        fixture(argv[1],"valid.pkg");key.app_id_prefix=scopes[i].scope;
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==scopes[i].expected);
        if(scopes[i].expected!=TINYRT_OK) CHECK(wasm_calls==0 && metadata.app.id.version==0);
    }
    tinyrt_package_trusted_key_t publishers[2]={key,{.key_id=8}};
    publishers[0].app_id_prefix="demo.";publishers[1].app_id_prefix="other.";
    CHECK(load(argv[1],"publisher2.bin",publishers[1].public_key,65)==65);
    config.trusted_keys=publishers;config.trusted_key_count=2;
    const char *namespaces[]={"publisher2_valid","publisher2_cross","namespace_boundary","namespace_empty"};
    for(unsigned i=0;i<4;++i) {
        char file[64];snprintf(file,sizeof(file),"%s.pkg",namespaces[i]);fixture(argv[1],file);
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==(i?TINYRT_VERIFY_FAILED:TINYRT_OK));
        if(i) CHECK(wasm_calls==0);
    }
    config.trusted_keys=&key;config.trusted_key_count=1;key.app_id_prefix=NULL;
    char reject[4096]; size_t names=load(argv[1],"reject.txt",reject,sizeof(reject)-1); reject[names]=0;
    char *name=strtok(reject,"\r\n");
    while(name) {
        char file[100]; snprintf(file,sizeof(file),"%s.pkg",name); fixture(argv[1],file);
        memset(&metadata,0xa5,sizeof(metadata));
        tinyrt_status_t r=tinyrt_package_inspect(&config,&io,base,data_size,&metadata);
        if(r!=TINYRT_VERIFY_FAILED) fprintf(stderr,"CASE %s status=%d\n",name,r);
        CHECK(r==TINYRT_VERIFY_FAILED);
        tinyrt_package_metadata_t zero={0}; CHECK(!memcmp(&metadata,&zero,sizeof(zero)));
        CHECK(wasm_calls==0 && bad_reads==0);
        name=strtok(NULL,"\r\n");
    }
    fixture(argv[1],"valid.pkg");
    for(uint32_t index=1;index<=4;++index) {
        fail_read=index; read_calls=0; wasm_calls=0;
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_IO_ERROR);
        CHECK(wasm_calls==0);
    }
    fail_read=0; read_calls=0; wasm_result=TINYRT_VERIFY_FAILED;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    wasm_result=TINYRT_IO_ERROR;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_IO_ERROR);
    wasm_result=TINYRT_NO_MEMORY;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_NO_MEMORY);
    wasm_result=TINYRT_BUSY;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_BUSY);
    wasm_result=TINYRT_INVALID_ARGUMENT;
    memset(&metadata,0xa5,sizeof(metadata));
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    tinyrt_package_metadata_t empty_metadata={0};
    CHECK(!memcmp(&metadata,&empty_metadata,sizeof(metadata)));
    memset(&app,0xa5,sizeof(app));
    CHECK(tinyrt_package_verify(&config,&io,base,data_size,&app)==TINYRT_INVALID_ARGUMENT);
    tinyrt_app_info_t empty_app={0};CHECK(!memcmp(&app,&empty_app,sizeof(app)));
    config.validate_wasm=NULL;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    config.validate_wasm=wasm_stub; config.trusted_key_count=0;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    config.trusted_key_count=1;
    uint32_t reads_before=read_calls;
    CHECK(tinyrt_package_inspect(&config,&io,UINT32_MAX-20,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(read_calls==reads_before);
    CHECK(tinyrt_package_inspect(&config,&io,base,TINYRT_STORE_SLOT_SIZE+1,&metadata)==TINYRT_VERIFY_FAILED);
    CHECK(read_calls==reads_before);
    CHECK(tinyrt_package_inspect(NULL,&io,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_package_inspect(&config,NULL,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,NULL)==TINYRT_INVALID_ARGUMENT);
    printf("PACKAGE checks=%u failures=%u (real SHA256/P256; explicit Wasm stub)\n",checks,failures);
    return failures?1:0;
}
