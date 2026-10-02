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
    tinyrt_execution_guard_t guard;
    tinyrt_app_info_t running;
    uint32_t mask;
    int32_t values[16];
    uint32_t durable_mask, last_save_ms;
    int32_t durable_values[16];
    bool save_attempted;
    tinyrt_package_metadata_t catalog[TINYRT_STORE_MAX_APPS];
    tinyrt_app_info_t scratch[TINYRT_STORE_MAX_APPS]; /* Single-owner heap workspace. */
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
static void destroy_runtime(tinyrt_manager_t *m) {
    if (!m)
        return;
    tinyrt_runtime_destroy(m->runtime);
    m->runtime = NULL;
    memset(&m->running, 0, sizeof(m->running));
    m->mask = 0;
    memset(m->values, 0, sizeof(m->values));
    m->durable_mask = 0;
    memset(m->durable_values, 0, sizeof(m->durable_values));
}
tinyrt_status_t tinyrt_manager_set_execution_guard(tinyrt_manager_t *m, const tinyrt_execution_guard_t *guard) {
    if (!m) return TINYRT_INVALID_ARGUMENT;
    if (m->runtime) return TINYRT_BUSY;
    if (guard && (!guard->arm || !guard->disarm || !guard->timeout_ms ||
                  guard->timeout_ms > TINYRT_CALLBACK_TIMEOUT_MAX_MS)) return TINYRT_INVALID_ARGUMENT;
    if (guard) m->guard = *guard;
    else memset(&m->guard, 0, sizeof(m->guard));
    return TINYRT_OK;
}
static tinyrt_status_t persist(tinyrt_manager_t *m, bool force) {
    if (m->mask == m->durable_mask && !memcmp(m->values, m->durable_values, sizeof(m->values)))
        return TINYRT_OK;
    uint32_t now = m->storage.now_ms(m->storage.ctx);
    if (!force && m->save_attempted && (uint32_t)(now - m->last_save_ms) < TINYRT_MANAGER_SAVE_INTERVAL_MS)
        return TINYRT_OK;
    /* Failed attempts consume the same allowance so restarting after a storage
     * failure cannot turn ordinary callbacks into an unbounded retry loop. */
    m->save_attempted = true;
    m->last_save_ms = now;
    tinyrt_status_t r = m->storage.save(m->storage.ctx, m->running.id.app_id, m->mask, m->values);
    if (r == TINYRT_OK) {
        m->durable_mask = m->mask;
        memcpy(m->durable_values, m->values, sizeof(m->values));
    }
    return r;
}
tinyrt_status_t tinyrt_manager_stop(tinyrt_manager_t *m) {
    if (!m)
        return TINYRT_INVALID_ARGUMENT;
    if (!m->runtime)
        return TINYRT_OK;
    tinyrt_status_t r = tinyrt_runtime_stop(m->runtime);
    const char *detail = r == TINYRT_OK ? "stop persistence failed" : tinyrt_runtime_last_error(m->runtime);
    if (r == TINYRT_OK)
        r = persist(m, true);
    if (r != TINYRT_OK)
        snprintf(m->error, sizeof(m->error), "%s (status=%d)", detail && *detail ? detail : "application stop failed", r);
    destroy_runtime(m);
    return r;
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
    (void)tinyrt_manager_stop(m);
    tinyrt_manager_abort(m);
    tinyrt_store_close(m->store);
    free(m);
}
tinyrt_status_t tinyrt_manager_list(tinyrt_manager_t *m, tinyrt_package_metadata_t out[TINYRT_STORE_MAX_APPS], uint32_t *count) {
    if (!m || !out || !count)
        return TINYRT_INVALID_ARGUMENT;
    *count = 0;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t *apps=m->scratch;
    uint32_t n = 0;
    tinyrt_status_t r = tinyrt_store_list(m->store, apps, TINYRT_STORE_MAX_APPS, &n);
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
        uint32_t healthy = 0;
        for (uint32_t i = 0; r == TINYRT_OK && i < n; i++) {
            r = inspect(m, &apps[i], &out[healthy]);
            if (r == TINYRT_VERIFY_FAILED || r == TINYRT_CORRUPT) {
                /* Confirm against immutable committed identity before hiding a
                 * newly damaged app. Never quarantine transient read/resource
                 * failures or inconsistent metadata from a faulty adapter. */
                tinyrt_status_t recheck = tinyrt_store_recheck(m->store, &apps[i].id);
                if (recheck == TINYRT_VERIFY_FAILED) {r = TINYRT_OK;continue;}
                if (recheck != TINYRT_OK) r = recheck;
            }
            if (r == TINYRT_OK) ++healthy;
        }
        if (r == TINYRT_OK) {
            n = healthy;
            if(out!=m->catalog) memcpy(m->catalog, out, n * sizeof(*out));
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
tinyrt_status_t tinyrt_manager_list_quarantined(tinyrt_manager_t *m, tinyrt_app_info_t out[TINYRT_STORE_MAX_APPS], uint32_t *count) {
    if (!m || !out || !count)
        return TINYRT_INVALID_ARGUMENT;
    *count = 0;
    if (m->install)
        return TINYRT_BUSY;
    return tinyrt_store_list_quarantined(m->store, out, TINYRT_STORE_MAX_APPS, count);
}
tinyrt_status_t tinyrt_manager_begin(tinyrt_manager_t *m, const tinyrt_app_info_t *a) {
    if (!m || !a || !a->package_size || a->package_size > TINYRT_STORE_MAX_PACKAGE_SIZE)
        return TINYRT_INVALID_ARGUMENT;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t found, *apps=m->scratch;
    uint32_t count = 0;
    tinyrt_status_t r = tinyrt_store_query(m->store, &a->id, &found);
    if (r == TINYRT_OK)
        return found.package_size == a->package_size ? TINYRT_ALREADY_INSTALLED : TINYRT_CONFLICT;
    if (r != TINYRT_NOT_FOUND && r != TINYRT_VERIFY_FAILED)
        return r;
    r = tinyrt_store_list(m->store, apps, TINYRT_STORE_MAX_APPS, &count);
    if (r != TINYRT_OK)
        return r;
    uint32_t healthy_count = count, quarantined_count = 0;
    r = tinyrt_store_list_quarantined(m->store, apps + count, TINYRT_STORE_MAX_APPS - count, &quarantined_count);
    if (r != TINYRT_OK)
        return r;
    count += quarantined_count;
    bool update = false;
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(apps[i].id.app_id, a->id.app_id)) {
            bool repair = i >= healthy_count && a->package_size == apps[i].package_size &&
                          a->id.version == apps[i].id.version && !memcmp(a->id.sha256, apps[i].id.sha256, 32);
            if (!repair && a->id.version <= apps[i].id.version)
                return TINYRT_CONFLICT;
            update = true;
        }
    if (!update && count >= TINYRT_STORE_MAX_APPS)
        return TINYRT_NO_SPACE;
    r = tinyrt_manager_stop(m);
    if (r != TINYRT_OK)
        return r;
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
    if (r == TINYRT_VERIFY_FAILED) {
        tinyrt_app_info_t *quarantined=m->scratch;uint32_t count = 0;
        r = tinyrt_store_list_quarantined(m->store, quarantined, TINYRT_STORE_MAX_APPS, &count);
        if (r != TINYRT_OK)
            return r;
        r = TINYRT_NOT_FOUND;
        for (uint32_t i = 0; i < count; ++i)
            if (quarantined[i].id.version == id->version && !strcmp(quarantined[i].id.app_id, id->app_id) &&
                !memcmp(quarantined[i].id.sha256, id->sha256, 32)) {
                a = quarantined[i];r = TINYRT_OK;break;
            }
    }
    if (r != TINYRT_OK)
        return r;
    r = tinyrt_manager_stop(m);
    if (r != TINYRT_OK)
        return r;
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
    destroy_runtime(m); /* A poisoned instance must never receive another call. */
    return r;
}
static tinyrt_status_t finish_call(tinyrt_manager_t *m, tinyrt_frame_t *out) {
    tinyrt_status_t r = tinyrt_runtime_render(m->runtime, out);
    if (r == TINYRT_OK)
        r = persist(m, false);
    if (r != TINYRT_OK) {
        out->count = 0;
        return fail_runtime(m, r);
    }
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_manager_start(tinyrt_manager_t *m, const tinyrt_package_id_t *id, int32_t width,
                                     int32_t height, tinyrt_frame_t *out) {
    if (!out)
        return TINYRT_INVALID_ARGUMENT;
    out->count = 0;
    if (!m || width <= 0 || height <= 0)
        return TINYRT_INVALID_ARGUMENT;
    if (m->install)
        return TINYRT_BUSY;
    tinyrt_app_info_t a;
    tinyrt_status_t r = tinyrt_store_query(m->store, id, &a);
    if (r != TINYRT_OK)
        return r;
    r = tinyrt_manager_stop(m);
    if (r != TINYRT_OK)
        return r;
    m->error[0] = 0;
    tinyrt_package_metadata_t *all=m->catalog, meta = {0};
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
    if (!found) {
        /* Listing may have just discovered corruption and quarantined the
         * requested identity. Preserve that diagnostic for direct launches. */
        r = tinyrt_store_query(m->store, id, &a);
        return r == TINYRT_OK ? TINYRT_VERIFY_FAILED : r;
    }
    bool aot=meta.execution_kind==TINYRT_PACKAGE_EXEC_AOT;
    if(!aot && meta.execution_kind!=TINYRT_PACKAGE_EXEC_WASM)return TINYRT_VERIFY_FAILED;
    if(aot && (!m->guard.arm || !m->verifier.aot_profile || !m->verifier.aot_profile->enabled))
        return TINYRT_INVALID_ARGUMENT;
    uint32_t module_size=aot?meta.aot_size:meta.wasm_size;
    uint32_t module_offset=aot?meta.aot_offset:meta.wasm_offset;
    uint8_t *module = malloc(module_size);
    if (!module)
        return TINYRT_NO_MEMORY;
    r = tinyrt_store_read(m->store, id, module_offset, module, module_size);
    tinyrt_runtime_host_t host = {m, host_get, host_set, host_now};
    if (r == TINYRT_OK) {
        if(aot) {
            tinyrt_runtime_aot_config_t config={m->verifier.aot_profile,&m->guard};
            r=tinyrt_runtime_create_aot(module,module_size,&meta.policy,&host,&config,&meta.aot,&m->runtime);
        } else r = tinyrt_runtime_create(module, module_size, &meta.policy, &host, &m->runtime);
    }
    free(module);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    if (m->guard.arm) {
        r = tinyrt_runtime_set_execution_guard(m->runtime, &m->guard);
        if (r != TINYRT_OK) return fail_runtime(m, r);
    }
    m->running = a;
    r = m->storage.load(m->storage.ctx, a.id.app_id, &m->mask, m->values);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    m->durable_mask = m->mask;
    memcpy(m->durable_values, m->values, sizeof(m->values));
    r = tinyrt_runtime_init(m->runtime, width, height);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    return finish_call(m, out);
}
tinyrt_status_t tinyrt_manager_event(tinyrt_manager_t *m, int32_t kind, int32_t x, int32_t y, int32_t arg,
                                     tinyrt_frame_t *out) {
    if (!out)
        return TINYRT_INVALID_ARGUMENT;
    out->count = 0;
    if (!m)
        return TINYRT_INVALID_ARGUMENT;
    if (!m->runtime)
        return TINYRT_NOT_FOUND;
    tinyrt_status_t r = tinyrt_runtime_event(m->runtime, kind, x, y, arg);
    if (r != TINYRT_OK)
        return fail_runtime(m, r);
    return finish_call(m, out);
}
const char *tinyrt_manager_error(const tinyrt_manager_t *m) {
    return m ? m->error : "invalid manager";
}

tinyrt_status_t tinyrt_manager_generation(tinyrt_manager_t *m,uint64_t *out) {
    if(!out) return TINYRT_INVALID_ARGUMENT;
    *out=0;
    return m ? tinyrt_store_generation(m->store,out) : TINYRT_INVALID_ARGUMENT;
}

tinyrt_status_t tinyrt_manager_stats(tinyrt_manager_t *m,tinyrt_store_stats_t *out) {
    return tinyrt_store_stats(m?m->store:NULL,out);
}

uint32_t tinyrt_manager_clock_interval_ms(const tinyrt_manager_t *m) {
    return tinyrt_runtime_clock_interval_ms(m?m->runtime:NULL);
}
uint32_t tinyrt_manager_input_events(const tinyrt_manager_t *m) {
    return tinyrt_runtime_input_events(m?m->runtime:NULL);
}

size_t tinyrt_manager_memory_used(const tinyrt_manager_t *m) {
    return m?tinyrt_runtime_memory_used():0;
}
size_t tinyrt_manager_memory_peak(const tinyrt_manager_t *m) {
    return m?tinyrt_runtime_memory_peak():0;
}
