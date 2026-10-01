#ifndef TINYRT_PACKAGE_H
#define TINYRT_PACKAGE_H
#include "tinyrt_store.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TINYRT_PACKAGE_HEADER_SIZE 256u
#define TINYRT_PACKAGE_ABI_VERSION 1u
#define TINYRT_PERMISSION_DRAW 1u
#define TINYRT_PERMISSION_INPUT 2u
#define TINYRT_PERMISSION_STORAGE 4u
#define TINYRT_PERMISSION_CLOCK 8u
#define TINYRT_PERMISSION_ALL 15u
typedef struct {
    uint32_t abi_version, permissions, max_memory_pages, instruction_budget;
} tinyrt_package_policy_t;
typedef struct {
    tinyrt_app_info_t app;
    char title[64];
    uint32_t wasm_offset, wasm_size, assets_offset, assets_size, signing_key_id;
    tinyrt_package_policy_t policy;
} tinyrt_package_metadata_t;
typedef struct {
    uint32_t key_id;
    uint8_t public_key[65]; /* SEC1 uncompressed P-256: 04 || X32 || Y32. */
    const char *app_id_prefix; /* NULL/empty: all IDs; trailing '.': descendants;
                               * otherwise one exact ID. Host-owned lifetime. */
} tinyrt_package_trusted_key_t;
/* Required: validate ABI exports, structure, imports and memory without running
 * guest code. Offset is absolute in io; policy is authenticated.
 * Preserve IO_ERROR, NO_MEMORY, BUSY and host configuration/lifecycle
 * INVALID_ARGUMENT. Invalid guest bytes must return VERIFY_FAILED. */
typedef tinyrt_status_t (*tinyrt_package_wasm_validate_fn)(
    void *, const tinyrt_store_io_t *, uint32_t, uint32_t,
    const tinyrt_package_policy_t *);
typedef struct {
    const tinyrt_package_trusted_key_t *trusted_keys;
    size_t trusted_key_count;
    tinyrt_package_wasm_validate_fn validate_wasm;
    void *wasm_ctx;
} tinyrt_package_verifier_t;
/* io must remain immutable for this synchronous call. Reads <=1024 bytes.
 * Metadata offsets are package-relative. Output is zero on failure.
 * No embedded trusted keys or development fallback. */
tinyrt_status_t tinyrt_package_inspect(const tinyrt_package_verifier_t *,
    const tinyrt_store_io_t *, uint32_t, uint32_t, tinyrt_package_metadata_t *);
/* Context is a tinyrt_package_verifier_t; directly usable by tinyrt_store_open. */
tinyrt_status_t tinyrt_package_verify(void *, const tinyrt_store_io_t *,
    uint32_t, uint32_t, tinyrt_app_info_t *);
#ifdef __cplusplus
}
#endif
#endif
