#include "tinyrt_transport.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static tinyrt_mgmt_rx_t decoded;
static unsigned frames;
static int receive(void *ctx,const uint8_t *p,uint16_t n) {
    (void)ctx; CHECK(n<=20); ++frames;
    int rc=tinyrt_mgmt_feed(&decoded,p,n);
    CHECK(rc==(decoded.received==decoded.total?0:1)); return 0;
}
static int refuse(void *ctx,const uint8_t *p,uint16_t n) { (void)ctx;(void)p;(void)n;return -7; }
int main(void) {
    tinyrt_mgmt_rx_t r={0};
    const uint8_t hello[]={0xa7,1,0x10,0,0x34,0x12,0,0,0,0};
    CHECK(tinyrt_mgmt_feed(&r,hello,sizeof hello)==0);
    CHECK(r.request_id==0x1234 && r.opcode==0x10 && r.total==0 && !r.active);
    uint8_t frame[]={0xa7,1,0x13,0,2,0,12,0,0,0,1,2,3,4,5,6,7,8,9,10};
    CHECK(tinyrt_mgmt_feed(&r,frame,sizeof frame)==1);
    CHECK(r.received==10);
    const uint8_t tail[]={0xa7,1,0x13,0,2,0,12,0,10,0,11,12};
    CHECK(tinyrt_mgmt_feed(&r,tail,sizeof tail)==0);
    CHECK(r.received==12 && r.payload[11]==12);
    CHECK(tinyrt_mgmt_feed(&r,tail,sizeof tail)==-1); /* duplicate final cannot dispatch */
    CHECK(tinyrt_mgmt_feed(&r,frame,sizeof frame)==1);
    uint8_t bad[20]; memcpy(bad,tail,sizeof tail); bad[4]=3;
    CHECK(tinyrt_mgmt_feed(&r,bad,sizeof tail)==-1 && !r.active);
    CHECK(tinyrt_mgmt_feed(&r,tail,sizeof tail)==-1); /* cannot append after failure */
    memcpy(bad,frame,sizeof frame); bad[7]=1; /* total=268 exceeds 256 */
    CHECK(tinyrt_mgmt_feed(&r,bad,sizeof bad)==-1);
    memcpy(bad,frame,sizeof frame); bad[1]=2;
    CHECK(tinyrt_mgmt_feed(&r,bad,sizeof bad)==-1);
    memcpy(bad,frame,sizeof frame); bad[3]=2;
    CHECK(tinyrt_mgmt_feed(&r,bad,sizeof bad)==-1);
    memcpy(bad,frame,sizeof frame); bad[6]=5;
    CHECK(tinyrt_mgmt_feed(&r,bad,sizeof bad)==-1);
    CHECK(tinyrt_mgmt_feed(&r,frame,9)==-1);
    CHECK(tinyrt_mgmt_feed(&r,frame,21)==-1); /* reject before reading */
    CHECK(tinyrt_mgmt_feed(&r,frame,sizeof frame)==1);
    tinyrt_mgmt_reset(&r);
    CHECK(tinyrt_mgmt_feed(&r,tail,sizeof tail)==-1);
    uint8_t data[256]; for(unsigned i=0;i<sizeof data;i++)data[i]=(uint8_t)i;
    CHECK(tinyrt_mgmt_emit(0x11,19,1,data,256,receive,NULL)==0);
    CHECK(frames==26 && decoded.response==1 && decoded.request_id==19);
    CHECK(memcmp(decoded.payload,data,256)==0);
    CHECK(tinyrt_mgmt_emit(0x11,19,1,data,257,receive,NULL)==-1);
    CHECK(tinyrt_mgmt_emit(0x11,19,1,data,1,refuse,NULL)==-7);
    CHECK(tinyrt_mgmt_emit(0x11,0,1,data,1,receive,NULL)==-1);
    printf("PASS transport %u checks\n",checks); return 0;
}
