/* Explicit component boundaries for manager tests. Actual cryptographic and
 * WAMR tests live in package/ and runtime/, never use these in a product. */
#include "tinyrt_runtime.h"
#include "test_package_verifier.h"
#include <stdlib.h>
#include <string.h>
unsigned runtime_destroy_count;
int runtime_fail_event;
int runtime_stop_write;
int runtime_skip_write;
int runtime_skip_frame;
int metadata_swap;
int metadata_aot;
int metadata_cover;
int metadata_audio;
int runtime_audio;
int runtime_resource;
uint8_t runtime_resource_bytes[4096];
unsigned runtime_aot_creates;
static int runtime_live;
struct tinyrt_runtime {tinyrt_runtime_host_t host;};
tinyrt_status_t tinyrt_package_verify(void *c,const tinyrt_store_io_t *io,uint32_t o,uint32_t n,tinyrt_app_info_t *a) {
 (void)c;return test_package_verify(NULL,io,o,n,a);
}
tinyrt_status_t tinyrt_package_inspect(const tinyrt_package_verifier_t *v,const tinyrt_store_io_t *io,uint32_t o,uint32_t n,tinyrt_package_metadata_t *m) {
 if(runtime_live)return TINYRT_BUSY;
 (void)v;memset(m,0,sizeof(*m));tinyrt_status_t r=test_package_verify(NULL,io,o,n,&m->app);
 if(metadata_cover){m->cover.offset=44;m->cover.size=n-44;m->cover.codec=1;memset(m->cover.sha256,13,32);}
 m->wasm_offset=0;m->wasm_size=n;m->policy=(tinyrt_package_policy_t){1,15,1,1000};
 if(metadata_audio){m->assets_offset=44;m->assets_size=n-44;m->policy.permissions=31;}
 m->execution_kind=metadata_aot?TINYRT_PACKAGE_EXEC_AOT:TINYRT_PACKAGE_EXEC_WASM;
 m->aot_offset=44;m->aot_size=n-44;
 memcpy(m->title,"Counter",8);if(metadata_swap) memcpy(m->app.id.app_id,"foreign",8);return r;
}
tinyrt_status_t tinyrt_runtime_create(const void *b,uint32_t n,const tinyrt_package_policy_t *p,const tinyrt_runtime_host_t *h,tinyrt_runtime_t **out) {
 (void)b;(void)n;(void)p;*out=calloc(1,sizeof(**out));if(!*out) return TINYRT_NO_MEMORY;(*out)->host=*h;runtime_live=1;return TINYRT_OK;
}
tinyrt_status_t tinyrt_runtime_create_aot(const void *b,uint32_t n,const tinyrt_package_policy_t *p,const tinyrt_runtime_host_t *h,
 const tinyrt_runtime_aot_config_t *config,const tinyrt_package_aot_metadata_t *meta,tinyrt_runtime_t **out) {
 (void)meta;*out=NULL;
 if(!config || !config->profile || !config->profile->enabled || !config->guard || !config->guard->arm)return TINYRT_INVALID_ARGUMENT;
 if(n!=356 || ((const unsigned char *)b)[0]!=13)return TINYRT_VERIFY_FAILED;
 ++runtime_aot_creates;return tinyrt_runtime_create(b,n,p,h,out);
}
tinyrt_status_t tinyrt_runtime_init(tinyrt_runtime_t *r,int32_t w,int32_t h) {(void)r;(void)w;(void)h;return TINYRT_OK;}
tinyrt_status_t tinyrt_runtime_set_execution_guard(tinyrt_runtime_t *r,const tinyrt_execution_guard_t *guard) {(void)r;(void)guard;return TINYRT_OK;}
tinyrt_status_t tinyrt_runtime_stop(tinyrt_runtime_t *r) {
 if(runtime_stop_write)return r->host.kv_set(r->host.ctx,0,77);
 return TINYRT_OK;
}
tinyrt_status_t tinyrt_runtime_event(tinyrt_runtime_t *r,int32_t kind,int32_t x,int32_t y,int32_t arg) {
 (void)kind;(void)x;(void)y;(void)arg;
 if(runtime_resource)return r->host.asset_read(r->host.ctx,(uint32_t)x,runtime_resource_bytes,(uint32_t)y);
 if(runtime_audio)return r->host.audio_play(r->host.ctx,(uint32_t)x,(uint32_t)y,(uint32_t)arg);
 if(runtime_skip_write)return TINYRT_OK;
 int32_t old=r->host.kv_get(r->host.ctx,0,0);
 tinyrt_status_t s=r->host.kv_set(r->host.ctx,0,old+1);
 return runtime_fail_event?TINYRT_VERIFY_FAILED:s;
}
tinyrt_status_t tinyrt_runtime_render(tinyrt_runtime_t *r,tinyrt_frame_t *out) {
 if(runtime_skip_frame){out->count=0;return TINYRT_OK;}
 out->count=1;out->pixel_bytes=0;memset(&out->commands[0],0,sizeof(out->commands[0]));out->commands[0].kind=TINYRT_DRAW_CLEAR;
 out->commands[0].rgb=(uint32_t)r->host.kv_get(r->host.ctx,0,0);return TINYRT_OK;
}
uint32_t tinyrt_runtime_clock_interval_ms(const tinyrt_runtime_t *r) {return r?17:100;}
uint32_t tinyrt_runtime_input_events(const tinyrt_runtime_t *r) {(void)r;return 0;}
size_t tinyrt_runtime_memory_used(void) {return runtime_live?256:0;}
size_t tinyrt_runtime_memory_peak(void) {return 256;}
const char *tinyrt_runtime_last_error(const tinyrt_runtime_t *r) {(void)r;return "test guest failure";}
void tinyrt_runtime_destroy(tinyrt_runtime_t *r) {if(r){runtime_destroy_count++;runtime_live=0;free(r);}}

int cache_fail_alloc;
unsigned cache_live_allocations;
static void *tracked_cache;
void *manager_test_malloc(size_t size) {
 if(size==262144 && cache_fail_alloc)return NULL;
 void *p=malloc(size);
 if(size==262144 && p){tracked_cache=p;++cache_live_allocations;}
 return p;
}
void manager_test_free(void *p) {
 if(p && p==tracked_cache){tracked_cache=NULL;--cache_live_allocations;}
 free(p);
}
