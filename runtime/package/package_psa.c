#include "package_crypto.h"
#include <psa/crypto.h>
#include <stdlib.h>
struct tr_package_hash {psa_hash_operation_t operation;};
static tinyrt_status_t status_of(psa_status_t status) {
    if(status==PSA_SUCCESS) return TINYRT_OK;
    if(status==PSA_ERROR_INSUFFICIENT_MEMORY) return TINYRT_NO_MEMORY;
    if(status==PSA_ERROR_INVALID_SIGNATURE || status==PSA_ERROR_INVALID_ARGUMENT) return TINYRT_VERIFY_FAILED;
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
tinyrt_status_t tr_package_p256_verify(const uint8_t public_key[65],
    const uint8_t digest[32],const uint8_t signature[64]) {
    psa_status_t status=psa_crypto_init();
    if(status!=PSA_SUCCESS) return status_of(status);
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key=0;
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256);
    status=psa_import_key(&attributes,public_key,65,&key);
    psa_reset_key_attributes(&attributes);
    if(status==PSA_SUCCESS)
        status=psa_verify_hash(key,PSA_ALG_ECDSA(PSA_ALG_SHA_256),digest,32,signature,64);
    if(key) psa_destroy_key(key);
    return status_of(status);
}
