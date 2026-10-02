#include "tinyrt_runtime.h"
#include "wasm_export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,reserves,releases,fail_at,accepted;
static size_t mapped;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);} } while(0)
static bool reserve(void *ctx,uint32_t size) {
    (void)ctx;++reserves;if(fail_at && reserves>=fail_at)return false;++accepted;mapped+=size;return true;
}
static void release(void *ctx,uint32_t size) {(void)ctx;++releases;CHECK(mapped>=size);mapped-=size;}
static uint8_t source[262144],bytes[262144];static uint32_t size;
static wasm_module_t load(void) {char error[192]={0};memcpy(bytes,source,size);return wasm_runtime_load(bytes,size,error,sizeof(error));}
int main(void) {
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    char path[1024];snprintf(path,sizeof(path),"%s/frame_observed.aot",AOT_FIXTURE_DIR);
    FILE *f=fopen(path,"rb");if(!f)return 2;size=(uint32_t)fread(source,1,sizeof(source),f);fclose(f);
    size_t base=tinyrt_runtime_memory_used();
#ifndef TINYRT_TEST_OMIT_MAPPING_HOOK
    wasm_runtime_set_aot_mapping_budget(reserve,release,NULL);
#else
    (void)reserve;(void)release;
#endif
    wasm_module_t module=load();CHECK(module!=NULL && reserves>=1 && mapped>0);
    unsigned count=reserves;if(module)wasm_runtime_unload(module);
    CHECK(releases==reserves && !mapped && tinyrt_runtime_memory_used()==base);
    /* Deny every actual acquisition point. Earlier successful mappings must
     * be released when a later acquisition or relocation cannot complete. */
    if(!count)count=4;
    for(unsigned i=1;i<=count;++i) {
        reserves=releases=accepted=0;fail_at=i;
        module=load();CHECK(reserves>=i && accepted==i-1);
        if(i==1)CHECK(module==NULL);
        if(module)wasm_runtime_unload(module);
        CHECK(releases==accepted && mapped==0 && tinyrt_runtime_memory_used()==base);
    }
    fail_at=0;
    for(unsigned i=0;i<50;++i) {
        reserves=releases=0;module=load();CHECK(module!=NULL);
        if(module)wasm_runtime_unload(module);
        CHECK(reserves==count && releases==count && !mapped && tinyrt_runtime_memory_used()==base);
    }
    wasm_runtime_set_aot_mapping_budget(NULL,NULL,NULL);
    tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);
    printf("AOT_MAPPING checks=%u failures=%u regions=%u\n",checks,failures,count);return failures?1:0;
}
