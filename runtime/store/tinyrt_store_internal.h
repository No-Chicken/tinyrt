#ifndef TINYRT_STORE_INTERNAL_H
#define TINYRT_STORE_INTERNAL_H
#include "tinyrt_store.h"
#include <stdbool.h>
typedef struct { tinyrt_app_info_t app; uint32_t slot; } tr_record_t;
typedef struct { uint64_t generation; uint32_t count; tr_record_t records[2]; } tr_directory_t;
struct tinyrt_store {
    tinyrt_store_io_t io;
    tinyrt_package_verify_fn verify;
    void *verify_ctx;
    tr_directory_t directory;
    int active_sector; /* -1: erased or interrupted initial provisioning */
    bool dirty;
    tinyrt_install_t *install;
};
struct tinyrt_install {
    tinyrt_store_t *store;
    tinyrt_app_info_t expected;
    uint32_t slot, received;
    bool committed, failed;
};
uint32_t tr_slot_offset(uint32_t slot);
bool tr_id_valid(const char *id);
bool tr_identity_equal(const tinyrt_package_id_t *a,const tinyrt_package_id_t *b);
bool tr_app_equal(const tinyrt_app_info_t *a,const tinyrt_app_info_t *b);
void tr_directory_encode(const tr_directory_t *d,uint8_t *b);
tinyrt_status_t tr_recover(tinyrt_store_t *s);
#endif
