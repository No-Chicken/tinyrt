#include "package_crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdlib.h>
static tinyrt_status_t failure_of(NTSTATUS status) {
    if((ULONG)status==0xC0000017UL || (ULONG)status==0xC000009AUL) return TINYRT_NO_MEMORY;
    return TINYRT_IO_ERROR;
}
struct tr_package_hash {
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    uint8_t *object;
};
void tr_package_hash_free(tr_package_hash_t *h) {
    if(!h) return;
    if(h->hash) BCryptDestroyHash(h->hash);
    if(h->algorithm) BCryptCloseAlgorithmProvider(h->algorithm,0);
    free(h->object); free(h);
}
tinyrt_status_t tr_package_hash_new(tr_package_hash_t **out) {
    *out=NULL;
    tr_package_hash_t *h=calloc(1,sizeof(*h));
    if(!h) return TINYRT_NO_MEMORY;
    NTSTATUS status=BCryptOpenAlgorithmProvider(&h->algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0);
    DWORD size=0,result=0;
    if(status>=0) status=BCryptGetProperty(h->algorithm,BCRYPT_OBJECT_LENGTH,
        (PUCHAR)&size,sizeof(size),&result,0);
    if(status<0 || !size) {tr_package_hash_free(h); return failure_of(status);}
    h->object=malloc(size);
    if(!h->object) {tr_package_hash_free(h); return TINYRT_NO_MEMORY;}
    status=BCryptCreateHash(h->algorithm,&h->hash,h->object,size,NULL,0,0);
    if(status<0) {tr_package_hash_free(h); return failure_of(status);}
    *out=h; return TINYRT_OK;
}
tinyrt_status_t tr_package_hash_update(tr_package_hash_t *h,const void *bytes,uint32_t n) {
    NTSTATUS status=BCryptHashData(h->hash,(PUCHAR)bytes,n,0);
    return status>=0 ? TINYRT_OK : failure_of(status);
}
tinyrt_status_t tr_package_hash_finish(tr_package_hash_t *h,uint8_t digest[32]) {
    NTSTATUS status=BCryptFinishHash(h->hash,digest,32,0);
    return status>=0 ? TINYRT_OK : failure_of(status);
}
