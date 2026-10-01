#include "tinyrt_manager.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tinyrt_manager {
    tinyrt_store_t *store;
    tinyrt_package_verifier_t verifier;
    tinyrt_manager_storage_t storage;
    tinyrt_install_t *install;
    uint32_t received;
    tinyrt_runtime_t *runtime;
    tinyrt_app_info_t running;
    uint32_t mask;
    int32_t values[16];
    tinyrt_frame_t frame;
    tinyrt_package_metadata_t catalog[2];
    uint32_t catalog_count;
    bool catalog_valid;
    char error[128];
};
typedef struct {
    tinyrt_manager_t *manager;
    tinyrt_package_id_t id;
} package_reader_t;
static tinyrt_status_t package_read(void *ctx, uint32_t off, void *bytes, uint32_t size) {
    package_reader_t *r = ctx;
    return tinyrt_store_read(r->manager->store, &r->id, off, bytes, size);
}
static tinyrt_status_t inspect(tinyrt_manager_t *m, const tinyrt_app_info_t *app,
                               tinyrt_package_metadata_t *out) {
    package_reader_t reader = {m, app->id};
    tinyrt_store_io_t io = {0};
    io.ctx = &reader;
    io.read = package_read;
    tinyrt_status_t status = tinyrt_package_inspect(&m->verifier, &io, 0, app->package_size, out);
    if (status == TINYRT_OK &&
        (out->app.package_size != app->package_size || out->app.id.version != app->id.version ||
         memcmp(out->app.id.app_id, app->id.app_id, 32) || memcmp(out->app.id.sha256, app->id.sha256, 32))) {
        memset(out, 0, sizeof(*out));
        return TINYRT_VERIFY_FAILED;
    }
    return status;
}
tinyrt_status_t tinyrt_manager_open(const tinyrt_store_io_t *io, const tinyrt_package_verifier_t *v,
                                    const tinyrt_manager_storage_t *storage, tinyrt_manager_t **out) {
    if (!out)
        return TINYRT_INVALID_ARGUMENT;
    *out = NULL;
    if (!io || !v || !storage || !storage->load || !storage->save || !storage->clear || !storage->now_ms)
        return TINYRT_INVALID_ARGUMENT;
    tinyrt_manager_t *m = calloc(1, sizeof(*m));
    if (!m)
        return TINYRT_NO_MEMORY;
    m->verifier = *v;
    m->storage = *storage;
    tinyrt_status_t r = tinyrt_store_open(io, tinyrt_package_verify, &m->verifier, &m->store);
    if (r != TINYRT_OK) {
        free(m);
        return r;
    }
    *out = m;
    return TINYRT_OK;
}
void tinyrt_manager_stop(tinyrt_manager_t *m) {
    if (!m)
        return;
    tinyrt_runtime_destroy(m->runtime);
    m->runtime = NULL;
    memset(&m->running, 0, sizeof(m->running));
    m->mask = 0;
    memset(m->values, 0, sizeof(m->values));
}
void tinyrt_manager_abort(tinyrt_manager_t *m) {
    if (!m)
        return;
    tinyrt_store_abort(m->install);
    m->install = NULL;
    m->received = 0;
}
void tinyrt_manager_close(tinyrt_manager_t *m) {
    if (!m)
        return;
    tinyrt_manager_stop(m);
    tinyrt_manager_abort(m);
    tinyrt_store_close(m->store);
    free(m);
}
tinyrt_status_t tinyrt_manager_list(tinyrt_manager_t *m, tinyrt_package_metadata_t out[2], uint32_t *count) {
    if (!m || !out || !count)
        return TINYRT_INVALID_ARGUMENT;
    *count = 0;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t apps[2];
    uint32_t n = 0;
    tinyrt_status_t r = tinyrt_store_list(m->store, apps, 2, &n);
    if (m->runtime) {
        /* Every cached entry was fully verified before this instance started.
         * A live WAMR instance prevents another module validation. Match the
         * durable identity, never infer metadata from an app ID alone. */
        if (!m->catalog_valid || n != m->catalog_count)
            return TINYRT_BUSY;
        for (uint32_t i = 0; r == TINYRT_OK && i < n; i++) {
            bool found = false;
            for (uint32_t j = 0; j < m->catalog_count; j++) {
                const tinyrt_app_info_t *a = &m->catalog[j].app;
                if (a->package_size == apps[i].package_size && a->id.version == apps[i].id.version &&
                    !memcmp(a->id.app_id, apps[i].id.app_id, 32) &&
                    !memcmp(a->id.sha256, apps[i].id.sha256, 32)) {
                    out[i] = m->catalog[j];
                    found = true;
                    break;
                }
            }
            if (!found)
                r = TINYRT_VERIFY_FAILED;
        }
    } else {
        m->catalog_valid = false;
        for (uint32_t i = 0; r == TINYRT_OK && i < n; i++)
            r = inspect(m, &apps[i], &out[i]);
        if (r == TINYRT_OK) {
            memcpy(m->catalog, out, n * sizeof(*out));
            m->catalog_count = n;
            m->catalog_valid = true;
        }
    }
    if (r == TINYRT_OK)
        *count = n;
    return r;
}
tinyrt_status_t tinyrt_manager_query(tinyrt_manager_t *m, const tinyrt_package_id_t *id,
                                     tinyrt_app_info_t *out) {
    return m ? tinyrt_store_query(m->store, id, out) : TINYRT_INVALID_ARGUMENT;
}
tinyrt_status_t tinyrt_manager_begin(tinyrt_manager_t *m, const tinyrt_app_info_t *a) {
    if (!m || !a || !a->package_size || a->package_size > TINYRT_STORE_SLOT_SIZE)
        return TINYRT_INVALID_ARGUMENT;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t found, apps[2];
    uint32_t count = 0;
    tinyrt_status_t r = tinyrt_store_query(m->store, &a->id, &found);
    if (r == TINYRT_OK)
        return found.package_size == a->package_size ? TINYRT_ALREADY_INSTALLED : TINYRT_CONFLICT;
    if (r != TINYRT_NOT_FOUND)
        return r;
    r = tinyrt_store_list(m->store, apps, 2, &count);
    if (r != TINYRT_OK)
        return r;
    bool update = false;
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(apps[i].id.app_id, a->id.app_id)) {
            if (a->id.version <= apps[i].id.version)
                return TINYRT_CONFLICT;
            update = true;
        }
    if (!update && count >= 2)
        return TINYRT_NO_SPACE;
    tinyrt_manager_stop(m);
    /* Clear before NEW install, not after. Interrupted uninstall cleanup or a
     * failed prior first install can never expose old data after reinstallation. */
    if (!update) {
        r = m->storage.clear(m->storage.ctx, a->id.app_id);
        if (r != TINYRT_OK)
            return r;
    }
    m->catalog_valid = false;
    r = tinyrt_store_begin(m->store, a, &m->install);
    if (r == TINYRT_OK)
        m->received = 0;
    return r;
}
tinyrt_status_t tinyrt_manager_write(tinyrt_manager_t *m, const void *bytes, uint32_t size) {
    if (!m || !m->install)
        return TINYRT_INVALID_ARGUMENT;
    tinyrt_status_t r = tinyrt_store_write(m->install, m->received, bytes, size);
    if (r == TINYRT_OK)
        m->received += size;
    return r;
}
tinyrt_status_t tinyrt_manager_finish(tinyrt_manager_t *m) {
    if (!m || !m->install)
        return TINYRT_INVALID_ARGUMENT;
    tinyrt_status_t r = tinyrt_store_commit(m->install);
    tinyrt_manager_abort(m);
    return r;
}
tinyrt_status_t tinyrt_manager_uninstall(tinyrt_manager_t *m, const tinyrt_package_id_t *id) {
    if (!m)
        return TINYRT_INVALID_ARGUMENT;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t a;
    tinyrt_status_t r = tinyrt_store_query(m->store, id, &a);
    if (r != TINYRT_OK)
        return r;
    tinyrt_manager_stop(m);
    m->catalog_valid = false;
    r = tinyrt_store_uninstall(m->store, a.id.app_id);
    /* Catalog commit determines uninstall. New installation always clears data
     * again, so a cleanup failure cannot make stale data observable. */
    if (r == TINYRT_OK)
        (void)m->storage.clear(m->storage.ctx, a.id.app_id);
    return r;
}
static int32_t host_get(void *ctx, uint32_t key, int32_t fallback) {
    tinyrt_manager_t *m = ctx;
    return key < 16 && (m->mask & (1u << key)) ? m->values[key] : fallback;
}
static tinyrt_status_t host_set(void *ctx, uint32_t key, int32_t value) {
    tinyrt_manager_t *m = ctx;
    if (key >= 16)
        return TINYRT_INVALID_ARGUMENT;
    m->mask |= 1u << key;
    m->values[key] = value;
    return TINYRT_OK;
}
static uint32_t host_now(void *ctx) {
    tinyrt_manager_t *m = ctx;
    return m->storage.now_ms(m->storage.ctx);
}
static tinyrt_status_t fail_runtime(tinyrt_manager_t *m, tinyrt_status_t r) {
    const char *detail = m->runtime ? tinyrt_runtime_last_error(m->runtime) : NULL;
    snprintf(m->error, sizeof(m->error), "%s (status=%d)", detail && *detail ? detail : "application failed",
             r);
    tinyrt_manager_stop(m);
    return r;
}
static tinyrt_status_t finish_call(tinyrt_manager_t *m, uint32_t mask, const int32_t old[16],
                                   tinyrt_frame_t *out) {
    tinyrt_status_t r = tinyrt_runtime_render(m->runtime, &m->frame);
    if (r == TINYRT_OK && (m->mask != mask || memcmp(m->values, old, sizeof(m->values))))
        r = m->storage.save(m->storage.ctx, m->running.id.app_id, m->mask, m->values);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    *out = m->frame;
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_manager_start(tinyrt_manager_t *m, const tinyrt_package_id_t *id, int32_t width,
                                     int32_t height, tinyrt_frame_t *out) {
    if (!m || !out || width <= 0 || height <= 0)
        return TINYRT_INVALID_ARGUMENT;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t a;
    tinyrt_status_t r = tinyrt_store_query(m->store, id, &a);
    if (r != TINYRT_OK)
        return r;
    tinyrt_manager_stop(m);
    m->error[0] = 0;
    tinyrt_package_metadata_t all[2], meta = {0};
    uint32_t count = 0;
    r = tinyrt_manager_list(m, all, &count);
    if (r != TINYRT_OK)
        return r;
    bool found = false;
    for (uint32_t i = 0; i < count; i++)
        if (!memcmp(&all[i].app.id, &a.id, sizeof(a.id))) {
            meta = all[i];
            found = true;
            break;
        }
    if (!found)
        return TINYRT_NOT_FOUND;
    uint8_t *wasm = malloc(meta.wasm_size);
    if (!wasm)
        return TINYRT_NO_MEMORY;
    r = tinyrt_store_read(m->store, id, meta.wasm_offset, wasm, meta.wasm_size);
    tinyrt_runtime_host_t host = {m, host_get, host_set, host_now};
    if (r == TINYRT_OK)
        r = tinyrt_runtime_create(wasm, meta.wasm_size, &meta.policy, &host, &m->runtime);
    free(wasm);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    m->running = a;
    r = m->storage.load(m->storage.ctx, a.id.app_id, &m->mask, m->values);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    uint32_t old_mask = m->mask;
    int32_t old[16];
    memcpy(old, m->values, sizeof(old));
    r = tinyrt_runtime_init(m->runtime, width, height);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    return finish_call(m, old_mask, old, out);
}
tinyrt_status_t tinyrt_manager_event(tinyrt_manager_t *m, int32_t kind, int32_t x, int32_t y, int32_t arg,
                                     tinyrt_frame_t *out) {
    if (!m || !out)
        return TINYRT_INVALID_ARGUMENT;
    if (!m->runtime)
        return TINYRT_NOT_FOUND;
    uint32_t mask = m->mask;
    int32_t old[16];
    memcpy(old, m->values, sizeof(old));
    tinyrt_status_t r = tinyrt_runtime_event(m->runtime, kind, x, y, arg);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    return finish_call(m, mask, old, out);
}
const char *tinyrt_manager_error(const tinyrt_manager_t *m) {
    return m ? m->error : "invalid manager";
}
