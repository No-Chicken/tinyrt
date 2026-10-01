/* Explicit component boundaries for manager tests. Actual cryptographic and
 * WAMR tests live in package/ and runtime/, never use these in a product. */
#include "tinyrt_runtime.h"
#include "test_package_verifier.h"
#include <stdlib.h>
#include <string.h>
unsigned runtime_destroy_count;
int runtime_fail_event;
int metadata_swap;
static int runtime_live;
struct tinyrt_runtime {tinyrt_runtime_host_t host;};
tinyrt_status_t tinyrt_package_verify(void *c,const tinyrt_store_io_t *io,uint32_t o,uint32_t n,tinyrt_app_info_t *a) {
 (void)c;return test_package_verify(NULL,io,o,n,a);
}
tinyrt_status_t tinyrt_package_inspect(const tinyrt_package_verifier_t *v,const tinyrt_store_io_t *io,uint32_t o,uint32_t n,tinyrt_package_metadata_t *m) {
 if(runtime_live)return TINYRT_BUSY;
 (void)v;memset(m,0,sizeof(*m));tinyrt_status_t r=test_package_verify(NULL,io,o,n,&m->app);
 m->wasm_offset=0;m->wasm_size=n;m->policy=(tinyrt_package_policy_t){1,15,1,1000};
 memcpy(m->title,"Counter",8);if(metadata_swap) memcpy(m->app.id.app_id,"foreign",8);return r;
}
tinyrt_status_t tinyrt_runtime_create(const void *b,uint32_t n,const tinyrt_package_policy_t *p,const tinyrt_runtime_host_t *h,tinyrt_runtime_t **out) {
 (void)b;(void)n;(void)p;*out=calloc(1,sizeof(**out));if(!*out) return TINYRT_NO_MEMORY;(*out)->host=*h;runtime_live=1;return TINYRT_OK;
}
tinyrt_status_t tinyrt_runtime_init(tinyrt_runtime_t *r,int32_t w,int32_t h) {(void)r;(void)w;(void)h;return TINYRT_OK;}
tinyrt_status_t tinyrt_runtime_event(tinyrt_runtime_t *r,int32_t kind,int32_t x,int32_t y,int32_t arg) {
 (void)kind;(void)x;(void)y;(void)arg;
 int32_t old=r->host.kv_get(r->host.ctx,0,0);
 tinyrt_status_t s=r->host.kv_set(r->host.ctx,0,old+1);
 return runtime_fail_event?TINYRT_VERIFY_FAILED:s;
}
tinyrt_status_t tinyrt_runtime_render(tinyrt_runtime_t *r,tinyrt_frame_t *out) {
 memset(out,0,sizeof(*out));out->count=1;out->commands[0].kind=TINYRT_DRAW_CLEAR;
 out->commands[0].rgb=(uint32_t)r->host.kv_get(r->host.ctx,0,0);return TINYRT_OK;
}
const char *tinyrt_runtime_last_error(const tinyrt_runtime_t *r) {(void)r;return "test guest failure";}
void tinyrt_runtime_destroy(tinyrt_runtime_t *r) {if(r){runtime_destroy_count++;runtime_live=0;free(r);}}
