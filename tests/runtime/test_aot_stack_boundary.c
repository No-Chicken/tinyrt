/* Compile the actual runtime with an unavailable platform stack boundary. */
#include <stdint.h>
uint8_t *tinyrt_test_stack_boundary(void) { return NULL; }
#define os_thread_get_stack_boundary tinyrt_test_stack_boundary
#include "../../runtime/wasm/tinyrt_runtime.c"
#undef os_thread_get_stack_boundary
static tinyrt_status_t arm(void *c,void (*cancel)(void *),void *arg,uint32_t ms) {(void)c;(void)cancel;(void)arg;(void)ms;return TINYRT_OK;}
static void disarm(void *c) {(void)c;}
int main(void) {
    if(tinyrt_runtime_system_init()!=TINYRT_OK)return 2;
    uint8_t bytes[65536];char path[1024];snprintf(path,sizeof(path),"%s/frame_observed.aot",AOT_FIXTURE_DIR);
    FILE *f=fopen(path,"rb");if(!f)return 2;uint32_t n=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);
    tinyrt_package_aot_profile_t profile={true,false,5,"x86_64","generic",{1}};
    tinyrt_package_aot_metadata_t meta={5,7,"x86_64","generic",{1},{1},{1},{1},{1},{1},{1}};
    tinyrt_execution_guard_t guard={NULL,arm,disarm,75};tinyrt_runtime_aot_config_t config={&profile,&guard};
    tinyrt_runtime_t *r=NULL;tinyrt_runtime_host_t host={0};tinyrt_package_policy_t policy={1,15,2,100000};
    tinyrt_status_t status=tinyrt_runtime_create_aot(bytes,n,&policy,&host,&config,&meta,&r);
    int good=status==TINYRT_INVALID_ARGUMENT && !r;
    tinyrt_runtime_destroy(r);tinyrt_runtime_system_shutdown();
    printf("AOT_STACK_BOUNDARY missing_boundary_rejected=%d used=%zu\n",good,tinyrt_runtime_memory_used());
    return good && !tinyrt_runtime_memory_used()?0:1;
}
