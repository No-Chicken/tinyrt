/* WAMR is owned by one serialized worker; this module never touches LVGL. */
#include "tinyrt_runtime.h"
#include "wasm_export.h"
#if WASM_ENABLE_AOT != 0
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4244) /* Pinned WAMR inline type accessors. */
#endif
#include "aot_runtime.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
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
static size_t allocated,peak_allocated;
static bool initialized, allocation_failed;
static unsigned live_instances;
size_t tinyrt_runtime_memory_used(void) { return allocated; }
size_t tinyrt_runtime_memory_peak(void) { return peak_allocated; }

#if WASM_ENABLE_AOT != 0
/* Conservative reservation covers Windows allocation granularity and ESP
 * cache-line rounding. Aliases share physical storage and count only once. */
static size_t mapping_charge(uint32_t size) { return ((size_t)size + 65535u) & ~(size_t)65535u; }
static bool mapping_reserve(void *ctx, uint32_t size)
{
    (void)ctx;
    if (!size || size > TINYRT_RUNTIME_HEAP_LIMIT) { allocation_failed=true;return false; }
    size_t charge=mapping_charge(size);
    if (allocated > TINYRT_RUNTIME_HEAP_LIMIT-charge) {
        allocation_failed=true;return false;
    }
    allocated+=charge;
    if(allocated>peak_allocated)peak_allocated=allocated;
    return true;
}
static void mapping_release(void *ctx, uint32_t size) { (void)ctx;allocated-=mapping_charge(size); }
#endif

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
    if(allocated>peak_allocated) peak_allocated=allocated;
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
    wasm_function_inst_t functions[4];
    tinyrt_package_policy_t policy;
    tinyrt_runtime_host_t host;
    tinyrt_execution_guard_t guard;
    tinyrt_frame_t *frame;
    int32_t width, height;
    uint32_t clock_interval_ms, callback, input_events, audio_requests;
    bool ready, failed, stopped, skipped, aot;
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
    if (!r->frame) { fail(r, "draw outside render"); return NULL; }
    if (r->skipped) { fail(r, "draw after skip"); return NULL; }
    if (r->frame->count >= TINYRT_FRAME_MAX_COMMANDS) {
        fail(r, "frame command limit exceeded"); return NULL;
    }
    if (r->frame->count == 0 && kind != TINYRT_DRAW_CLEAR) {
        fail(r, "frame must start with clear"); return NULL;
    }
    if (r->frame->count == 0) r->frame->pixel_bytes = 0;
    tinyrt_draw_command_t *c = &r->frame->commands[r->frame->count++];
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
static bool rectangle(tinyrt_runtime_t *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return x >= 0 && y >= 0 && w > 0 && h > 0 && x < r->width && y < r->height
        && w <= r->width - x && h <= r->height - y;
}
static int32_t draw_round_rect(wasm_exec_env_t e, int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t radius, uint32_t rgb)
{
    tinyrt_runtime_t *r = context(e);
    if (!rectangle(r,x,y,w,h) || radius < 0 || radius > w / 2 || radius > h / 2)
        return fail(r, "rounded rectangle bounds or radius invalid");
    tinyrt_draw_command_t *c = command(r,TINYRT_DRAW_ROUND_RECT);
    if (!c) return -1;
    c->x=x; c->y=y; c->w=w; c->h=h; c->radius=radius; c->rgb=rgb & 0xffffff;
    return 0;
}
static int32_t draw_arc(wasm_exec_env_t e, int32_t x, int32_t y, int32_t radius,
                       int32_t thickness, int32_t start, int32_t end, uint32_t rgb)
{
    tinyrt_runtime_t *r = context(e);
    if (radius <= 0 || thickness <= 0 || thickness > radius || start < 0 || end < start || end > 360
        || (int64_t)x-radius < 0 || (int64_t)y-radius < 0
        || (int64_t)x+radius >= r->width || (int64_t)y+radius >= r->height)
        return fail(r, "arc bounds or style invalid");
    tinyrt_draw_command_t *c = command(r,TINYRT_DRAW_ARC);
    if (!c) return -1;
    c->x=x; c->y=y; c->radius=radius; c->thickness=thickness;
    c->start_angle=start; c->end_angle=end; c->rgb=rgb & 0xffffff;
    return 0;
}
static int32_t draw_text_box(wasm_exec_env_t e, int32_t x, int32_t y, int32_t w, int32_t h,
                            uint32_t ptr, uint32_t len, uint32_t rgb, int32_t font, int32_t align)
{
    tinyrt_runtime_t *r = context(e);
    if (!rectangle(r,x,y,w,h) || !len || len > TINYRT_TEXT_MAX_BYTES
        || (font != 18 && font != 24 && font != 36 && font != 48) || align < 0 || align > 2)
        return fail(r, "text box bounds or style invalid");
    if (!wasm_runtime_validate_app_addr(r->instance,ptr,len)) return fail(r,"text memory out of bounds");
    const uint8_t *p = wasm_runtime_addr_app_to_native(r->instance,ptr);
    if (!p || !utf8(p,len)) return fail(r,"text is not displayable UTF-8");
    tinyrt_draw_command_t *c = command(r,TINYRT_DRAW_TEXT_BOX);
    if (!c) return -1;
    c->x=x; c->y=y; c->w=w; c->h=h; c->font_px=font; c->align=align; c->rgb=rgb & 0xffffff;
    memcpy(c->text,p,len);
    return 0;
}
static int32_t draw_rgb565(wasm_exec_env_t e, int32_t x, int32_t y, int32_t w, int32_t h,
                           uint32_t ptr, uint32_t len)
{
    tinyrt_runtime_t *r=context(e);
    if (!permission(r,TINYRT_PERMISSION_DRAW)) return -1;
    if (!r->frame || r->skipped) return fail(r,"pixels outside drawing render");
    /* Bound dimensions before multiplication; negative i32/large u32 bit
     * patterns must never wrap into a small copy length. */
    if (!rectangle(r,x,y,w,h) || w>256 || h>240 || len!=(uint32_t)w*(uint32_t)h*2u)
        return fail(r,"pixel bounds or length invalid");
    if (r->frame->count && r->frame->pixel_bytes) return fail(r,"one image per frame");
    if (!wasm_runtime_validate_app_addr(r->instance,ptr,len)) return fail(r,"pixel memory out of bounds");
    const uint8_t *p=wasm_runtime_addr_app_to_native(r->instance,ptr);
    if (!p) return fail(r,"pixel memory unavailable");
    tinyrt_draw_command_t *c=command(r,TINYRT_DRAW_RGB565);
    if (!c) return -1;
    c->x=x;c->y=y;c->w=w;c->h=h;
    memcpy(r->frame->pixels,p,len);r->frame->pixel_bytes=len;
    return 0;
}
static int32_t draw_skip(wasm_exec_env_t e)
{
    tinyrt_runtime_t *r=context(e);
    if (!permission(r,TINYRT_PERMISSION_DRAW)) return -1;
    if (!r->frame || r->frame->count || r->skipped) return fail(r,"skip must be the only render operation");
    r->skipped=true;return 0;
}
static int32_t draw_rgb565_scaled(wasm_exec_env_t e,int32_t x,int32_t y,int32_t w,int32_t h,
                                 int32_t sw,int32_t sh,uint32_t ptr,uint32_t len)
{
    tinyrt_runtime_t *r=context(e);
    if (!permission(r,TINYRT_PERMISSION_DRAW)) return -1;
    if (!r->frame || r->skipped) return fail(r,"pixels outside drawing render");
    if (!rectangle(r,x,y,w,h) || sw<1 || sw>256 || sh<1 || sh>240 ||
        len!=(uint32_t)sw*(uint32_t)sh*2u) return fail(r,"scaled pixel bounds or length invalid");
    if (r->frame->count && r->frame->pixel_bytes) return fail(r,"one image per frame");
    if (!wasm_runtime_validate_app_addr(r->instance,ptr,len)) return fail(r,"pixel memory out of bounds");
    const uint8_t *p=wasm_runtime_addr_app_to_native(r->instance,ptr);
    if (!p) return fail(r,"pixel memory missing");
    tinyrt_draw_command_t *c=command(r,TINYRT_DRAW_RGB565_SCALED);
    if (!c) return -1;
    c->x=x;c->y=y;c->w=w;c->h=h;c->source_w=sw;c->source_h=sh;
    memcpy(r->frame->pixels,p,len);r->frame->pixel_bytes=len;
    return 0;
}
static int32_t asset_read(wasm_exec_env_t e,uint32_t offset,uint32_t ptr,uint32_t len)
{
    tinyrt_runtime_t *r=context(e);
    if (r->callback>1 || len>4096 || !r->host.asset_read) return fail(r,"resource read callback or length invalid");
    if (!wasm_runtime_validate_app_addr(r->instance,ptr,len)) return fail(r,"resource destination out of bounds");
    void *p=wasm_runtime_addr_app_to_native(r->instance,ptr);
    if (!p || r->host.asset_read(r->host.ctx,offset,p,len)!=TINYRT_OK) return fail(r,"resource read out of bounds or failed");
    return (int32_t)len;
}
static int32_t audio_play(wasm_exec_env_t e,uint32_t offset,uint32_t length,uint32_t rate)
{
    tinyrt_runtime_t *r=context(e);
    if (!permission(r,TINYRT_PERMISSION_AUDIO)) return -1;
    if (r->callback>1 || !length || (length&1u) || length>TINYRT_AUDIO_MAX_BYTES ||
        rate!=TINYRT_AUDIO_SAMPLE_RATE || offset>UINT32_MAX-length)
        return fail(r,"audio callback, range or format invalid");
    if (r->audio_requests>=4) return 1;
    ++r->audio_requests;
    if (!r->host.audio_play) return 1;
    tinyrt_status_t result=r->host.audio_play(r->host.ctx,offset,length,rate);
    if (result==TINYRT_BUSY) return 1;
    if (result!=TINYRT_OK) return fail(r,"audio resource range or host failed");
    return 0;
}
static int32_t clock_interval(wasm_exec_env_t e, int32_t ms)
{
    tinyrt_runtime_t *r=context(e);
    if (!permission(r,TINYRT_PERMISSION_CLOCK)) return -1;
    if (r->callback>1 || ms<1 || ms>1000) return fail(r,"clock interval or callback invalid");
    r->clock_interval_ms=(uint32_t)ms;return 0;
}
static int32_t kv_get(wasm_exec_env_t e, uint32_t key, int32_t fallback)
{
    tinyrt_runtime_t *r = context(e);
    if (!permission(r, TINYRT_PERMISSION_STORAGE)) return -1;
    if (key >= 16 || !r->host.kv_get) return fail(r, "key or storage callback invalid");
    return r->host.kv_get(r->host.ctx, key, fallback);
}
static int32_t input_events(wasm_exec_env_t e, uint32_t mask)
{
    tinyrt_runtime_t *r=context(e);
    if (!permission(r,TINYRT_PERMISSION_INPUT)) return -1;
    if (r->callback != 0 || (mask != 0 && mask != 56 && mask != 64 && mask != 120))
        return fail(r,"input subscription or callback invalid");
    r->input_events=mask;return 0;
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
/* WAMR sorts this table in place. Keep lexical order so the parallel policy
 * tables retain their association after native registration. */
static int32_t runtime_backend(wasm_exec_env_t e) { return context(e)->aot ? 1 : 0; }
static NativeSymbol natives[] = {
    { "asset_read", (void *)asset_read, "(iii)i", NULL },
    { "audio_play", (void *)audio_play, "(iii)i", NULL },
    { "clock_interval", (void *)clock_interval, "(i)i", NULL },
    { "draw_arc", (void *)draw_arc, "(iiiiiii)i", NULL },
    { "draw_clear", (void *)draw_clear, "(i)i", NULL },
    { "draw_rect", (void *)draw_rect, "(iiiii)i", NULL },
    { "draw_rgb565", (void *)draw_rgb565, "(iiiiii)i", NULL },
    { "draw_rgb565_scaled", (void *)draw_rgb565_scaled, "(iiiiiiii)i", NULL },
    { "draw_round_rect", (void *)draw_round_rect, "(iiiiii)i", NULL },
    { "draw_skip", (void *)draw_skip, "()i", NULL },
    { "draw_text", (void *)draw_text, "(iiiii)i", NULL },
    { "draw_text_box", (void *)draw_text_box, "(iiiiiiiii)i", NULL },
    { "input_events", (void *)input_events, "(i)i", NULL },
    { "kv_get", (void *)kv_get, "(ii)i", NULL },
    { "kv_set", (void *)kv_set, "(ii)i", NULL },
    { "now_ms", (void *)now_ms, "()i", NULL },
    { "runtime_backend", (void *)runtime_backend, "()i", NULL }
};
static const unsigned arity[] = { 3, 3, 1, 7, 1, 5, 6, 8, 6, 0, 5, 9, 1, 2, 2, 0, 0 };
static const uint32_t required_permission[] = { 0, 16, 8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 4, 4, 8, 0 };
static const char *exports[] = { "tinyrt_init", "tinyrt_event", "tinyrt_render", "tinyrt_stop" };
static const unsigned export_arity[] = { 2, 4, 0, 0 };

tinyrt_status_t tinyrt_runtime_system_init(void)
{
    if (initialized) return TINYRT_OK;
    peak_allocated=allocated;
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
#if WASM_ENABLE_AOT != 0
    wasm_runtime_set_aot_mapping_budget(mapping_reserve,mapping_release,NULL);
#endif
    initialized = true;
    return TINYRT_OK;
}
void tinyrt_runtime_system_shutdown(void)
{
    if (initialized && !live_instances) {
        wasm_runtime_destroy();
#if WASM_ENABLE_AOT != 0
        wasm_runtime_set_aot_mapping_budget(NULL,NULL,NULL);
#endif
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
    return p && p->abi_version == 1 && !(p->permissions & ~TINYRT_PERMISSION_ALL)
        && p->max_memory_pages >= 1 && p->max_memory_pages <= 16
        && p->instruction_budget >= 1 && p->instruction_budget <= 100000;
}
static bool preflight(const uint8_t *bytes, uint32_t size, const tinyrt_package_policy_t *p)
{
    static const uint8_t magic[] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
    if (!policy_valid(p) || size < 8 || size > TINYRT_STORE_MAX_PACKAGE_SIZE || memcmp(bytes, magic, 8)) return false;
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
    if (count < 0 || (unsigned)count > sizeof(natives) / sizeof(*natives)) return false;
    for (int32_t i = 0; i < count; i++) {
        wasm_import_t import;
        memset(&import, 0, sizeof(import));
        wasm_runtime_get_import_type(module, i, &import);
        if (import.kind != WASM_IMPORT_EXPORT_KIND_FUNC || !import.linked || strcmp(import.module_name, "tinyrt")) return false;
        unsigned j;
        for (j = 0; j < sizeof(natives) / sizeof(*natives); j++)
            if (!strcmp(import.name, natives[j].symbol)) break;
        if (j == sizeof(natives) / sizeof(*natives) || !signature(import.u.func_type, arity[j])
            || (required_permission[j] && !(policy->permissions & required_permission[j]))) return false;
    }
    unsigned found = 0;
    count = wasm_runtime_get_export_count(module);
    if (count < 0 || count > 64) return false;
    for (int32_t i = 0; i < count; i++) {
        wasm_export_t ex;
        memset(&ex, 0, sizeof(ex));
        wasm_runtime_get_export_type(module, i, &ex);
        if (!strcmp(ex.name, "__post_instantiate") || !strcmp(ex.name, "__wasm_call_ctors") || !strcmp(ex.name, "_initialize")) return false;
        for (unsigned j = 0; j < sizeof(exports) / sizeof(*exports); j++) {
            if (strcmp(ex.name, exports[j])) continue;
            if (ex.kind != WASM_IMPORT_EXPORT_KIND_FUNC || !signature(ex.u.func_type, export_arity[j])) return false;
            found |= 1u << j;
        }
    }
    return (found & 7u) == 7u; /* tinyrt_stop is optional; validate it when present. */
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
bool tinyrt_runtime_aot_supported(void) {
#if WASM_ENABLE_AOT != 0
    return true;
#else
    return false;
#endif
}
static bool guard_valid(const tinyrt_execution_guard_t *g)
{
    return g && g->arm && g->disarm && g->timeout_ms && g->timeout_ms<=TINYRT_CALLBACK_TIMEOUT_MAX_MS;
}
static bool aot_target_valid(const char value[16])
{
    unsigned i=0;
    for(;i<16 && value[i];++i)
        if(!((value[i]>='a' && value[i]<='z') || (value[i]>='0' && value[i]<='9')
             || value[i]=='_' || value[i]=='-'))return false;
    if(!i || i==16)return false;
    for(;i<16;++i)if(value[i])return false;
    return true;
}
static bool aot_config_valid(const tinyrt_runtime_aot_config_t *config)
{
#if WASM_ENABLE_AOT != 0
    uint8_t stack_marker;
    uintptr_t low=(uintptr_t)os_thread_get_stack_boundary(), current=(uintptr_t)&stack_marker;
    if(!low || low>=current || current-low<=WASM_STACK_GUARD_SIZE)return false;
#endif
    if(!tinyrt_runtime_aot_supported() || !config || !config->profile || !config->profile->enabled
       || config->profile->format_version!=5 || !guard_valid(config->guard)
       || !aot_target_valid(config->profile->target_arch) || !aot_target_valid(config->profile->target_cpu))return false;
    for(unsigned i=0;i<sizeof(config->profile->compat_id);++i)if(config->profile->compat_id[i])return true;
    return false;
}
static bool aot_metadata_matches(const tinyrt_runtime_aot_config_t *config,
                                  const tinyrt_package_aot_metadata_t *meta)
{
    const tinyrt_package_aot_profile_t *p=config->profile;
    return meta && meta->format_version==p->format_version && meta->safety_flags==TINYRT_AOT_REQUIRED_FLAGS
        && !memcmp(meta->target_arch,p->target_arch,sizeof(p->target_arch))
        && !memcmp(meta->target_cpu,p->target_cpu,sizeof(p->target_cpu))
        && !memcmp(meta->compat_id,p->compat_id,sizeof(p->compat_id));
}
#if WASM_ENABLE_AOT != 0
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static bool aot_preflight(const uint8_t *bytes,uint32_t size,const tinyrt_package_aot_metadata_t *meta)
{
    /* Relocatable little-endian target info, no unsupported feature flags or
     * XIP borrowing. The WAMR loader also verifies machine/ABI compatibility. */
    static const uint8_t magic[]={0,'a','o','t'};
    return size>=64 && size<=TINYRT_STORE_MAX_PACKAGE_SIZE && !memcmp(bytes,magic,4)
        && le32(bytes+4)==meta->format_version && le32(bytes+8)==0 && le32(bytes+12)==48
        && bytes[20]==1 && bytes[21]==0 && !le32(bytes+32) && !le32(bytes+36)
        && !le32(bytes+40) && !le32(bytes+44) && bytes[63]==0
        && !memcmp(bytes+48,meta->target_arch,16);
}
static bool aot_module_policy(wasm_module_t module,const tinyrt_package_policy_t *policy)
{
    const AOTModule *m=(const AOTModule *)module;
    if(m->import_memory_count || m->import_table_count || m->import_global_count || m->memory_count!=1
       || m->table_count>1 || m->start_func_index!=UINT32_MAX || m->is_indirect_mode)return false;
    const AOTMemory *mem=&m->memories[0];
    if(mem->flags!=1 || !mem->num_bytes_per_page
       || !mem->init_page_count || mem->init_page_count>mem->max_page_count
       || (uint64_t)mem->num_bytes_per_page*mem->max_page_count>(uint64_t)policy->max_memory_pages*65536u)return false;
    if(m->table_count) {
        const AOTTableType *t=&m->tables[0].table_type;
        if(t->flags!=1 || t->elem_type!=0x70 || t->init_size>t->max_size || t->max_size>1024)return false;
    }
    return module_policy(module,policy);
}
#endif
static tinyrt_status_t load_aot_module(uint8_t *bytes,uint32_t size,const tinyrt_package_policy_t *policy,
    const tinyrt_package_aot_metadata_t *meta,wasm_module_t *module,char *error,uint32_t error_size)
{
#if WASM_ENABLE_AOT != 0
    if(!aot_preflight(bytes,size,meta))return TINYRT_VERIFY_FAILED;
    allocation_failed=false;
    *module=wasm_runtime_load(bytes,size,error,error_size);
    if(!*module)return allocation_failed?TINYRT_NO_MEMORY:TINYRT_VERIFY_FAILED;
    if(!aot_module_policy(*module,policy)) {
        wasm_runtime_unload(*module);*module=NULL;return TINYRT_VERIFY_FAILED;
    }
    return TINYRT_OK;
#else
    (void)bytes;(void)size;(void)policy;(void)meta;(void)module;(void)error;(void)error_size;
    return TINYRT_INVALID_ARGUMENT;
#endif
}
tinyrt_status_t tinyrt_runtime_validate_wasm(void *ctx, const tinyrt_store_io_t *io,
    uint32_t offset, uint32_t size, const tinyrt_package_policy_t *policy)
{
    (void)ctx;
    if (!initialized || !io || !io->read || !policy_valid(policy)) return TINYRT_INVALID_ARGUMENT;
    if (live_instances) return TINYRT_BUSY;
    if (size < 8 || size > TINYRT_STORE_MAX_PACKAGE_SIZE || offset > UINT32_MAX - size) return TINYRT_VERIFY_FAILED;
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
tinyrt_status_t tinyrt_runtime_validate_aot(void *ctx,const tinyrt_store_io_t *io,
    uint32_t offset,uint32_t size,const tinyrt_package_policy_t *policy,const tinyrt_package_aot_metadata_t *meta)
{
    const tinyrt_runtime_aot_config_t *config=ctx;
    if(!initialized || !io || !io->read || !policy_valid(policy) || !aot_config_valid(config))return TINYRT_INVALID_ARGUMENT;
    if(live_instances)return TINYRT_BUSY;
    if(size<64 || size>TINYRT_STORE_MAX_PACKAGE_SIZE || offset>UINT32_MAX-size
       || !aot_metadata_matches(config,meta))return TINYRT_VERIFY_FAILED;
    uint8_t *bytes=allocate(size);if(!bytes)return TINYRT_NO_MEMORY;
    tinyrt_status_t status=TINYRT_OK;
    for(uint32_t pos=0;pos<size;) {
        uint32_t n=size-pos;if(n>1024)n=1024;
        status=io->read(io->ctx,offset+pos,bytes+pos,n);if(status!=TINYRT_OK)break;pos+=n;
    }
    wasm_module_t module=NULL;char error[192];
    if(status==TINYRT_OK)status=load_aot_module(bytes,size,policy,meta,&module,error,sizeof(error));
    if(module)wasm_runtime_unload(module);
    release(bytes);return status;
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
static tinyrt_status_t create_runtime(const void *bytes, uint32_t size,
    const tinyrt_package_policy_t *policy, const tinyrt_runtime_host_t *host,
    const tinyrt_runtime_aot_config_t *aot_config,const tinyrt_package_aot_metadata_t *meta,tinyrt_runtime_t **out)
{
    if (!out) return TINYRT_INVALID_ARGUMENT;
    *out = NULL;
    if (!initialized || !bytes || !host || !policy_valid(policy)) return TINYRT_INVALID_ARGUMENT;
    if (live_instances) return TINYRT_BUSY;
    if(aot_config) {
        if(!aot_config_valid(aot_config))return TINYRT_INVALID_ARGUMENT;
        if(!aot_metadata_matches(aot_config,meta) || size<64 || size>TINYRT_STORE_MAX_PACKAGE_SIZE)return TINYRT_VERIFY_FAILED;
    } else if (!preflight(bytes, size, policy)) return TINYRT_VERIFY_FAILED;
    tinyrt_runtime_t *r = allocate((unsigned int)sizeof(*r));
    if (!r) return TINYRT_NO_MEMORY;
    live_instances++;
    r->policy = *policy;
    r->host = *host;
    r->aot=aot_config!=NULL;
    if(aot_config)r->guard=*aot_config->guard;
    r->clock_interval_ms=TINYRT_DEFAULT_CLOCK_INTERVAL_MS;r->callback=UINT32_MAX;
    r->bytes = allocate(size);
    if (!r->bytes) { tinyrt_runtime_destroy(r); return TINYRT_NO_MEMORY; }
    memcpy(r->bytes, bytes, size);
    tinyrt_status_t status = aot_config
        ? load_aot_module(r->bytes,size,policy,meta,&r->module,r->error,sizeof(r->error))
        : load_module(r->bytes, size, policy, &r->module, r->error, sizeof(r->error));
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
    for (unsigned i = 0; i < sizeof(exports) / sizeof(*exports); i++)
        r->functions[i] = wasm_runtime_lookup_function(r->instance, exports[i]);
    *out = r;
    return TINYRT_OK;
}
tinyrt_status_t tinyrt_runtime_create(const void *bytes,uint32_t size,const tinyrt_package_policy_t *policy,
    const tinyrt_runtime_host_t *host,tinyrt_runtime_t **out)
{
    return create_runtime(bytes,size,policy,host,NULL,NULL,out);
}
tinyrt_status_t tinyrt_runtime_create_aot(const void *bytes,uint32_t size,const tinyrt_package_policy_t *policy,
    const tinyrt_runtime_host_t *host,const tinyrt_runtime_aot_config_t *config,
    const tinyrt_package_aot_metadata_t *meta,tinyrt_runtime_t **out)
{
    if(!config) { if(out)*out=NULL;return TINYRT_INVALID_ARGUMENT; }
    return create_runtime(bytes,size,policy,host,config,meta,out);
}
tinyrt_status_t tinyrt_runtime_set_execution_guard(tinyrt_runtime_t *r, const tinyrt_execution_guard_t *guard)
{
    if (!r || r->ready || r->failed || r->callback != UINT32_MAX) return TINYRT_INVALID_ARGUMENT;
    if ((r->aot && !guard) || (guard && !guard_valid(guard))) return TINYRT_INVALID_ARGUMENT;
    if (guard) r->guard = *guard;
    else memset(&r->guard, 0, sizeof(r->guard));
    return TINYRT_OK;
}
void tinyrt_runtime_request_cancel(tinyrt_runtime_t *r)
{
    if (r) wasm_runtime_request_exec_env_termination(r->env);
}
static void cancel_callback(void *ctx)
{
    tinyrt_runtime_request_cancel(ctx);
}
static tinyrt_status_t invoke(tinyrt_runtime_t *r, unsigned function, uint32_t argc, uint32_t *argv)
{
    if (r->failed) return TINYRT_VERIFY_FAILED;
#if WASM_ENABLE_AOT != 0
    if(r->aot) {
        uint8_t stack_marker;
        uintptr_t low=(uintptr_t)os_thread_get_stack_boundary(),current=(uintptr_t)&stack_marker;
        if(!low || low>=current || current-low<=WASM_STACK_GUARD_SIZE) {
            r->failed=true;snprintf(r->error,sizeof(r->error),"native stack boundary unavailable");
            return TINYRT_INVALID_ARGUMENT;
        }
    }
#endif
    if (r->guard.arm) {
        tinyrt_status_t status = r->guard.arm(r->guard.ctx, cancel_callback, r, r->guard.timeout_ms);
        if (status != TINYRT_OK) {
            r->failed = true;
            snprintf(r->error, sizeof(r->error), "callback guard could not start");
            return status;
        }
    }
    if(!r->aot)wasm_runtime_set_instruction_count_limit(r->env, r->policy.instruction_budget);
    r->callback=function;
    r->audio_requests=0;
    bool ok = wasm_runtime_call_wasm(r->env, r->functions[function], argc, argv);
    if (r->guard.disarm) r->guard.disarm(r->guard.ctx);
    r->callback=UINT32_MAX;
    if (wasm_runtime_is_exec_env_terminated(r->env)) {
        r->failed = true;
        snprintf(r->error, sizeof(r->error), "guest callback cancelled");
    }
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
    if (!r || !r->ready || r->stopped || kind < 1 || kind > 6) return TINYRT_INVALID_ARGUMENT;
    if (r->failed) return TINYRT_VERIFY_FAILED;
    if (kind != 2 && !(r->policy.permissions & TINYRT_PERMISSION_INPUT)) return TINYRT_INVALID_ARGUMENT;
    if (kind >= 3 && !(r->input_events & (1u << kind))) return TINYRT_INVALID_ARGUMENT;
    if ((kind == 1 || kind == 3 || kind == 4) && (x < 0 || y < 0 || x >= r->width || y >= r->height)) return TINYRT_INVALID_ARGUMENT;
    if (kind == 6 && (x != 1 || (y != 0 && y != 1) || arg != 0)) return TINYRT_INVALID_ARGUMENT;
    if (kind == 2 && !(r->policy.permissions & TINYRT_PERMISSION_CLOCK)) return TINYRT_INVALID_ARGUMENT;
    uint32_t argv[] = { (uint32_t)kind, (uint32_t)x, (uint32_t)y, (uint32_t)arg };
    return invoke(r, 1, 4, argv);
}
tinyrt_status_t tinyrt_runtime_stop(tinyrt_runtime_t *r)
{
    if (!r) return TINYRT_INVALID_ARGUMENT;
    if (r->failed) return TINYRT_VERIFY_FAILED;
    if (!r->ready) return TINYRT_INVALID_ARGUMENT;
    if (r->stopped) return TINYRT_OK;
    r->stopped = true;
    if (!r->functions[3]) return TINYRT_OK;
    uint32_t argv[1] = { 0 };
    return invoke(r,3,0,argv);
}
tinyrt_status_t tinyrt_runtime_render(tinyrt_runtime_t *r, tinyrt_frame_t *out)
{
    if (!out) return TINYRT_INVALID_ARGUMENT;
    out->count = 0;
    if (!r || !r->ready || r->stopped) return TINYRT_INVALID_ARGUMENT;
    if (r->failed) return TINYRT_VERIFY_FAILED;
    r->skipped = false;
    r->frame = out;
    uint32_t argv[1] = { 0 };
    tinyrt_status_t status = invoke(r, 2, 0, argv);
    r->frame = NULL;
    if (status == TINYRT_OK && !out->count && !r->skipped) {
        fail(r, "empty frame");
        status = TINYRT_VERIFY_FAILED;
    }
    if (status != TINYRT_OK) out->count = 0;
    return status;
}
uint32_t tinyrt_runtime_clock_interval_ms(const tinyrt_runtime_t *r)
{
    return r && r->ready && !r->failed && !r->stopped ? r->clock_interval_ms : TINYRT_DEFAULT_CLOCK_INTERVAL_MS;
}
uint32_t tinyrt_runtime_input_events(const tinyrt_runtime_t *r)
{
    return r && r->ready && !r->failed && !r->stopped ? r->input_events : 0;
}
const char *tinyrt_runtime_last_error(const tinyrt_runtime_t *r)
{
    return r ? r->error : "invalid runtime";
}
