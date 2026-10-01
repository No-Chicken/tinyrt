/* WAMR is owned by one serialized worker; this module never touches LVGL. */
#include "tinyrt_runtime.h"
#include "wasm_export.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union {
#ifdef _MSC_VER
    __declspec(align(16)) unsigned char alignment[16];
#else
    max_align_t alignment;
#endif
    struct { size_t size; } value;
} allocation_t;
static size_t allocated;
static bool initialized, allocation_failed;
static unsigned live_instances;
size_t tinyrt_runtime_memory_used(void) { return allocated; }

static void *allocate(unsigned int n)
{
    if (n > TINYRT_RUNTIME_HEAP_LIMIT || allocated > TINYRT_RUNTIME_HEAP_LIMIT - n) {
        allocation_failed = true;
        return NULL;
    }
    allocation_t *h = calloc(1, sizeof(*h) + n);
    if (!h) { allocation_failed = true; return NULL; }
    h->value.size = n;
    allocated += n;
    return h + 1;
}
static void release(void *p)
{
    if (!p) return;
    allocation_t *h = (allocation_t *)p - 1;
    allocated -= h->value.size;
    free(h);
}
static void *usage_alloc(mem_alloc_usage_t usage, unsigned int n)
{
    (void)usage;
    return allocate(n);
}
static void usage_free(mem_alloc_usage_t usage, void *p)
{
    (void)usage;
    release(p);
}
static void *usage_realloc(mem_alloc_usage_t usage, bool mapped, void *p, unsigned int n)
{
    (void)usage;
    if (mapped) return NULL;
    if (!n) { release(p); return NULL; }
    void *next = allocate(n);
    if (next && p) {
        size_t old_size = ((allocation_t *)p - 1)->value.size;
        memcpy(next, p, old_size < n ? old_size : n);
        release(p);
    }
    return next;
}

struct tinyrt_runtime {
    uint8_t *bytes;
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t env;
    wasm_function_inst_t functions[3];
    tinyrt_package_policy_t policy;
    tinyrt_runtime_host_t host;
    tinyrt_frame_t frame;
    int32_t width, height;
    bool ready, rendering, failed;
    char error[192];
};

static bool utf8(const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t c = p[i++], trailing = 0, minimum = 0;
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
        if (c >= 0xc2 && c <= 0xdf) { c &= 31; trailing = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { c &= 15; trailing = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { c &= 7; trailing = 3; minimum = 0x10000; }
        else return false;
        if (trailing > n - i) return false;
        while (trailing--) {
            if ((p[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (p[i++] & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
static tinyrt_runtime_t *context(wasm_exec_env_t e)
{
    return wasm_runtime_get_user_data(e);
}
static int32_t fail(tinyrt_runtime_t *r, const char *why)
{
    r->failed = true;
    snprintf(r->error, sizeof(r->error), "%s", why);
    wasm_runtime_set_exception(r->instance, why);
    return -1;
}
static bool permission(tinyrt_runtime_t *r, uint32_t required)
{
    if (r->policy.permissions & required) return true;
    fail(r, "host permission denied");
    return false;
}
static tinyrt_draw_command_t *command(tinyrt_runtime_t *r, uint32_t kind)
{
    if (!permission(r, TINYRT_PERMISSION_DRAW)) return NULL;
    if (!r->rendering) { fail(r, "draw outside render"); return NULL; }
    if (r->frame.count >= TINYRT_FRAME_MAX_COMMANDS) {
        fail(r, "frame command limit exceeded"); return NULL;
    }
    if (r->frame.count == 0 && kind != TINYRT_DRAW_CLEAR) {
        fail(r, "frame must start with clear"); return NULL;
    }
    tinyrt_draw_command_t *c = &r->frame.commands[r->frame.count++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    return c;
}
static int32_t draw_clear(wasm_exec_env_t e, uint32_t rgb)
{
    tinyrt_runtime_t *r = context(e);
    tinyrt_draw_command_t *c = command(r, TINYRT_DRAW_CLEAR);
    if (!c) return -1;
    c->rgb = rgb & 0xffffff;
    return 0;
}
static int32_t draw_rect(wasm_exec_env_t e, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
    tinyrt_runtime_t *r = context(e);
    if (x < 0 || y < 0 || w < 0 || h < 0 || x > r->width || y > r->height
        || w > r->width - x || h > r->height - y) return fail(r, "rectangle out of bounds");
    tinyrt_draw_command_t *c = command(r, TINYRT_DRAW_RECT);
    if (!c) return -1;
    c->x = x; c->y = y; c->w = w; c->h = h; c->rgb = rgb & 0xffffff;
    return 0;
}
static int32_t draw_text(wasm_exec_env_t e, int32_t x, int32_t y, uint32_t ptr, uint32_t len, uint32_t rgb)
{
    tinyrt_runtime_t *r = context(e);
    if (x < 0 || y < 0 || x >= r->width || y >= r->height || !len || len > TINYRT_TEXT_MAX_BYTES)
        return fail(r, "text bounds or length invalid");
    if (!wasm_runtime_validate_app_addr(r->instance, ptr, len)) return fail(r, "text memory out of bounds");
    const uint8_t *p = wasm_runtime_addr_app_to_native(r->instance, ptr);
    if (!p || !utf8(p, len)) return fail(r, "text is not displayable UTF-8");
    tinyrt_draw_command_t *c = command(r, TINYRT_DRAW_TEXT);
    if (!c) return -1;
    c->x = x; c->y = y; c->rgb = rgb & 0xffffff;
    memcpy(c->text, p, len);
    return 0;
}
static int32_t kv_get(wasm_exec_env_t e, uint32_t key, int32_t fallback)
{
    tinyrt_runtime_t *r = context(e);
    if (!permission(r, TINYRT_PERMISSION_STORAGE)) return -1;
    if (key >= 16 || !r->host.kv_get) return fail(r, "key or storage callback invalid");
    return r->host.kv_get(r->host.ctx, key, fallback);
}
static int32_t kv_set(wasm_exec_env_t e, uint32_t key, int32_t value)
{
    tinyrt_runtime_t *r = context(e);
    if (!permission(r, TINYRT_PERMISSION_STORAGE)) return -1;
    if (key >= 16 || !r->host.kv_set) return fail(r, "key or storage callback invalid");
    if (r->host.kv_set(r->host.ctx, key, value) != TINYRT_OK) return fail(r, "storage callback failed");
    return 0;
}
static uint32_t now_ms(wasm_exec_env_t e)
{
    tinyrt_runtime_t *r = context(e);
    if (!permission(r, TINYRT_PERMISSION_CLOCK)) return 0;
    if (!r->host.now_ms) { fail(r, "clock callback missing"); return 0; }
    return r->host.now_ms(r->host.ctx);
}
static NativeSymbol natives[] = {
    { "draw_clear", (void *)draw_clear, "(i)i", NULL },
    { "draw_rect", (void *)draw_rect, "(iiiii)i", NULL },
    { "draw_text", (void *)draw_text, "(iiiii)i", NULL },
    { "kv_get", (void *)kv_get, "(ii)i", NULL },
    { "kv_set", (void *)kv_set, "(ii)i", NULL },
    { "now_ms", (void *)now_ms, "()i", NULL }
};
static const unsigned arity[] = { 1, 5, 5, 2, 2, 0 };
static const uint32_t required_permission[] = { 1, 1, 1, 4, 4, 8 };
static const char *exports[] = { "tinyrt_init", "tinyrt_event", "tinyrt_render" };
static const unsigned export_arity[] = { 2, 4, 0 };

tinyrt_status_t tinyrt_runtime_system_init(void)
{
    if (initialized) return TINYRT_OK;
    allocation_failed = false;
    RuntimeInitArgs args = { 0 };
    args.mem_alloc_type = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func = (void *)usage_alloc;
    args.mem_alloc_option.allocator.realloc_func = (void *)usage_realloc;
    args.mem_alloc_option.allocator.free_func = (void *)usage_free;
    args.native_module_name = "tinyrt";
    args.native_symbols = natives;
    args.n_native_symbols = (uint32_t)(sizeof(natives) / sizeof(*natives));
    if (!wasm_runtime_full_init(&args)) return TINYRT_NO_MEMORY;
    initialized = true;
    return TINYRT_OK;
}
void tinyrt_runtime_system_shutdown(void)
{
    if (initialized && !live_instances) {
        wasm_runtime_destroy();
        initialized = false;
    }
}

/* Preflight only reads bounded section envelopes and memory/table limits. The
 * complete instruction/type/data validation is always performed by WAMR. */
typedef struct { const uint8_t *p, *end; } reader_t;
static bool u32(reader_t *r, uint32_t *out)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 5; i++) {
        if (r->p == r->end) return false;
        uint8_t b = *r->p++;
        if (i == 4 && (b & 0xf0)) return false;
        value |= (uint32_t)(b & 127) << (7 * i);
        if (!(b & 128)) { *out = value; return true; }
    }
    return false;
}
static bool policy_valid(const tinyrt_package_policy_t *p)
{
    return p && p->abi_version == 1 && !(p->permissions & ~15u)
        && p->max_memory_pages >= 1 && p->max_memory_pages <= 16
        && p->instruction_budget >= 1 && p->instruction_budget <= 100000;
}
static bool preflight(const uint8_t *bytes, uint32_t size, const tinyrt_package_policy_t *p)
{
    static const uint8_t magic[] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
    if (!policy_valid(p) || size < 8 || size > TINYRT_STORE_SLOT_SIZE || memcmp(bytes, magic, 8)) return false;
    reader_t r = { bytes + 8, bytes + size };
    bool memory_found = false;
    while (r.p < r.end) {
        uint8_t id = *r.p++;
        uint32_t length;
        if (!u32(&r, &length) || length > (size_t)(r.end - r.p)) return false;
        reader_t s = { r.p, r.p + length };
        r.p += length;
        if (id == 8) return false;
        if (id == 5) {
            uint32_t count, flags, initial, maximum;
            if (memory_found || !u32(&s, &count) || count != 1 || !u32(&s, &flags) || flags != 1
                || !u32(&s, &initial) || !u32(&s, &maximum) || initial > maximum
                || !initial || maximum > p->max_memory_pages || s.p != s.end) return false;
            memory_found = true;
        } else if (id == 4) {
            uint32_t count;
            if (!u32(&s, &count) || count > 1) return false;
            if (count) {
                uint32_t flags, initial, maximum;
                if (s.p == s.end || *s.p++ != 0x70 || !u32(&s, &flags) || flags != 1
                    || !u32(&s, &initial) || !u32(&s, &maximum) || initial > maximum || maximum > 1024) return false;
            }
            if (s.p != s.end) return false;
        }
    }
    return memory_found;
}
static bool signature(wasm_func_type_t type, unsigned parameters)
{
    if (wasm_func_type_get_param_count(type) != parameters || wasm_func_type_get_result_count(type) != 1
        || wasm_func_type_get_result_valkind(type, 0) != WASM_I32) return false;
    for (unsigned i = 0; i < parameters; i++)
        if (wasm_func_type_get_param_valkind(type, i) != WASM_I32) return false;
    return true;
}
static bool module_policy(wasm_module_t module, const tinyrt_package_policy_t *policy)
{
    int32_t count = wasm_runtime_get_import_count(module);
    if (count < 0 || count > 7) return false;
    for (int32_t i = 0; i < count; i++) {
        wasm_import_t import;
        memset(&import, 0, sizeof(import));
        wasm_runtime_get_import_type(module, i, &import);
        if (import.kind != WASM_IMPORT_EXPORT_KIND_FUNC || !import.linked || strcmp(import.module_name, "tinyrt")) return false;
        unsigned j;
        for (j = 0; j < sizeof(natives) / sizeof(*natives); j++)
            if (!strcmp(import.name, natives[j].symbol)) break;
        if (j == sizeof(natives) / sizeof(*natives) || !signature(import.u.func_type, arity[j])
            || !(policy->permissions & required_permission[j])) return false;
    }
    unsigned found = 0;
    count = wasm_runtime_get_export_count(module);
    if (count < 0 || count > 64) return false;
    for (int32_t i = 0; i < count; i++) {
        wasm_export_t ex;
        memset(&ex, 0, sizeof(ex));
        wasm_runtime_get_export_type(module, i, &ex);
        if (!strcmp(ex.name, "__post_instantiate") || !strcmp(ex.name, "__wasm_call_ctors") || !strcmp(ex.name, "_initialize")) return false;
        for (unsigned j = 0; j < 3; j++) {
            if (strcmp(ex.name, exports[j])) continue;
            if (ex.kind != WASM_IMPORT_EXPORT_KIND_FUNC || !signature(ex.u.func_type, export_arity[j])) return false;
            found |= 1u << j;
        }
    }
    return found == 7;
}
static tinyrt_status_t load_module(uint8_t *bytes, uint32_t size, const tinyrt_package_policy_t *p,
                                  wasm_module_t *module, char *error, uint32_t error_size)
{
    if (!preflight(bytes, size, p)) return TINYRT_VERIFY_FAILED;
    allocation_failed = false;
    *module = wasm_runtime_load(bytes, size, error, error_size);
    if (!*module) return allocation_failed ? TINYRT_NO_MEMORY : TINYRT_VERIFY_FAILED;
    if (!module_policy(*module, p)) {
        wasm_runtime_unload(*module);
        *module = NULL;
        return TINYRT_VERIFY_FAILED;
    }
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_runtime_validate_wasm(void *ctx, const tinyrt_store_io_t *io,
    uint32_t offset, uint32_t size, const tinyrt_package_policy_t *policy)
{
    (void)ctx;
    if (!initialized || !io || !io->read || !policy_valid(policy)) return TINYRT_INVALID_ARGUMENT;
    if (live_instances) return TINYRT_BUSY;
    if (size < 8 || size > TINYRT_STORE_SLOT_SIZE || offset > UINT32_MAX - size) return TINYRT_VERIFY_FAILED;
    uint8_t *bytes = allocate(size);
    if (!bytes) return TINYRT_NO_MEMORY;
    tinyrt_status_t status = TINYRT_OK;
    for (uint32_t pos = 0; pos < size;) {
        uint32_t n = size - pos;
        if (n > 1024) n = 1024;
        status = io->read(io->ctx, offset + pos, bytes + pos, n);
        if (status != TINYRT_OK) break;
        pos += n;
    }
    wasm_module_t module = NULL;
    char error[192];
    if (status == TINYRT_OK) status = load_module(bytes, size, policy, &module, error, sizeof(error));
    if (module) wasm_runtime_unload(module);
    release(bytes);
    return status;
}
void tinyrt_runtime_destroy(tinyrt_runtime_t *r)
{
    if (!r) return;
    if (r->env) wasm_runtime_destroy_exec_env(r->env);
    if (r->instance) wasm_runtime_deinstantiate(r->instance);
    if (r->module) wasm_runtime_unload(r->module);
    release(r->bytes);
    release(r);
    live_instances--;
}
tinyrt_status_t tinyrt_runtime_create(const void *bytes, uint32_t size,
    const tinyrt_package_policy_t *policy, const tinyrt_runtime_host_t *host, tinyrt_runtime_t **out)
{
    if (!out) return TINYRT_INVALID_ARGUMENT;
    *out = NULL;
    if (!initialized || !bytes || !host || !policy_valid(policy)) return TINYRT_INVALID_ARGUMENT;
    if (live_instances) return TINYRT_BUSY;
    if (!preflight(bytes, size, policy)) return TINYRT_VERIFY_FAILED;
    tinyrt_runtime_t *r = allocate((unsigned int)sizeof(*r));
    if (!r) return TINYRT_NO_MEMORY;
    live_instances++;
    r->policy = *policy;
    r->host = *host;
    r->bytes = allocate(size);
    if (!r->bytes) { tinyrt_runtime_destroy(r); return TINYRT_NO_MEMORY; }
    memcpy(r->bytes, bytes, size);
    tinyrt_status_t status = load_module(r->bytes, size, policy, &r->module, r->error, sizeof(r->error));
    if (status != TINYRT_OK) { tinyrt_runtime_destroy(r); return status; }
    allocation_failed = false;
    r->instance = wasm_runtime_instantiate(r->module, 8192, 0, r->error, sizeof(r->error));
    if (r->instance) r->env = wasm_runtime_create_exec_env(r->instance, 8192);
    if (!r->instance || !r->env) {
        status = allocation_failed ? TINYRT_NO_MEMORY : TINYRT_VERIFY_FAILED;
        tinyrt_runtime_destroy(r);
        return status;
    }
    wasm_runtime_set_user_data(r->env, r);
    for (unsigned i = 0; i < 3; i++) r->functions[i] = wasm_runtime_lookup_function(r->instance, exports[i]);
    *out = r;
    return TINYRT_OK;
}
static tinyrt_status_t invoke(tinyrt_runtime_t *r, unsigned function, uint32_t argc, uint32_t *argv)
{
    if (r->failed) return TINYRT_VERIFY_FAILED;
    wasm_runtime_set_instruction_count_limit(r->env, r->policy.instruction_budget);
    bool ok = wasm_runtime_call_wasm(r->env, r->functions[function], argc, argv);
    if (!ok || r->failed || (int32_t)argv[0] != 0) {
        const char *exception = wasm_runtime_get_exception(r->instance);
        if (!r->failed) snprintf(r->error, sizeof(r->error), "%s", exception ? exception : "guest returned failure");
        r->failed = true;
        return TINYRT_VERIFY_FAILED;
    }
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_runtime_init(tinyrt_runtime_t *r, int32_t width, int32_t height)
{
    if (!r || r->ready || width < 1 || height < 1 || width > 4096 || height > 4096) return TINYRT_INVALID_ARGUMENT;
    r->width = width; r->height = height;
    uint32_t argv[] = { (uint32_t)width, (uint32_t)height };
    tinyrt_status_t status = invoke(r, 0, 2, argv);
    if (status == TINYRT_OK) r->ready = true;
    return status;
}
tinyrt_status_t tinyrt_runtime_event(tinyrt_runtime_t *r, int32_t kind, int32_t x, int32_t y, int32_t arg)
{
    if (!r || !r->ready || (kind != 1 && kind != 2)) return TINYRT_INVALID_ARGUMENT;
    if (r->failed) return TINYRT_VERIFY_FAILED;
    if (kind == 1 && (!(r->policy.permissions & TINYRT_PERMISSION_INPUT) || x < 0 || y < 0 || x >= r->width || y >= r->height)) return TINYRT_INVALID_ARGUMENT;
    if (kind == 2 && !(r->policy.permissions & TINYRT_PERMISSION_CLOCK)) return TINYRT_INVALID_ARGUMENT;
    uint32_t argv[] = { (uint32_t)kind, (uint32_t)x, (uint32_t)y, (uint32_t)arg };
    return invoke(r, 1, 4, argv);
}
tinyrt_status_t tinyrt_runtime_render(tinyrt_runtime_t *r, tinyrt_frame_t *out)
{
    if (!r || !out || !r->ready) return TINYRT_INVALID_ARGUMENT;
    if (r->failed) return TINYRT_VERIFY_FAILED;
    memset(&r->frame, 0, sizeof(r->frame));
    r->rendering = true;
    uint32_t argv[1] = { 0 };
    tinyrt_status_t status = invoke(r, 2, 0, argv);
    r->rendering = false;
    if (status == TINYRT_OK && !r->frame.count) {
        fail(r, "empty frame");
        return TINYRT_VERIFY_FAILED;
    }
    if (status == TINYRT_OK) *out = r->frame;
    return status;
}
const char *tinyrt_runtime_last_error(const tinyrt_runtime_t *r)
{
    return r ? r->error : "invalid runtime";
}
