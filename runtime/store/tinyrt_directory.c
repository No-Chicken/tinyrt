#include "tinyrt_store_internal.h"
#include <stdlib.h>
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t get64(const uint8_t *p) { return get32(p)|((uint64_t)get32(p+4)<<32); }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8)); }
static void put64(uint8_t *p,uint64_t v) { put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32)); }
static uint32_t crc32(const uint8_t *b,uint32_t n) {
    uint32_t c=UINT32_MAX;
    for(uint32_t i=0;i<n;++i) {
        c^=b[i];
        for(unsigned j=0;j<8;++j) c=(c>>1)^(UINT32_C(0xedb88320)&(0u-(c&1u)));
    }
    return ~c;
}
static bool all_value(const uint8_t *p,uint32_t n,uint8_t value) {
    for(uint32_t i=0;i<n;++i) if(p[i]!=value) return false;
    return true;
}
uint32_t tr_slot_offset(uint32_t slot) { return 0x2000u+slot*TINYRT_STORE_SLOT_SIZE; }
bool tr_id_valid(const char *id) {
    if(!id || !id[0]) return false;
    for(unsigned i=0;i<32;++i) {
        unsigned char c=(unsigned char)id[i];
        if(!c) return true;
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) return false;
    }
    return false;
}
bool tr_identity_equal(const tinyrt_package_id_t *a,const tinyrt_package_id_t *b) {
    return a->version==b->version && !strcmp(a->app_id,b->app_id) && !memcmp(a->sha256,b->sha256,32);
}
bool tr_app_equal(const tinyrt_app_info_t *a,const tinyrt_app_info_t *b) {
    return a->package_size==b->package_size && tr_identity_equal(&a->id,&b->id);
}
void tr_directory_encode(const tr_directory_t *d,uint8_t *b) {
    memset(b,255,4096);memcpy(b,"TRDIR001",8);
    put32(b+8,1);put32(b+12,d->count);put64(b+16,d->generation);memset(b+24,0,8);
    for(uint32_t i=0;i<d->count;++i) {
        const tr_record_t *r=&d->records[i];uint8_t *p=b+32+80*i;
        memset(p,0,80);memcpy(p,r->app.id.app_id,strlen(r->app.id.app_id));
        put32(p+32,r->app.id.version);put32(p+36,r->slot);put32(p+40,r->app.package_size);
        memcpy(p+44,r->app.id.sha256,32);
    }
    put32(b+4088,crc32(b,4088));put32(b+4092,0);
}
static bool decode(const uint8_t *b,tr_directory_t *d) {
    memset(d,0,sizeof(*d));
    if(memcmp(b,"TRDIR001",8)||get32(b+8)!=1||get32(b+12)>2||!get64(b+16)||
       get32(b+4092)!=0||get32(b+4088)!=crc32(b,4088)||!all_value(b+24,8,0)) return false;
    d->count=get32(b+12);d->generation=get64(b+16);
    if(!all_value(b+32+80*d->count,4088-(32+80*d->count),255)) return false;
    for(uint32_t i=0;i<d->count;++i) {
        const uint8_t *p=b+32+80*i;tr_record_t *r=&d->records[i];
        memcpy(r->app.id.app_id,p,32);
        if(!tr_id_valid(r->app.id.app_id)) return false;
        size_t used=strlen(r->app.id.app_id);
        if(!all_value(p+(uint32_t)used,32-(uint32_t)used,0)) return false;
        r->app.id.version=get32(p+32);r->slot=get32(p+36);r->app.package_size=get32(p+40);
        memcpy(r->app.id.sha256,p+44,32);
        if(!r->app.id.version||r->slot>=3||!r->app.package_size||
           r->app.package_size>TINYRT_STORE_SLOT_SIZE||get32(p+76)) return false;
        if(i && (r->slot==d->records[0].slot||!strcmp(r->app.id.app_id,d->records[0].app.id.app_id))) return false;
    }
    return true;
}
static bool directories_equal(const tr_directory_t *a,const tr_directory_t *b) {
    if(a->count!=b->count) return false;
    for(uint32_t i=0;i<a->count;++i)
        if(a->records[i].slot!=b->records[i].slot||!tr_app_equal(&a->records[i].app,&b->records[i].app)) return false;
    return true;
}
static tinyrt_status_t initial_state(tinyrt_store_t *s,uint8_t *b) {
    tr_directory_t empty={0};empty.generation=1;
    uint8_t *expected=malloc(4096);
    if(!expected) return TINYRT_NO_MEMORY;
    tr_directory_encode(&empty,expected);
    tinyrt_status_t result=s->io.read(s->io.ctx,0,b,4096);
    if(result==TINYRT_OK) {
        for(unsigned i=0;i<4096;++i) if((b[i]&expected[i])!=expected[i]) {result=TINYRT_CORRUPT;break;}
    }
    free(expected);
    if(result!=TINYRT_OK) return result;
    for(uint32_t off=4096;off<TINYRT_STORE_SIZE;off+=4096) {
        result=s->io.read(s->io.ctx,off,b,4096);
        if(result!=TINYRT_OK) return result;
        if(!all_value(b,4096,255)) return TINYRT_CORRUPT;
    }
    memset(&s->directory,0,sizeof(s->directory));s->active_sector=-1;
    return TINYRT_OK;
}
tinyrt_status_t tr_recover(tinyrt_store_t *s) {
    tr_directory_t d[2];bool valid[2]={false,false};
    uint8_t *b=malloc(4096);
    if(!b) return TINYRT_NO_MEMORY;
    tinyrt_status_t result=TINYRT_OK;
    for(unsigned sector=0;sector<2;++sector) {
        result=s->io.read(s->io.ctx,sector*4096,b,4096);
        if(result!=TINYRT_OK) goto done;
        valid[sector]=decode(b,&d[sector]);
    }
    if(valid[0]&&valid[1]&&d[0].generation==d[1].generation&&!directories_equal(&d[0],&d[1])) {
        result=TINYRT_CORRUPT;goto done;
    }
    if(valid[0]||valid[1]) {
        unsigned pick=valid[1]&&(!valid[0]||d[1].generation>d[0].generation)?1u:0u;
        /* Commit history is selected solely from directory structure. A bad
         * app must never resurrect an uninstalled app or roll back an update. */
        for(uint32_t i=0;i<d[pick].count;++i) {
            tr_record_t *r=&d[pick].records[i];tinyrt_app_info_t actual={0};
            result=s->verify(s->verify_ctx,&s->io,tr_slot_offset(r->slot),r->app.package_size,&actual);
            if(result==TINYRT_VERIFY_FAILED||result==TINYRT_CORRUPT) {
                r->quarantined=true;result=TINYRT_OK;continue;
            }
            /* Resource, IO, busy and verifier configuration errors are not
             * evidence against an installed package. Publish no partial RAM. */
            if(result!=TINYRT_OK) goto done;
            r->quarantined=!tr_id_valid(actual.id.app_id)||!tr_app_equal(&actual,&r->app);
        }
        s->directory=d[pick];s->active_sector=(int)pick;
    } else result=initial_state(s,b);
done:
    free(b);s->dirty=result!=TINYRT_OK;return result;
}
