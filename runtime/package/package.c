#include "tinyrt_package.h"
#include "package_crypto.h"
#include <stdbool.h>
#include <string.h>

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static bool all_zero(const uint8_t *p, size_t n) {
    for(size_t i=0;i<n;++i) if(p[i]) return false;
    return true;
}
static bool canonical_string(const uint8_t *s, size_t capacity, size_t *length) {
    size_t n=0;
    while(n<capacity && s[n]) ++n;
    if(!n || n==capacity || !all_zero(s+n,capacity-n)) return false;
    *length=n;
    return true;
}
static bool valid_id(const uint8_t *p) {
    size_t n;
    if(!canonical_string(p,32,&n)) return false;
    for(size_t i=0;i<n;++i) {
        uint8_t c=p[i];
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='.' || c=='_' || c=='-')) return false;
    }
    return true;
}
static tinyrt_status_t authorize_id(const char *scope,const char *id) {
    if(!scope || !scope[0]) return TINYRT_OK;
    size_t n=0;
    for(;n<32 && scope[n];++n) {
        unsigned char c=(unsigned char)scope[n];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))
            return TINYRT_INVALID_ARGUMENT;
    }
    if(n==32) return TINYRT_INVALID_ARGUMENT;
    if(scope[n-1]=='.')
        return strlen(id)>n && !strncmp(scope,id,n) ? TINYRT_OK : TINYRT_VERIFY_FAILED;
    return !strcmp(scope,id) ? TINYRT_OK : TINYRT_VERIFY_FAILED;
}
static bool valid_title(const uint8_t *p) {
    size_t n;
    if(!canonical_string(p,64,&n)) return false;
    for(size_t i=0;i<n;) {
        uint32_t cp=p[i++], minimum;
        unsigned continuation;
        if(cp<0x80) continue;
        if(cp>=0xc2 && cp<=0xdf) {cp&=0x1f; continuation=1; minimum=0x80;}
        else if(cp>=0xe0 && cp<=0xef) {cp&=0x0f; continuation=2; minimum=0x800;}
        else if(cp>=0xf0 && cp<=0xf4) {cp&=0x07; continuation=3; minimum=0x10000;}
        else return false;
        if(continuation>n-i) return false;
        for(unsigned j=0;j<continuation;++j) {
            uint8_t c=p[i++];
            if((c&0xc0)!=0x80) return false;
            cp=(cp<<6)|(c&0x3f);
        }
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    return true;
}
/* Canonical encoding checks only; cryptographic verification is delegated. */
static bool canonical_signature(const uint8_t *signature) {
    static const uint8_t order[32]={
        0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51};
    static const uint8_t half[32]={
        0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8};
    return !all_zero(signature,32) && memcmp(signature,order,32)<0 &&
           !all_zero(signature+32,32) && memcmp(signature+32,half,32)<=0;
}
static bool parse_header(const uint8_t h[256], uint32_t length, tinyrt_package_metadata_t *m) {
    uint32_t format=get16(h+8);
    if(format!=1 || memcmp(h,"TRPKG001",8) || get16(h+10)!=256 ||
       get32(h+12)!=length || !all_zero(h+184,8)) return false;
    m->format_version=format;
    if(get32(h+16)!=256 || get32(h+20)<1 || get32(h+20)>3 ||
       get32(h+24)!=16 || get32(h+28)) return false;
    m->app.id.version=get32(h+32);
    m->policy.abi_version=get32(h+36); m->policy.permissions=get32(h+40);
    m->policy.max_memory_pages=get32(h+44); m->policy.instruction_budget=get32(h+48);
    m->signing_key_id=get32(h+52);
    if(!m->app.id.version || m->policy.abi_version!=TINYRT_PACKAGE_ABI_VERSION ||
       (m->policy.permissions&~TINYRT_PERMISSION_ALL) || !m->policy.max_memory_pages ||
       m->policy.max_memory_pages>16 || !m->policy.instruction_budget ||
       m->policy.instruction_budget>100000 || !valid_id(h+56) || !valid_title(h+88) ||
       !canonical_signature(h+192)) return false;
    memcpy(m->app.id.app_id,h+56,32); memcpy(m->title,h+88,64);
    m->app.package_size=length;
    return true;
}
static tinyrt_status_t signed_header_digest(const uint8_t *header,uint8_t digest[32]) {
    const char *domain="TinyRT-package-v1";
    tr_package_hash_t *hash=NULL;
    tinyrt_status_t r=tr_package_hash_new(&hash);
    if(r==TINYRT_OK) r=tr_package_hash_update(hash,domain,18); /* Includes one NUL. */
    if(r==TINYRT_OK) r=tr_package_hash_update(hash,header,192);
    if(r==TINYRT_OK) r=tr_package_hash_finish(hash,digest);
    tr_package_hash_free(hash);
    return r;
}
static tinyrt_status_t parse_sections(const tinyrt_store_io_t *io,uint32_t base,
    uint32_t length,uint32_t count,tinyrt_package_metadata_t *m) {
    uint32_t cursor=256+16*count,previous=0;
    if(cursor>length) return TINYRT_VERIFY_FAILED;
    for(uint32_t i=0;i<count;++i) {
        uint8_t entry[16],padding[3];
        tinyrt_status_t r=io->read(io->ctx,base+256+16*i,entry,sizeof(entry));
        if(r!=TINYRT_OK) return r;
        uint32_t kind=get32(entry),off=get32(entry+8),size=get32(entry+12);
        uint32_t pad=(4-(cursor&3u))&3u;
        if(kind<=previous || kind>3 || get32(entry+4) || pad>length-cursor ||
           off!=cursor+pad || !size || off>length || size>length-off) return TINYRT_VERIFY_FAILED;
        if(pad) {
            r=io->read(io->ctx,base+cursor,padding,pad);
            if(r!=TINYRT_OK) return r;
            if(!all_zero(padding,pad)) return TINYRT_VERIFY_FAILED;
        }
        if(kind==1) {
            if(size<=8) return TINYRT_VERIFY_FAILED;
            m->wasm_offset=off;m->wasm_size=size;
        } else if(kind==2) {
            if(size<=256) return TINYRT_VERIFY_FAILED;
            m->aot_offset=off+256;m->aot_size=size-256;
        } else {m->assets_offset=off;m->assets_size=size;}
        previous=kind;cursor=off+size;
    }
    return cursor==length && (m->wasm_size || m->aot_size)?TINYRT_OK:TINYRT_VERIFY_FAILED;
}

static bool valid_target(const uint8_t *p) {
    size_t n;
    if(!canonical_string(p,16,&n)) return false;
    for(size_t i=0;i<n;++i)
        if(!((p[i]>='a' && p[i]<='z') || (p[i]>='0' && p[i]<='9') || p[i]=='_' || p[i]=='-')) return false;
    return true;
}
static tinyrt_status_t inspect_aot(const tinyrt_package_verifier_t *v,
    const tinyrt_package_trusted_key_t *key,const tinyrt_store_io_t *io,uint32_t off,
    tinyrt_package_metadata_t *m) {
    const tinyrt_package_aot_profile_t *p=v->aot_profile;
    if(key->aot_authority==TINYRT_AOT_AUTHORITY_NONE) return TINYRT_VERIFY_FAILED;
    if(key->aot_authority==TINYRT_AOT_AUTHORITY_DEVELOPMENT) {
        if(!p || !p->allow_development || strncmp(m->app.id.app_id,"demo.",5) || !m->app.id.app_id[5])
            return TINYRT_VERIFY_FAILED;
    } else if(key->aot_authority!=TINYRT_AOT_AUTHORITY_RELEASE) return TINYRT_INVALID_ARGUMENT;
    uint8_t raw[256];
    tinyrt_status_t r=io->read(io->ctx,off+m->aot_offset-256,raw,sizeof(raw));
    if(r!=TINYRT_OK) return r;
    if(get16(raw)!=1 || get16(raw+2)!=256 || !get32(raw+4) ||
       get32(raw+8)!=TINYRT_AOT_REQUIRED_FLAGS || !all_zero(raw+12,4) ||
       !valid_target(raw+16) || !valid_target(raw+32) || !all_zero(raw+248,8)) return TINYRT_VERIFY_FAILED;
    for(uint32_t pos=48;pos<88;pos+=20) if(all_zero(raw+pos,20)) return TINYRT_VERIFY_FAILED;
    for(uint32_t pos=88;pos<248;pos+=32) if(all_zero(raw+pos,32)) return TINYRT_VERIFY_FAILED;
    tinyrt_package_aot_metadata_t *a=&m->aot;
    a->format_version=get32(raw+4);a->safety_flags=get32(raw+8);
    memcpy(a->target_arch,raw+16,16);memcpy(a->target_cpu,raw+32,16);
    memcpy(a->wamr_commit,raw+48,20);memcpy(a->llvm_commit,raw+68,20);
    memcpy(a->patch_sha256,raw+88,32);memcpy(a->compat_id,raw+120,32);
    memcpy(a->options_sha256,raw+152,32);memcpy(a->compiler_sha256,raw+184,32);
    memcpy(a->source_wasm_sha256,raw+216,32);
    if(!p || !p->enabled) m->fallback_reason=TINYRT_PACKAGE_FALLBACK_DISABLED;
    else {
        if(!valid_target((const uint8_t *)p->target_arch) || !valid_target((const uint8_t *)p->target_cpu) ||
           !p->format_version || all_zero(p->compat_id,32) || !v->validate_aot) return TINYRT_INVALID_ARGUMENT;
        if(memcmp(p->target_arch,a->target_arch,16) || memcmp(p->target_cpu,a->target_cpu,16))
            m->fallback_reason=TINYRT_PACKAGE_FALLBACK_TARGET;
        else if(p->format_version!=a->format_version || memcmp(p->compat_id,a->compat_id,32))
            m->fallback_reason=TINYRT_PACKAGE_FALLBACK_COMPAT;
        else m->execution_kind=TINYRT_PACKAGE_EXEC_AOT;
    }
    if(m->execution_kind!=TINYRT_PACKAGE_EXEC_AOT && !m->wasm_size) return TINYRT_VERIFY_FAILED;
    return TINYRT_OK;
}
static tinyrt_status_t validation_status(tinyrt_status_t r) {
    return (r==TINYRT_OK || r==TINYRT_IO_ERROR || r==TINYRT_NO_MEMORY || r==TINYRT_BUSY ||
            r==TINYRT_INVALID_ARGUMENT)?r:TINYRT_VERIFY_FAILED;
}

tinyrt_status_t tinyrt_package_inspect(const tinyrt_package_verifier_t *v,
    const tinyrt_store_io_t *io,uint32_t off,uint32_t length,tinyrt_package_metadata_t *out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!v || !io || !io->read ||
       (!v->validate_wasm && !v->validate_aot) ||
       (v->trusted_key_count && !v->trusted_keys) || length>UINT32_MAX-off)
        return TINYRT_INVALID_ARGUMENT;
    if(length<256 || length>TINYRT_STORE_MAX_PACKAGE_SIZE) return TINYRT_VERIFY_FAILED;
    uint8_t header[256],digest[32];
    tinyrt_status_t r=io->read(io->ctx,off,header,sizeof(header));
    if(r!=TINYRT_OK) return r;
    tinyrt_package_metadata_t m={0};
    if(!parse_header(header,length,&m)) return TINYRT_VERIFY_FAILED;
    const tinyrt_package_trusted_key_t *trusted=NULL;
    for(size_t i=0;i<v->trusted_key_count;++i) {
        if(v->trusted_keys[i].key_id==m.signing_key_id) {
            if(trusted) return TINYRT_INVALID_ARGUMENT; /* Ambiguous trust configuration. */
            trusted=&v->trusted_keys[i];
        }
    }
    if(!trusted || trusted->public_key[0]!=4) return TINYRT_VERIFY_FAILED;
    r=authorize_id(trusted->app_id_prefix,m.app.id.app_id);
    if(r!=TINYRT_OK) return r;
    r=signed_header_digest(header,digest);
    if(r==TINYRT_OK) r=tr_package_p256_verify(trusted->public_key,digest,header+192);
    if(r!=TINYRT_OK) return r;
    r=parse_sections(io,off,length,get32(header+20),&m);
    if(r!=TINYRT_OK) return r;
    m.execution_kind=TINYRT_PACKAGE_EXEC_WASM;
    if(m.wasm_size && !v->validate_wasm) return TINYRT_INVALID_ARGUMENT;
    if(m.aot_size) {
        r=inspect_aot(v,trusted,io,off,&m);
        if(r!=TINYRT_OK) return r;
    }
    tr_package_hash_t *whole=NULL,*payload=NULL,*source=NULL;
    r=tr_package_hash_new(&whole);
    if(r==TINYRT_OK) r=tr_package_hash_new(&payload);
    if(r==TINYRT_OK && m.aot_size && m.wasm_size) r=tr_package_hash_new(&source);
    if(r==TINYRT_OK) r=tr_package_hash_update(whole,header,sizeof(header));
    uint8_t block[1024];
    for(uint32_t pos=256;r==TINYRT_OK && pos<length;) {
        uint32_t n=length-pos;
        if(n>sizeof(block)) n=(uint32_t)sizeof(block);
        r=io->read(io->ctx,off+pos,block,n);
        if(r==TINYRT_OK) r=tr_package_hash_update(whole,block,n);
        if(r==TINYRT_OK) r=tr_package_hash_update(payload,block,n);
        if(r==TINYRT_OK && source && pos<m.wasm_offset+m.wasm_size && pos+n>m.wasm_offset) {
            uint32_t begin=pos>m.wasm_offset?pos:m.wasm_offset;
            uint32_t end=pos+n<m.wasm_offset+m.wasm_size?pos+n:m.wasm_offset+m.wasm_size;
            r=tr_package_hash_update(source,block+begin-pos,end-begin);
        }
        pos+=n;
    }
    if(r==TINYRT_OK) r=tr_package_hash_finish(payload,digest);
    if(r==TINYRT_OK && memcmp(digest,header+152,32)) r=TINYRT_VERIFY_FAILED;
    if(r==TINYRT_OK) r=tr_package_hash_finish(whole,m.app.id.sha256);
    if(r==TINYRT_OK && source) {
        r=tr_package_hash_finish(source,digest);
        if(r==TINYRT_OK && memcmp(digest,m.aot.source_wasm_sha256,32)) r=TINYRT_VERIFY_FAILED;
    }
    tr_package_hash_free(source);tr_package_hash_free(payload); tr_package_hash_free(whole);
    if(r!=TINYRT_OK) return r;
    if(m.wasm_size) r=validation_status(v->validate_wasm(v->wasm_ctx,io,off+m.wasm_offset,m.wasm_size,&m.policy));
    if(r==TINYRT_OK && m.execution_kind==TINYRT_PACKAGE_EXEC_AOT)
        r=validation_status(v->validate_aot(v->aot_ctx,io,off+m.aot_offset,m.aot_size,&m.policy,&m.aot));
    if(r!=TINYRT_OK) return r;
    *out=m;
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_package_verify(void *ctx,const tinyrt_store_io_t *io,
    uint32_t off,uint32_t length,tinyrt_app_info_t *out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    tinyrt_package_metadata_t metadata;
    tinyrt_status_t r=tinyrt_package_inspect(ctx,io,off,length,&metadata);
    if(r==TINYRT_OK) *out=metadata.app;
    return r;
}
