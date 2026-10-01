#include "test_package_verifier.h"
#include <string.h>
static uint32_t get32(const uint8_t *b) {return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put32(uint8_t *b,uint32_t v) {for(unsigned i=0;i<4;++i) b[i]=(uint8_t)(v>>(i*8));}
static uint32_t fold(uint32_t h,const uint8_t *b,uint32_t n) {for(uint32_t i=0;i<n;++i) h=(h^b[i])*UINT32_C(16777619);return h;}
static void info(const uint8_t *b,uint32_t n,uint32_t hash,tinyrt_app_info_t *out) {
 memset(out,0,sizeof(*out));memcpy(out->id.app_id,b+8,32);out->id.version=get32(b+40);out->package_size=n;
 for(unsigned i=0;i<8;++i) put32(out->id.sha256+4*i,hash^(UINT32_C(0x9e3779b9)*i));
}
void test_package_make(uint8_t *b,uint32_t n,const char *id,uint32_t ver,uint8_t seed,tinyrt_app_info_t *out) {
 memset(b,0,n);memcpy(b,"TESTPKG1",8);memcpy(b+8,id,strlen(id));put32(b+40,ver);b[44]=seed;
 for(uint32_t i=64;i<n;++i) b[i]=(uint8_t)(seed+i*7u);
 info(b,n,fold(UINT32_C(2166136261),b,n),out);
}
tinyrt_status_t test_package_verify(void *c,const tinyrt_store_io_t *io,uint32_t o,uint32_t n,tinyrt_app_info_t *out) {
 (void)c;uint8_t h[64],b[256];if(n<64) return TINYRT_VERIFY_FAILED;
 tinyrt_status_t r=io->read(io->ctx,o,h,64);if(r!=TINYRT_OK) return r;
 if(memcmp(h,"TESTPKG1",8) || !memchr(h+8,0,32)) return TINYRT_VERIFY_FAILED;
 uint32_t hash=fold(UINT32_C(2166136261),h,64);
 for(uint32_t pos=64;pos<n;) {
  uint32_t take=n-pos;if(take>sizeof(b)) take=sizeof(b);
  r=io->read(io->ctx,o+pos,b,take);if(r!=TINYRT_OK) return r;
  for(uint32_t i=0;i<take;++i) if(b[i]!=(uint8_t)(h[44]+(pos+i)*7u)) return TINYRT_VERIFY_FAILED;
  hash=fold(hash,b,take);pos+=take;
 }
 info(h,n,hash,out);return TINYRT_OK;
}
