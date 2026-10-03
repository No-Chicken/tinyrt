#ifndef TINYRT_PACKAGE_H
#define TINYRT_PACKAGE_H
#include "tinyrt_store.h"
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TINYRT_PACKAGE_HEADER_SIZE 256u
#define TINYRT_PACKAGE_ABI_VERSION 1u
#define TINYRT_PERMISSION_DRAW 1u
#define TINYRT_PERMISSION_INPUT 2u
#define TINYRT_PERMISSION_STORAGE 4u
#define TINYRT_PERMISSION_CLOCK 8u
#define TINYRT_PERMISSION_AUDIO 16u
#define TINYRT_PERMISSION_ALL 31u
typedef struct {
    uint32_t abi_version, permissions, max_memory_pages, instruction_budget;
} tinyrt_package_policy_t;
#define TINYRT_AOT_LOOP_POLL 1u
#define TINYRT_AOT_BOUNDS_CHECKS 2u
#define TINYRT_AOT_STACK_CHECKS 4u
#define TINYRT_AOT_REQUIRED_FLAGS 7u
typedef enum {
    TINYRT_PACKAGE_EXEC_NONE=0, TINYRT_PACKAGE_EXEC_WASM=1, TINYRT_PACKAGE_EXEC_AOT=2
} tinyrt_package_execution_t;
typedef enum {
    TINYRT_PACKAGE_FALLBACK_NONE=0, TINYRT_PACKAGE_FALLBACK_DISABLED=1,
    TINYRT_PACKAGE_FALLBACK_TARGET=2, TINYRT_PACKAGE_FALLBACK_COMPAT=3
} tinyrt_package_fallback_t;
typedef struct {
    uint32_t format_version, safety_flags;
    char target_arch[16], target_cpu[16];
    uint8_t wamr_commit[20], llvm_commit[20], patch_sha256[32], compat_id[32];
    uint8_t options_sha256[32], compiler_sha256[32], source_wasm_sha256[32];
} tinyrt_package_aot_metadata_t;
#define TINYRT_COVER_CODEC_RGB565LE_TWO_SIZES 1u
#define TINYRT_COVER_HEADER_SIZE 32u
#define TINYRT_COVER_SECTION_SIZE 133232u
typedef struct {
    uint32_t offset, size, codec;
    uint8_t sha256[32]; /* Complete signed section, including its 32-byte header. */
} tinyrt_package_cover_t;
typedef struct {
    tinyrt_app_info_t app;
    char title[64];
    uint32_t wasm_offset, wasm_size, assets_offset, assets_size, signing_key_id;
    tinyrt_package_policy_t policy;
    uint32_t format_version, aot_offset, aot_size;
    tinyrt_package_aot_metadata_t aot;
    tinyrt_package_cover_t cover;
    tinyrt_package_execution_t execution_kind;
    tinyrt_package_fallback_t fallback_reason;
} tinyrt_package_metadata_t;
typedef enum {
    TINYRT_AOT_AUTHORITY_NONE=0, TINYRT_AOT_AUTHORITY_RELEASE=1,
    TINYRT_AOT_AUTHORITY_DEVELOPMENT=2
} tinyrt_package_aot_authority_t;
typedef struct {
    uint32_t key_id;
    uint8_t public_key[65]; /* SEC1 uncompressed P-256: 04 || X32 || Y32. */
    const char *app_id_prefix; /* NULL/empty: all IDs; trailing '.': descendants;
                               * otherwise one exact ID. Host-owned lifetime. */
    tinyrt_package_aot_authority_t aot_authority; /* Zero keeps legacy keys Wasm-only. */
} tinyrt_package_trusted_key_t;
/* Required: validate ABI exports, structure, imports and memory without running
 * guest code. Offset is absolute in io; policy is authenticated.
 * Preserve IO_ERROR, NO_MEMORY, BUSY and host configuration/lifecycle
 * INVALID_ARGUMENT. Invalid guest bytes must return VERIFY_FAILED. */
typedef tinyrt_status_t (*tinyrt_package_wasm_validate_fn)(
    void *, const tinyrt_store_io_t *, uint32_t, uint32_t,
    const tinyrt_package_policy_t *);
typedef struct {
    bool enabled, allow_development;
    uint32_t format_version;
    char target_arch[16], target_cpu[16];
    uint8_t compat_id[32];
} tinyrt_package_aot_profile_t;
/* Authenticated metadata states release-pipeline claims, not machine-code proof.
 * Only called for an authorized, compatible module; never execute guest code. */
typedef tinyrt_status_t (*tinyrt_package_aot_validate_fn)(
    void *, const tinyrt_store_io_t *, uint32_t, uint32_t,
    const tinyrt_package_policy_t *, const tinyrt_package_aot_metadata_t *);
typedef struct {
    const tinyrt_package_trusted_key_t *trusted_keys;
    size_t trusted_key_count;
    tinyrt_package_wasm_validate_fn validate_wasm;
    void *wasm_ctx;
    const tinyrt_package_aot_profile_t *aot_profile; /* Host-owned; NULL disables AOT. */
    tinyrt_package_aot_validate_fn validate_aot;
    void *aot_ctx;
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
