#include "fake_nor.h"
#include <string.h>
static int bounds(uint32_t o,uint32_t n) {return o<=TINYRT_STORE_SIZE && n<=TINYRT_STORE_SIZE-o;}
static tinyrt_status_t rd(void *c,uint32_t o,void *d,uint32_t n) {
 fake_nor_t *f=c; if(!d || !bounds(o,n)) return TINYRT_INVALID_ARGUMENT;
 if(f->powered_off || f->fail_read || (f->fail_read_offset && o==f->fail_read_offset)) return TINYRT_IO_ERROR;
 ++f->reads;memcpy(d,f->bytes+o,n);return TINYRT_OK;
}
static tinyrt_status_t mut(fake_nor_t *f,char k,uint32_t o,const void *d,uint32_t n) {
 if(!bounds(o,n)) return TINYRT_INVALID_ARGUMENT;
 if(f->powered_off || f->mutations>=16384) return TINYRT_IO_ERROR;
 if(k=='P') {const uint8_t *b=d;for(uint32_t i=0;i<n;++i) if((f->bytes[o+i]&b[i])!=b[i]) return TINYRT_IO_ERROR;}
 f->log[f->mutations]=(fake_nor_op_t){k,o,n};++f->mutations;
 int fail=f->fail_at && f->mutations==f->fail_at;
 uint32_t take=fail && f->partial_bytes<n ? f->partial_bytes:n;
 if(k=='P') memcpy(f->bytes+o,d,take);
 if(k=='E') memset(f->bytes+o,255,take);
 if(fail) {f->powered_off=!f->stay_on_after_failure;f->fail_read=f->fail_reads_on_failure;return TINYRT_IO_ERROR;} return TINYRT_OK;
}
static tinyrt_status_t wr(void *c,uint32_t o,const void *d,uint32_t n) {
 if(!d || !n) return TINYRT_INVALID_ARGUMENT;return mut(c,'P',o,d,n);
}
static tinyrt_status_t er(void *c,uint32_t o,uint32_t n) {
 if(!n || o%4096 || n%4096) return TINYRT_INVALID_ARGUMENT;return mut(c,'E',o,0,n);
}
static tinyrt_status_t sy(void *c) {return mut(c,'S',0,0,0);}
void fake_nor_init(fake_nor_t *f) {memset(f,0,sizeof(*f));memset(f->bytes,255,sizeof(f->bytes));}
void fake_nor_reset_log(fake_nor_t *f) {f->mutations=0;f->reads=0;f->fail_at=0;f->powered_off=0;f->fail_read=0;f->stay_on_after_failure=0;f->fail_reads_on_failure=0;f->fail_read_offset=0;}
tinyrt_store_io_t fake_nor_io(fake_nor_t *f) {tinyrt_store_io_t io={f,rd,wr,er,sy};return io;}
