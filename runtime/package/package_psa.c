#include "package_crypto.h"
#include <psa/crypto.h>
#include <stdlib.h>
struct tr_package_hash {psa_hash_operation_t operation;};
static tinyrt_status_t status_of(psa_status_t status) {
    if(status==PSA_SUCCESS) return TINYRT_OK;
    if(status==PSA_ERROR_INSUFFICIENT_MEMORY) return TINYRT_NO_MEMORY;
    if(status==PSA_ERROR_INVALID_ARGUMENT) return TINYRT_VERIFY_FAILED;
    return TINYRT_IO_ERROR;
}
tinyrt_status_t tr_package_hash_new(tr_package_hash_t **out) {
    *out=NULL;
    psa_status_t status=psa_crypto_init();
    if(status!=PSA_SUCCESS) return status_of(status);
    tr_package_hash_t *h=malloc(sizeof(*h));
    if(!h) return TINYRT_NO_MEMORY;
    h->operation=psa_hash_operation_init();
    status=psa_hash_setup(&h->operation,PSA_ALG_SHA_256);
    if(status!=PSA_SUCCESS) {psa_hash_abort(&h->operation); free(h); return status_of(status);}
    *out=h; return TINYRT_OK;
}
tinyrt_status_t tr_package_hash_update(tr_package_hash_t *h,const void *bytes,uint32_t n) {
    return status_of(psa_hash_update(&h->operation,bytes,n));
}
tinyrt_status_t tr_package_hash_finish(tr_package_hash_t *h,uint8_t digest[32]) {
    size_t length=0;
    psa_status_t status=psa_hash_finish(&h->operation,digest,32,&length);
    if(status==PSA_SUCCESS && length!=32) return TINYRT_VERIFY_FAILED;
    return status_of(status);
}
void tr_package_hash_free(tr_package_hash_t *h) {
    if(h) {psa_hash_abort(&h->operation); free(h);}
}
