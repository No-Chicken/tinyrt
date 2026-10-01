#include "tinyrt_transport.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static void put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
void tinyrt_mgmt_reset(tinyrt_mgmt_rx_t *r) { if(r)memset(r,0,sizeof(*r)); }
int tinyrt_mgmt_feed(tinyrt_mgmt_rx_t *r,const uint8_t *p,uint16_t n) {
    if(!r)return -1;
    if(!p || n<10 || n>20 || p[0]!=0xa7 || p[1]!=1 || p[3]>1)goto invalid;
    uint16_t id=u16(p+4),total=u16(p+6),off=u16(p+8),size=(uint16_t)(n-10);
    if(!id || total>256 || off>total || size>total-off || (!size && total))goto invalid;
    if(!r->active) {
        if(off)goto invalid;
        r->request_id=id;r->total=total;r->received=0;r->opcode=p[2];r->response=p[3];r->active=1;
    } else if(r->request_id!=id || r->total!=total || r->opcode!=p[2] || r->response!=p[3])goto invalid;
    if(off!=r->received)goto invalid;
    if(size)memcpy(r->payload+off,p+10,size);
    r->received=(uint16_t)(off+size);
    if(r->received==total) { r->active=0;return 0; }
    return 1;
invalid: tinyrt_mgmt_reset(r);return -1;
}
int tinyrt_mgmt_emit(uint8_t op,uint16_t id,uint8_t response,const uint8_t *p,uint16_t n,tinyrt_mgmt_emit_fn emit,void *ctx) {
    if(!id || response>1 || n>256 || (!p && n) || !emit)return -1;
    uint16_t off=0;
    do {
        uint8_t frame[20]={0xa7,1,op,response};
        uint16_t size=(uint16_t)(n-off);if(size>10)size=10;
        put16(frame+4,id);put16(frame+6,n);put16(frame+8,off);
        if(size)memcpy(frame+10,p+off,size);
        int rc=emit(ctx,frame,(uint16_t)(10+size));if(rc)return rc;
        off=(uint16_t)(off+size);
    } while(off<n);
    return 0;
}
