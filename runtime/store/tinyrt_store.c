#include "tinyrt_store_internal.h"
#include <stdlib.h>
#include <string.h>
tinyrt_status_t tinyrt_store_open(const tinyrt_store_io_t *io,tinyrt_package_verify_fn v,void *c,tinyrt_store_t **out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    *out=NULL;
    if(!io||!io->read||!io->program||!io->erase||!io->sync||!v) return TINYRT_INVALID_ARGUMENT;
    tinyrt_store_t *s=calloc(1,sizeof(*s));
    if(!s) return TINYRT_NO_MEMORY;
    s->io=*io;s->verify=v;s->verify_ctx=c;
    tinyrt_status_t r=tr_recover(s);
    if(r!=TINYRT_OK) {free(s);return r;}
    *out=s;return TINYRT_OK;
}
void tinyrt_store_close(tinyrt_store_t *s) {if(s) {free(s->install);free(s);}}
static tinyrt_status_t list_health(tinyrt_store_t *s,tinyrt_app_info_t *a,uint32_t n,uint32_t *count,bool quarantined) {
    if(!s||!count||(n&&!a)) return TINYRT_INVALID_ARGUMENT;
    *count=0;
    if(s->dirty) {tinyrt_status_t r=tr_recover(s);if(r!=TINYRT_OK) return r;}
    for(uint32_t i=0;i<s->directory.count;++i)
        if(s->directory.records[i].quarantined==quarantined) ++*count;
    if(n<*count) return TINYRT_NO_SPACE;
    for(uint32_t i=0,j=0;i<s->directory.count;++i)
        if(s->directory.records[i].quarantined==quarantined) a[j++]=s->directory.records[i].app;
    for(uint32_t i=1;i<*count;++i) {
        tinyrt_app_info_t value=a[i];uint32_t j=i;
        while(j && strcmp(a[j-1].id.app_id,value.id.app_id)>0) {a[j]=a[j-1];--j;}
        a[j]=value;
    }
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_store_list(tinyrt_store_t *s,tinyrt_app_info_t *a,uint32_t n,uint32_t *count) {
    return list_health(s,a,n,count,false);
}
tinyrt_status_t tinyrt_store_list_quarantined(tinyrt_store_t *s,tinyrt_app_info_t *a,uint32_t n,uint32_t *count) {
    return list_health(s,a,n,count,true);
}
tinyrt_status_t tinyrt_store_query(tinyrt_store_t *s,const tinyrt_package_id_t *id,tinyrt_app_info_t *a) {
    if(!s||!id||!a||!tr_id_valid(id->app_id)||!id->version) return TINYRT_INVALID_ARGUMENT;
    /* The caller may query in place with id == &a->id. */
    tinyrt_package_id_t requested=*id;
    memset(a,0,sizeof(*a));
    if(s->dirty) {tinyrt_status_t r=tr_recover(s);if(r!=TINYRT_OK) return r;}
    for(uint32_t i=0;i<s->directory.count;++i)
        if(tr_identity_equal(&requested,&s->directory.records[i].app.id)) {
            if(s->directory.records[i].quarantined) return TINYRT_VERIFY_FAILED;
            *a=s->directory.records[i].app;return TINYRT_OK;
        }
    return TINYRT_NOT_FOUND;
}
tinyrt_status_t tinyrt_store_recheck(tinyrt_store_t *s,const tinyrt_package_id_t *id) {
    if(!s||!id||!tr_id_valid(id->app_id)||!id->version) return TINYRT_INVALID_ARGUMENT;
    if(s->install) return TINYRT_BUSY;
    if(s->dirty) {tinyrt_status_t r=tr_recover(s);if(r!=TINYRT_OK) return r;}
    for(uint32_t i=0;i<s->directory.count;++i) {
        tr_record_t *record=&s->directory.records[i];
        if(!tr_identity_equal(id,&record->app.id)) continue;
        tinyrt_app_info_t actual={0};
        tinyrt_status_t r=s->verify(s->verify_ctx,&s->io,record->offset,record->app.package_size,&actual);
        if(r==TINYRT_OK && (!tr_id_valid(actual.id.app_id)||!tr_app_equal(&actual,&record->app)))
            r=TINYRT_VERIFY_FAILED;
        if(r==TINYRT_CORRUPT) r=TINYRT_VERIFY_FAILED;
        if((r==TINYRT_OK||r==TINYRT_VERIFY_FAILED) && record->quarantined!=(r!=TINYRT_OK)) {
            if(s->revision==UINT64_MAX) return TINYRT_CONFLICT;
            record->quarantined=r!=TINYRT_OK;++s->revision;
        }
        return r;
    }
    return TINYRT_NOT_FOUND;
}

static tinyrt_status_t refresh_after_error(tinyrt_store_t *s,tinyrt_status_t error) {
    s->dirty=true;
    /* Recovery failure leaves dirty set. No later mutation may use stale RAM. */
    (void)tr_recover(s);
    return error;
}
static tinyrt_status_t publish_directory(tinyrt_store_t *s,const tr_directory_t *next) {
    uint8_t *b=malloc(4096);
    if(!b) return TINYRT_NO_MEMORY;
    tr_directory_encode(next,b);
    uint32_t target=s->active_sector==0?4096u:0u;
    tinyrt_status_t r=s->io.erase(s->io.ctx,target,4096);
    if(r==TINYRT_OK) r=s->io.program(s->io.ctx,target,b,4092);
    if(r==TINYRT_OK) r=s->io.sync(s->io.ctx);
    if(r==TINYRT_OK) r=s->io.program(s->io.ctx,target+4092,b+4092,4);
    if(r==TINYRT_OK) r=s->io.sync(s->io.ctx);
    free(b);
    if(r!=TINYRT_OK) return refresh_after_error(s,r);
    s->dirty=true;
    r=tr_recover(s);
    if(r!=TINYRT_OK) return r;
    if(s->directory.generation!=next->generation || s->active_sector!=(int)(target/4096))
        return TINYRT_CORRUPT;
    return TINYRT_OK;
}
static int find_app(const tinyrt_store_t *s,const char *id) {
    for(uint32_t i=0;i<s->directory.count;++i)
        if(!strcmp(s->directory.records[i].app.id.app_id,id)) return (int)i;
    return -1;
}
tinyrt_status_t tinyrt_store_begin(tinyrt_store_t *s,const tinyrt_app_info_t *a,tinyrt_install_t **out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    *out=NULL;
    if(!s||!a||!tr_id_valid(a->id.app_id)||!a->id.version||
       !a->package_size||a->package_size>TINYRT_STORE_MAX_PACKAGE_SIZE) return TINYRT_INVALID_ARGUMENT;
    if(s->install) return TINYRT_BUSY;
    tinyrt_status_t r=tr_recover(s);
    if(r!=TINYRT_OK) return r;
    int index=find_app(s,a->id.app_id);
    if(index>=0) {
        const tinyrt_app_info_t *old=&s->directory.records[index].app;
        if(tr_app_equal(old,a)) {
            if(!s->directory.records[index].quarantined) return TINYRT_ALREADY_INSTALLED;
        } else if(a->id.version<=old->id.version) return TINYRT_CONFLICT;
    } else if(s->directory.count>=TINYRT_STORE_MAX_APPS) return TINYRT_NO_SPACE;
    if(s->directory.generation==UINT64_MAX||s->revision==UINT64_MAX) return TINYRT_CONFLICT;

    tinyrt_install_t *h=calloc(1,sizeof(*h));
    if(!h) return TINYRT_NO_MEMORY;
    h->store=s;h->expected=*a;
    /* Normalize unused ID bytes; directory serialization is canonical. */
    memset(h->expected.id.app_id,0,32);
    memcpy(h->expected.id.app_id,a->id.app_id,strlen(a->id.app_id));
    uint32_t needed=tr_extent_size(a->package_size);
    h->offset=2*TINYRT_STORE_SECTOR_SIZE;
    /* First fit across all committed extents, including quarantined packages
     * and the old version being updated. Staging never erases either one. */
    for(;;) {
        if(needed>TINYRT_STORE_SIZE-h->offset) {free(h);return TINYRT_NO_SPACE;}
        uint32_t next=h->offset;
        for(uint32_t i=0;i<s->directory.count;++i) {
            const tr_record_t *record=&s->directory.records[i];
            uint32_t end=record->offset+tr_extent_size(record->app.package_size);
            if(h->offset<end && record->offset<h->offset+needed && end>next) next=end;
        }
        if(next==h->offset) break;
        h->offset=next;
    }
    if(s->active_sector<0) {
        tr_directory_t *empty=calloc(1,sizeof(*empty));
        if(!empty) {free(h);return TINYRT_NO_MEMORY;}
        empty->generation=1;r=publish_directory(s,empty);free(empty);
        if(r!=TINYRT_OK) {free(h);return r;}
    }
    /* Each erase call is one physical sector, making failure boundaries explicit. */
    for(uint32_t off=0;off<needed;off+=4096) {
        r=s->io.erase(s->io.ctx,h->offset+off,4096);
        if(r!=TINYRT_OK) {free(h);return refresh_after_error(s,r);}
    }
    s->install=h;*out=h;return TINYRT_OK;
}
tinyrt_status_t tinyrt_store_write(tinyrt_install_t *h,uint32_t off,const void *bytes,uint32_t n) {
    if(!h||!bytes||!n) return TINYRT_INVALID_ARGUMENT;
    if(h->failed) return TINYRT_IO_ERROR;
    if(h->committed||off!=h->received||off>h->expected.package_size||
       n>h->expected.package_size-off) return TINYRT_INVALID_ARGUMENT;
    tinyrt_store_t *s=h->store;
    tinyrt_status_t r=s->io.program(s->io.ctx,h->offset+off,bytes,n);
    if(r!=TINYRT_OK) {h->failed=true;return refresh_after_error(s,r);}
    h->received+=n;return TINYRT_OK;
}
tinyrt_status_t tinyrt_store_commit(tinyrt_install_t *h) {
    if(!h) return TINYRT_INVALID_ARGUMENT;
    if(h->committed) return TINYRT_ALREADY_INSTALLED;
    if(h->failed) return TINYRT_IO_ERROR;
    if(h->received!=h->expected.package_size) return TINYRT_INVALID_ARGUMENT;
    tinyrt_store_t *s=h->store;
    if(s->revision==UINT64_MAX) return TINYRT_CONFLICT;
    tinyrt_status_t r=s->io.sync(s->io.ctx);
    tinyrt_app_info_t actual={0};
    if(r==TINYRT_OK)
        r=s->verify(s->verify_ctx,&s->io,h->offset,h->received,&actual);
    if(r==TINYRT_OK&&(!tr_id_valid(actual.id.app_id)||!tr_app_equal(&actual,&h->expected)))
        r=TINYRT_VERIFY_FAILED;
    if(r!=TINYRT_OK) {h->failed=true;return refresh_after_error(s,r);}
    tr_directory_t *next=malloc(sizeof(*next));
    if(!next) return TINYRT_NO_MEMORY;
    *next=s->directory;
    int index=find_app(s,h->expected.id.app_id);
    if(index<0) index=(int)next->count++;
    next->records[index].app=h->expected;next->records[index].offset=h->offset;
    ++next->generation;
    r=publish_directory(s,next);free(next);
    if(r==TINYRT_OK && s->directory.records[index].quarantined) r=TINYRT_VERIFY_FAILED;
    if(r==TINYRT_OK) h->committed=true;else h->failed=true;
    return r;
}
void tinyrt_store_abort(tinyrt_install_t *h) {
    if(!h) return;
    h->store->install=NULL;
    free(h);
}
tinyrt_status_t tinyrt_store_uninstall(tinyrt_store_t *s,const char *id) {
    if(!s||!tr_id_valid(id)) return TINYRT_INVALID_ARGUMENT;
    if(s->install) return TINYRT_BUSY;
    tinyrt_status_t r=tr_recover(s);
    if(r!=TINYRT_OK) return r;
    int index=find_app(s,id);
    if(index<0) return TINYRT_NOT_FOUND;
    if(s->directory.generation==UINT64_MAX||s->revision==UINT64_MAX) return TINYRT_CONFLICT;
    tr_directory_t *next=malloc(sizeof(*next));
    if(!next) return TINYRT_NO_MEMORY;
    *next=s->directory;
    --next->count;
    if((uint32_t)index<next->count) next->records[index]=next->records[next->count];
    memset(&next->records[next->count],0,sizeof(next->records[0]));
    ++next->generation;
    r=publish_directory(s,next);free(next);return r;
}

tinyrt_status_t tinyrt_store_read(tinyrt_store_t *s,const tinyrt_package_id_t *id,
    uint32_t offset,void *bytes,uint32_t size) {
    if(!s||!id||!tr_id_valid(id->app_id)||!id->version||!bytes||!size)
        return TINYRT_INVALID_ARGUMENT;
    if(s->install) return TINYRT_BUSY;
    tinyrt_package_id_t requested=*id;
    if(s->dirty) {tinyrt_status_t r=tr_recover(s);if(r!=TINYRT_OK) return r;}
    for(uint32_t i=0;i<s->directory.count;++i) {
        const tr_record_t *record=&s->directory.records[i];
        if(!tr_identity_equal(&requested,&record->app.id)) continue;
        if(record->quarantined) return TINYRT_VERIFY_FAILED;
        if(offset>=record->app.package_size||size>record->app.package_size-offset)
            return TINYRT_INVALID_ARGUMENT;
        uint32_t base=record->offset+offset;
        uint8_t *out=bytes;
        for(uint32_t done=0;done<size;) {
            uint32_t n=size-done;if(n>TINYRT_STORE_SECTOR_SIZE) n=TINYRT_STORE_SECTOR_SIZE;
            tinyrt_status_t r=s->io.read(s->io.ctx,base+done,out+done,n);
            if(r!=TINYRT_OK) {s->dirty=true;return r;}
            done+=n;
        }
        return TINYRT_OK;
    }
    return TINYRT_NOT_FOUND;
}

tinyrt_status_t tinyrt_store_generation(tinyrt_store_t *s,uint64_t *out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    *out=0;
    if(!s) return TINYRT_INVALID_ARGUMENT;
    if(s->dirty) {tinyrt_status_t r=tr_recover(s);if(r!=TINYRT_OK) return r;}
    *out=s->revision;return TINYRT_OK;
}

tinyrt_status_t tinyrt_store_stats(tinyrt_store_t *s,tinyrt_store_stats_t *out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!s) return TINYRT_INVALID_ARGUMENT;
    if(s->install) return TINYRT_BUSY;
    if(s->dirty) {tinyrt_status_t r=tr_recover(s);if(r!=TINYRT_OK) return r;}
    tinyrt_store_stats_t stats={0};
    stats.generation=s->revision;
    stats.total_bytes=TINYRT_STORE_SIZE;
    stats.data_bytes=TINYRT_STORE_SIZE-2*TINYRT_STORE_SECTOR_SIZE;
    stats.max_apps=TINYRT_STORE_MAX_APPS;
    stats.max_package_size=TINYRT_STORE_MAX_PACKAGE_SIZE;
    stats.installed_count=s->directory.count;
    for(uint32_t i=0;i<s->directory.count;++i) {
        const tr_record_t *r=&s->directory.records[i];
        stats.package_bytes+=r->app.package_size;
        stats.allocated_bytes+=tr_extent_size(r->app.package_size);
        if(r->quarantined) ++stats.quarantined_count;
    }
    stats.free_bytes=stats.data_bytes-stats.allocated_bytes;
    /* Visit at most 16 extents in physical order without a temporary catalog.
     * Validated, aligned, disjoint intervals make every subtraction bounded. */
    uint32_t cursor=2*TINYRT_STORE_SECTOR_SIZE;
    for(;;) {
        uint32_t next=TINYRT_STORE_SIZE,end=TINYRT_STORE_SIZE;
        for(uint32_t i=0;i<s->directory.count;++i) {
            const tr_record_t *r=&s->directory.records[i];
            if(r->offset>=cursor && r->offset<next) {
                next=r->offset;end=next+tr_extent_size(r->app.package_size);
            }
        }
        if(next-cursor>stats.largest_free_bytes) stats.largest_free_bytes=next-cursor;
        if(next==TINYRT_STORE_SIZE) break;
        cursor=end;
    }
    *out=stats;return TINYRT_OK;
}
