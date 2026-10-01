#ifndef TINYRT_STORE_H
#define TINYRT_STORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TINYRT_STORE_SIZE UINT32_C(0xE0000)
#define TINYRT_STORE_SECTOR_SIZE UINT32_C(0x1000)
#define TINYRT_STORE_SLOT_SIZE UINT32_C(0x4A000)
#define TINYRT_STORE_SLOT_COUNT 3u
#define TINYRT_STORE_MAX_APPS 2u
typedef struct tinyrt_store tinyrt_store_t;
typedef struct tinyrt_install tinyrt_install_t;
typedef struct { char app_id[32]; uint32_t version; uint8_t sha256[32]; } tinyrt_package_id_t;
typedef struct { tinyrt_package_id_t id; uint32_t package_size; } tinyrt_app_info_t;
typedef enum {
    TINYRT_OK = 0, TINYRT_ALREADY_INSTALLED, TINYRT_INVALID_ARGUMENT,
    TINYRT_IO_ERROR, TINYRT_CORRUPT, TINYRT_NO_SPACE, TINYRT_BUSY,
    TINYRT_CONFLICT, TINYRT_NOT_FOUND, TINYRT_VERIFY_FAILED, TINYRT_NO_MEMORY
} tinyrt_status_t;
typedef struct {
    void *ctx;
    tinyrt_status_t (*read)(void *, uint32_t, void *, uint32_t);
    tinyrt_status_t (*program)(void *, uint32_t, const void *, uint32_t);
    tinyrt_status_t (*erase)(void *, uint32_t, uint32_t);
    tinyrt_status_t (*sync)(void *);
} tinyrt_store_io_t;
/* Verify the WHOLE package, independently deriving identity/length. Real ports
 * must check content digest, signature and policy. Preserve IO_ERROR. */
typedef tinyrt_status_t (*tinyrt_package_verify_fn)(void *, const tinyrt_store_io_t *,
    uint32_t, uint32_t, tinyrt_app_info_t *);
/* Synchronous NOR only. Caller serializes APIs and releases running resources
 * before mutation. IO callbacks enforce store bounds. */
tinyrt_status_t tinyrt_store_open(const tinyrt_store_io_t *, tinyrt_package_verify_fn, void *, tinyrt_store_t **);
void tinyrt_store_close(tinyrt_store_t *);
tinyrt_status_t tinyrt_store_list(tinyrt_store_t *, tinyrt_app_info_t *, uint32_t, uint32_t *);
tinyrt_status_t tinyrt_store_query(tinyrt_store_t *, const tinyrt_package_id_t *, tinyrt_app_info_t *);
/* Read committed identity only; offsets are package-relative. Caller still
 * serializes all access and releases readers before mutations. Reads are
 * unavailable while an install handle exists, including after commit. */
tinyrt_status_t tinyrt_store_read(tinyrt_store_t *, const tinyrt_package_id_t *,
    uint32_t offset, void *bytes, uint32_t size);
tinyrt_status_t tinyrt_store_begin(tinyrt_store_t *, const tinyrt_app_info_t *, tinyrt_install_t **);
tinyrt_status_t tinyrt_store_write(tinyrt_install_t *, uint32_t, const void *, uint32_t);
/* Commit retains handle. Repeat returns ALREADY_INSTALLED. */
tinyrt_status_t tinyrt_store_commit(tinyrt_install_t *);
/* NULL safe; frees handle. Never pass a freed pointer. Does not undo commit. */
void tinyrt_store_abort(tinyrt_install_t *);
tinyrt_status_t tinyrt_store_uninstall(tinyrt_store_t *, const char *);
#ifdef __cplusplus
}
#endif
#endif
