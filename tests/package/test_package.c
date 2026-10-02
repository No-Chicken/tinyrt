#include "tinyrt_package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while(0)
static uint8_t data[TINYRT_STORE_MAX_PACKAGE_SIZE+512];
static uint32_t data_size,read_calls,fail_read,bad_reads,wasm_calls,expected_wasm_offset=288;
static tinyrt_status_t wasm_result;
static uint32_t aot_calls,expected_aot_offset=576;
static tinyrt_status_t aot_result=TINYRT_OK;
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
    CHECK(off==base+expected_wasm_offset && n==15 && p->permissions==15 && p->max_memory_pages==8);
    return wasm_result;
}
static tinyrt_status_t aot_stub(void *ctx,const tinyrt_store_io_t *io,uint32_t off,uint32_t n,
    const tinyrt_package_policy_t *p,const tinyrt_package_aot_metadata_t *aot) {
    (void)ctx;(void)io;++aot_calls;
    CHECK(off==base+expected_aot_offset && n==29 && p->permissions==15);
    CHECK(aot->format_version==5 && aot->safety_flags==7 && !strcmp(aot->target_cpu,"esp32s3"));
    return aot_result;
}
static size_t load(const char *dir,const char *name,void *out,size_t capacity) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"rb"); if(!f) {perror(path); exit(2);}
    size_t n=fread(out,1,capacity,f); CHECK(!ferror(f)); fclose(f); return n;
}
static void fixture(const char *dir,const char *name) {
    data_size=(uint32_t)load(dir,name,data,sizeof(data));
    read_calls=fail_read=bad_reads=wasm_calls=aot_calls=0; wasm_result=aot_result=TINYRT_OK;
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    tinyrt_package_trusted_key_t key={.key_id=7};
    CHECK(load(argv[1],"public.bin",key.public_key,65)==65);
    tinyrt_package_verifier_t config={&key,1,wasm_stub,NULL};
    tinyrt_store_io_t io={.read=read_data};
    tinyrt_package_metadata_t metadata;
    fixture(argv[1],"valid.pkg");expected_wasm_offset=288;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(!strcmp(metadata.app.id.app_id,"demo.app") && metadata.app.id.version==3);
    CHECK(metadata.app.package_size==2608 && metadata.wasm_offset==288 && metadata.wasm_size==15);
    CHECK(metadata.assets_offset==304 && metadata.assets_size==2304 && metadata.signing_key_id==7);
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
        char file[64]; snprintf(file,sizeof(file),"%s.pkg",valid_names[k]); fixture(argv[1],file);expected_wasm_offset=k==0?272:288;
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
        CHECK(metadata.app.package_size==data_size && metadata.wasm_size==15 && !bad_reads);
        if(k==1)CHECK(data_size==0x200000);
        snprintf(file,sizeof(file),"%s.sha256",valid_names[k]); CHECK(load(argv[1],file,expected,32)==32);
        CHECK(!memcmp(metadata.app.id.sha256,expected,32));
    }
    fixture(argv[1],"valid.pkg");expected_wasm_offset=288;
    fixture(argv[1],"section_cover.pkg");expected_wasm_offset=304;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(metadata.assets_offset==320 && metadata.assets_size==9);
    CHECK(metadata.cover.offset==332 && metadata.cover.size==133232 && metadata.cover.codec==1);
    CHECK(load(argv[1],"cover.sha256",expected,32)==32);
    CHECK(!memcmp(metadata.cover.sha256,expected,32) && !bad_reads);
    fixture(argv[1],"valid.pkg");expected_wasm_offset=288;
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
        fixture(argv[1],"valid.pkg");expected_wasm_offset=288;key.app_id_prefix=scopes[i].scope;
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
    fixture(argv[1],"valid.pkg");expected_wasm_offset=288;
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
    read_calls=0;fail_read=1;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(read_calls==0); /* Legacy invalid host config fails before touching IO. */
    fail_read=0;
    config.validate_wasm=wasm_stub; config.trusted_key_count=0;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    config.trusted_key_count=1;
    uint32_t reads_before=read_calls;
    CHECK(tinyrt_package_inspect(&config,&io,UINT32_MAX-20,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(read_calls==reads_before);
    CHECK(tinyrt_package_inspect(&config,&io,base,TINYRT_STORE_MAX_PACKAGE_SIZE+1,&metadata)==TINYRT_VERIFY_FAILED);
    CHECK(read_calls==reads_before);
    CHECK(tinyrt_package_inspect(NULL,&io,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_package_inspect(&config,NULL,base,data_size,&metadata)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,NULL)==TINYRT_INVALID_ARGUMENT);
    fixture(argv[1],"section_wasm.pkg");expected_wasm_offset=272;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(metadata.wasm_offset==272 && metadata.wasm_size==15 && wasm_calls==1);
    fixture(argv[1],"section_wasm_assets.pkg");expected_wasm_offset=288;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(metadata.assets_offset==304 && metadata.assets_size==9 && metadata.format_version==1);
    fixture(argv[1],"section_maximum.pkg");
    CHECK(data_size==0x200000 && tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    fixture(argv[1],"section_aot.pkg");expected_wasm_offset=304;
    tinyrt_package_aot_profile_t profile={.enabled=true,.format_version=5,.target_arch="xtensa",.target_cpu="esp32s3"};
    memset(profile.compat_id,4,sizeof(profile.compat_id));
    config.aot_profile=&profile;config.validate_aot=aot_stub;
    key.aot_authority=TINYRT_AOT_AUTHORITY_RELEASE;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(metadata.execution_kind==TINYRT_PACKAGE_EXEC_AOT && metadata.aot_offset==576 && aot_calls==1 && wasm_calls==1);
    CHECK(metadata.aot.compiler_sha256[0]==6 && !bad_reads);
    for(unsigned i=0;i<3;++i) {
        fixture(argv[1],"section_aot.pkg");
        if(i==0) profile.enabled=false;
        else if(i==1) strcpy(profile.target_cpu,"different");
        else profile.compat_id[0]^=1;
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
        CHECK(metadata.execution_kind==TINYRT_PACKAGE_EXEC_WASM && (unsigned)metadata.fallback_reason==i+1 && !aot_calls && wasm_calls==1);
        profile.enabled=true;memset(profile.target_cpu,0,sizeof(profile.target_cpu));
        strcpy(profile.target_cpu,"esp32s3");memset(profile.compat_id,4,sizeof(profile.compat_id));
    }
    fixture(argv[1],"section_aot.pkg");key.aot_authority=TINYRT_AOT_AUTHORITY_NONE;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED && !wasm_calls && !aot_calls);
    key.aot_authority=TINYRT_AOT_AUTHORITY_DEVELOPMENT;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    profile.allow_development=true;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    fixture(argv[1],"section_outside_demo.pkg");
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED && !wasm_calls && !aot_calls);
    key.aot_authority=TINYRT_AOT_AUTHORITY_RELEASE;profile.allow_development=false;
    fixture(argv[1],"section_aot_only.pkg");expected_aot_offset=528;config.validate_wasm=NULL;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    CHECK(metadata.wasm_size==0 && metadata.execution_kind==TINYRT_PACKAGE_EXEC_AOT && aot_calls==1 && !wasm_calls);
    profile.enabled=false;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    profile.enabled=true;config.validate_wasm=wasm_stub;expected_aot_offset=576;
    publishers[0]=key;publishers[1].aot_authority=TINYRT_AOT_AUTHORITY_RELEASE;publishers[1].app_id_prefix=NULL;
    config.trusted_keys=publishers;config.trusted_key_count=2;
    fixture(argv[1],"section_previous_key.pkg");
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK && metadata.signing_key_id==8);
    config.trusted_keys=&key;config.trusted_key_count=1;
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_VERIFY_FAILED);
    names=load(argv[1],"section_reject.txt",reject,sizeof(reject)-1);reject[names]=0;name=strtok(reject,"\r\n");
    while(name) {
        char file[100];snprintf(file,sizeof(file),"%s.pkg",name);fixture(argv[1],file);
        tinyrt_status_t status=tinyrt_package_inspect(&config,&io,base,data_size,&metadata);
        if(status!=TINYRT_VERIFY_FAILED) fprintf(stderr,"CASE %s status=%d\n",name,status);
        CHECK(status==TINYRT_VERIFY_FAILED && !bad_reads && !wasm_calls && !aot_calls);
        CHECK(!memcmp(&metadata,&empty_metadata,sizeof(metadata)));
        name=strtok(NULL,"\r\n");
    }
    fixture(argv[1],"section_aot.pkg");
    CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_OK);
    uint32_t successful_reads=read_calls;
    for(uint32_t i=1;i<=successful_reads;++i) {
        fixture(argv[1],"section_aot.pkg");fail_read=i;
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==TINYRT_IO_ERROR);
        CHECK(!bad_reads && !wasm_calls && !aot_calls);
    }
    const tinyrt_status_t aot_errors[]={TINYRT_VERIFY_FAILED,TINYRT_IO_ERROR,TINYRT_NO_MEMORY,TINYRT_BUSY,TINYRT_INVALID_ARGUMENT};
    for(unsigned i=0;i<sizeof(aot_errors)/sizeof(aot_errors[0]);++i) {
        fixture(argv[1],"section_aot.pkg");aot_result=aot_errors[i];
        CHECK(tinyrt_package_inspect(&config,&io,base,data_size,&metadata)==aot_errors[i]);
        CHECK(!memcmp(&metadata,&empty_metadata,sizeof(metadata)));
    }
    printf("PACKAGE checks=%u failures=%u (real SHA256/P256; explicit Wasm stub)\n",checks,failures);
    return failures?1:0;
}
