#ifndef TINYRT_MANAGER_H
#define TINYRT_MANAGER_H
#include "tinyrt_runtime.h"
typedef struct tinyrt_manager tinyrt_manager_t;
#define TINYRT_MANAGER_SAVE_INTERVAL_MS 5000u
typedef struct {
    void *ctx;
    tinyrt_status_t (*load)(void *,const char *,uint32_t *,int32_t values[16]);
    tinyrt_status_t (*save)(void *,const char *,uint32_t,const int32_t values[16]);
    tinyrt_status_t (*clear)(void *,const char *);
    uint32_t (*now_ms)(void *);
} tinyrt_manager_storage_t;
/* Synchronous single-owner engine. Runtime system must already be initialized
 * on this owner thread; close manager before shutting down the runtime. */
tinyrt_status_t tinyrt_manager_open(const tinyrt_store_io_t *,const tinyrt_package_verifier_t *,const tinyrt_manager_storage_t *,tinyrt_manager_t **);
void tinyrt_manager_close(tinyrt_manager_t *);
tinyrt_status_t tinyrt_manager_list(tinyrt_manager_t *,tinyrt_package_metadata_t out[2],uint32_t *count);
/* Diagnostic committed identities, never runnable metadata. Uninstall accepts
 * these exact identities; begin permits identical-package repair or an upgrade. */
tinyrt_status_t tinyrt_manager_list_quarantined(tinyrt_manager_t *,tinyrt_app_info_t out[2],uint32_t *count);
tinyrt_status_t tinyrt_manager_query(tinyrt_manager_t *,const tinyrt_package_id_t *,tinyrt_app_info_t *);
tinyrt_status_t tinyrt_manager_begin(tinyrt_manager_t *,const tinyrt_app_info_t *);
tinyrt_status_t tinyrt_manager_write(tinyrt_manager_t *,const void *,uint32_t size);
tinyrt_status_t tinyrt_manager_finish(tinyrt_manager_t *);
void tinyrt_manager_abort(tinyrt_manager_t *);
tinyrt_status_t tinyrt_manager_uninstall(tinyrt_manager_t *,const tinyrt_package_id_t *);
tinyrt_status_t tinyrt_manager_start(tinyrt_manager_t *,const tinyrt_package_id_t *,int32_t width,int32_t height,tinyrt_frame_t *);
tinyrt_status_t tinyrt_manager_event(tinyrt_manager_t *,int32_t kind,int32_t x,int32_t y,int32_t arg,tinyrt_frame_t *);
/* KV setters change RAM. Successful init/event+render coalesce changes into at
 * most one save attempt per interval across all apps on this manager. The first
 * changed callback may save immediately; later unchanged callbacks also flush
 * pending data when due. now_ms must be monotonic modulo uint32_t; callbacks
 * must occur within one complete wrap for the timing bound to remain meaningful.
 * Guest faults/save errors discard unsaved RAM and destroy the instance.
 * Normal stop: bounded optional guest callback, then persist changed KV without
 * rendering. All outcomes destroy the instance; failure leaves durable KV
 * unchanged if the storage adapter provides transactional save. Idempotent.
 * Stop is an explicit host-controlled exception to the interval. It updates the
 * same timer; repeated host stop/start or manager recreation is not rate-limited.
 * Power loss can lose pending data; flush timing also depends on host scheduling.
 * close attempts stop but cannot report failure; call stop first if required. */
tinyrt_status_t tinyrt_manager_stop(tinyrt_manager_t *);
const char *tinyrt_manager_error(const tinyrt_manager_t *);
#endif
