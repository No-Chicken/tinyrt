#ifndef TINYRT_PACKAGE_CRYPTO_H
#define TINYRT_PACKAGE_CRYPTO_H
#include "tinyrt_store.h"
typedef struct tr_package_hash tr_package_hash_t;
tinyrt_status_t tr_package_hash_new(tr_package_hash_t **);
tinyrt_status_t tr_package_hash_update(tr_package_hash_t *, const void *, uint32_t);
tinyrt_status_t tr_package_hash_finish(tr_package_hash_t *, uint8_t[32]);
void tr_package_hash_free(tr_package_hash_t *);
tinyrt_status_t tr_package_p256_verify(const uint8_t[65], const uint8_t[32], const uint8_t[64]);
#endif
