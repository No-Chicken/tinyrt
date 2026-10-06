#ifndef TINYRT_STORE_H
#define TINYRT_STORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef TINYRT_STORE_SIZE
#define TINYRT_STORE_SIZE UINT32_C(0x4E0000)
#endif
#define TINYRT_STORE_SECTOR_SIZE UINT32_C(0x1000)
#define TINYRT_STORE_MAX_PACKAGE_SIZE UINT32_C(0x200000)
#define TINYRT_STORE_MAX_APPS 16u
typedef struct tinyrt_store tinyrt_store_t;
typedef struct tinyrt_install tinyrt_install_t;
typedef struct { char app_id[32]; uint32_t version; uint8_t sha256[32]; } tinyrt_package_id_t;
typedef struct { tinyrt_package_id_t id; uint32_t package_size; } tinyrt_app_info_t;
typedef struct {
    uint64_t generation;
    uint32_t total_bytes, data_bytes, package_bytes, allocated_bytes;
    uint32_t free_bytes, largest_free_bytes, installed_count, quarantined_count;
    uint32_t max_apps, max_package_size;
} tinyrt_store_stats_t;
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
/* Opaque inventory revision for this open store; includes RAM quarantine changes.
 * Blank inventory may be 0. Read/recovery only, never writes media; zero output on
 * error. Caller serializes with list/mutations. Restart requires a fresh listing. */
tinyrt_status_t tinyrt_store_generation(tinyrt_store_t *, uint64_t *);
/* Read-only committed space, including quarantined extents. Installed count
 * includes quarantine. Active install handles return BUSY; output is zero on
 * all failures. Generation is the same opaque inventory revision as above. */
tinyrt_status_t tinyrt_store_stats(tinyrt_store_t *, tinyrt_store_stats_t *);
tinyrt_status_t tinyrt_store_list(tinyrt_store_t *, tinyrt_app_info_t *, uint32_t, uint32_t *);
/* Quarantine preserves the committed identity/extent and counts against capacity.
 * Healthy list excludes these entries. Query/read return VERIFY_FAILED for an
 * exact quarantined identity (zero query output), NOT_FOUND for stale identities.
 * Reinstalling identical bytes repairs it; higher versions replace it normally.
 * Uninstall remains available. This is RAM health derived on recovery; quarantine
 * is not serialized and recovery never writes/erases bytes. */
tinyrt_status_t tinyrt_store_list_quarantined(tinyrt_store_t *, tinyrt_app_info_t *, uint32_t, uint32_t *);
/* Revalidate one exact committed identity and update its RAM health, without
 * writing media. VERIFY_FAILED confirms quarantine; resource/configuration/IO
 * errors preserve its previous health. Requires verifier resources to be idle. */
tinyrt_status_t tinyrt_store_recheck(tinyrt_store_t *, const tinyrt_package_id_t *);
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
