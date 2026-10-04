/* Portable owned resources and deterministic RGB565 raster records. */
#include "tinyrt_runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <windows.h>
typedef volatile LONG refs_t;
static void ref_add(refs_t *p) { InterlockedIncrement(p); }
static int ref_drop(refs_t *p) { return InterlockedDecrement(p) == 0; }
static int ref_count(refs_t *p) {
  return (int)InterlockedCompareExchange(p, 0, 0);
}
#else
#include <stdatomic.h>
typedef atomic_uint refs_t;
static void ref_add(refs_t *p) { atomic_fetch_add(p, 1); }
static int ref_drop(refs_t *p) { return atomic_fetch_sub(p, 1) == 1; }
static int ref_count(refs_t *p) { return (int)atomic_load(p); }
#endif
typedef struct {
  refs_t refs;
  uint32_t w, h, format, len;
  tinyrt_gfx_free_fn release;
  uint8_t data[];
} texture_t;
struct tinyrt_gfx_resources {
  refs_t refs;
  tinyrt_gfx_alloc_fn allocate;
  tinyrt_gfx_free_fn release;
  uint32_t bytes;
  texture_t *textures[32];
  uint16_t palettes[16][256];
  uint32_t palette_valid;
};
/* Independent graphics pool includes all immutable snapshots and metadata.
 * It has a hard process-wide ceiling in addition to the session texture cap. */
static refs_t pool_bytes;
#define GFX_POOL_LIMIT (1152u * 1024u)
typedef union {
#ifdef _MSC_VER
  __declspec(align(16)) unsigned char alignment[16];
#else
  max_align_t alignment;
#endif
  struct {
    unsigned int bytes;
  } value;
} pool_header_t;
static int pool_reserve(unsigned int n) {
#ifdef _MSC_VER
  LONG old;
  do {
    old = InterlockedCompareExchange(&pool_bytes, 0, 0);
    if (n > GFX_POOL_LIMIT - (unsigned int)old)
      return 0;
  } while (InterlockedCompareExchange(&pool_bytes, old + (LONG)n, old) != old);
#else
  unsigned int old = atomic_load(&pool_bytes);
  do {
    if (n > GFX_POOL_LIMIT - old)
      return 0;
  } while (!atomic_compare_exchange_weak(&pool_bytes, &old, old + n));
#endif
  return 1;
}
static void pool_unreserve(unsigned int n) {
#ifdef _MSC_VER
  InterlockedExchangeAdd(&pool_bytes, -(LONG)n);
#else
  atomic_fetch_sub(&pool_bytes, n);
#endif
}
size_t tinyrt_gfx_memory_used(void) { return (size_t)ref_count(&pool_bytes); }
static void *default_alloc(unsigned int n) {
  if (n > GFX_POOL_LIMIT - sizeof(pool_header_t))
    return NULL;
  unsigned int charge = n + (unsigned int)sizeof(pool_header_t);
  if (!pool_reserve(charge))
    return NULL;
  pool_header_t *h = calloc(1, charge);
  if (!h) {
    pool_unreserve(charge);
    return NULL;
  }
  h->value.bytes = charge;
  return h + 1;
}
static void default_free(void *p) {
  if (!p)
    return;
  pool_header_t *h = (pool_header_t *)p - 1;
  unsigned int n = h->value.bytes;
  free(h);
  pool_unreserve(n);
}
static void tex_drop(texture_t *t) {
  if (t && ref_drop(&t->refs))
    t->release(t);
}
tinyrt_gfx_resources_t *tinyrt_gfx_resources_create(tinyrt_gfx_alloc_fn a,
                                                    tinyrt_gfx_free_fn f) {
  if ((!a) != (!f))
    return NULL;
  if (!a) {
    a = default_alloc;
    f = default_free;
  }
  tinyrt_gfx_resources_t *r = a((unsigned int)sizeof(*r));
  if (!r)
    return NULL;
  memset(r, 0, sizeof(*r));
  r->refs = 1;
  r->allocate = a;
  r->release = f;
  return r;
}
void tinyrt_gfx_resources_retain(tinyrt_gfx_resources_t *r) {
  if (r)
    ref_add(&r->refs);
}
void tinyrt_gfx_resources_release(tinyrt_gfx_resources_t *r) {
  if (!r || !ref_drop(&r->refs))
    return;
  for (unsigned i = 0; i < 32; i++)
    tex_drop(r->textures[i]);
  r->release(r);
}
static int writable(tinyrt_gfx_resources_t **rp) {
  tinyrt_gfx_resources_t *r = *rp;
  if (!r)
    return -1;
  if (ref_count(&r->refs) == 1)
    return 0;
  tinyrt_gfx_resources_t *n = r->allocate((unsigned int)sizeof(*n));
  if (!n)
    return -1;
  memcpy((uint8_t *)n + sizeof(n->refs), (const uint8_t *)r + sizeof(r->refs),
         sizeof(*n) - sizeof(n->refs));
  n->refs = 1;
  for (unsigned i = 0; i < 32; i++)
    if (n->textures[i])
      ref_add(&n->textures[i]->refs);
  *rp = n;
  tinyrt_gfx_resources_release(r);
  return 0;
}
int tinyrt_gfx_texture_upload(tinyrt_gfx_resources_t **rp, uint32_t slot,
                              uint32_t fmt, uint32_t w, uint32_t h,
                              const void *src, uint32_t len) {
  if (!rp || !*rp || slot >= 32 || (fmt != 1 && fmt != 2) || !w || !h ||
      w > 512 || h > 512 || !src || len != w * h * (fmt == 2 ? 2u : 1u))
    return -1;
  tinyrt_gfx_resources_t *r = *rp;
  texture_t *old = r->textures[slot];
  uint32_t oldlen = old ? old->len : 0;
  if (len > TINYRT_GFX_RESOURCE_BYTES ||
      r->bytes - oldlen > TINYRT_GFX_RESOURCE_BYTES - len)
    return -1;
  texture_t *n = r->allocate((unsigned int)sizeof(*n) + len);
  if (!n)
    return -1;
  n->refs = 1;
  n->w = w;
  n->h = h;
  n->format = fmt;
  n->len = len;
  n->release = r->release;
  memcpy(n->data, src, len);
  if (writable(rp)) {
    tex_drop(n);
    return -1;
  }
  r = *rp;
  old = r->textures[slot];
  r->textures[slot] = n;
  r->bytes = r->bytes - oldlen + len;
  tex_drop(old);
  return 0;
}
int tinyrt_gfx_texture_rows(tinyrt_gfx_resources_t **rp, uint32_t slot,
                            uint32_t y, uint32_t rows, const void *src,
                            uint32_t len) {
  if (!rp || !*rp || slot >= 32 || !src)
    return -1;
  tinyrt_gfx_resources_t *r = *rp;
  texture_t *t = r->textures[slot];
  if (!t || !rows || y > t->h || rows > t->h - y ||
      len != rows * t->w * (t->format == 2 ? 2u : 1u))
    return -1;
  texture_t *n = r->allocate((unsigned int)sizeof(*n) + t->len);
  if (!n)
    return -1;
  memcpy((uint8_t *)n + sizeof(n->refs), (const uint8_t *)t + sizeof(t->refs),
         sizeof(*n) + t->len - sizeof(n->refs));
  n->refs = 1;
  memcpy(n->data + y * t->w * (t->format == 2 ? 2u : 1u), src, len);
  if (writable(rp)) {
    tex_drop(n);
    return -1;
  }
  r = *rp;
  t = r->textures[slot];
  r->textures[slot] = n;
  tex_drop(t);
  return 0;
}
int tinyrt_gfx_palette_upload(tinyrt_gfx_resources_t **rp, uint32_t slot,
                              uint32_t first, uint32_t n, const void *src) {
  if (!rp || !*rp || slot >= 16 || first >= 256 || !n || n > 256 - first ||
      !src)
    return -1;
  if (writable(rp))
    return -1;
  tinyrt_gfx_resources_t *r = *rp;
  const uint8_t *p = src;
  for (uint32_t i = 0; i < n; i++)
    r->palettes[slot][first + i] =
        (uint16_t)(p[2 * i] | ((uint32_t)p[2 * i + 1] << 8));
  r->palette_valid |= 1u << slot;
  return 0;
}
int tinyrt_gfx_texture_free(tinyrt_gfx_resources_t **rp, uint32_t slot) {
  if (!rp || !*rp || slot >= 32)
    return -1;
  if (writable(rp))
    return -1;
  tinyrt_gfx_resources_t *r = *rp;
  texture_t *t = r->textures[slot];
  if (t)
    r->bytes -= t->len;
  r->textures[slot] = NULL;
  tex_drop(t);
  return 0;
}
static uint32_t u32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static uint32_t u16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static int coord(uint32_t n) {
  return (int32_t)n >= -32768 && (int32_t)n <= 32767;
}
static int rect(const uint8_t *p) {
  return coord(u32(p)) && coord(u32(p + 4)) && u32(p + 8) > 0 &&
         u32(p + 8) <= 32767 && u32(p + 12) > 0 && u32(p + 12) <= 32767;
}
static int pal(const tinyrt_gfx_resources_t *r, uint32_t s) {
  return r && s < 16 && (r->palette_valid & (1u << s));
}
static int sprite(const uint8_t *p, const tinyrt_gfx_resources_t *r) {
  uint32_t s = u32(p + 16);
  if (!rect(p) || !r || s >= 32 || !r->textures[s])
    return 0;
  texture_t *t = r->textures[s];
  uint32_t u = u32(p + 20), v = u32(p + 24), w = u32(p + 28), h = u32(p + 32),
           flags = u32(p + 36);
  return w && h && u <= t->w && v <= t->h && w <= t->w - u && h <= t->h - v &&
         !(flags & ~31u) && (t->format == 2 || pal(r, u32(p + 40)));
}
static int valid_utf8(const uint8_t *p, uint32_t n) {
  for (uint32_t i = 0; i < n;) {
    uint32_t c = p[i++], k = 0, min = 0;
    if (c < 128) {
      if (c < 32 || c == 127)
        return 0;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      k = 1;
      min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      k = 2;
      min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      k = 3;
      min = 65536;
    } else
      return 0;
    if (k > n - i)
      return 0;
    while (k--) {
      if ((p[i] & 192) != 128)
        return 0;
      c = (c << 6) | (p[i++] & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return 0;
  }
  return 1;
}
int tinyrt_gfx_validate(const void *data, uint32_t len,
                        const tinyrt_gfx_resources_t *r, uint32_t *count,
                        uint32_t *error) {
  uint32_t off = 0, n = 0;
  if (count)
    *count = 0;
  if (error)
    *error = 0;
  if (!data || !len || len > TINYRT_GFX_MAX_BYTES || (len & 3))
    return -1;
  const uint8_t *b = data;
  while (off < len) {
    if (error)
      *error = n;
    if (len - off < 4 || n >= 4096)
      return -1;
    uint32_t op = u16(b + off), sz = u16(b + off + 2);
    const uint8_t *p = b + off + 4;
    uint32_t a = 0;
    if (sz < 4 || (sz & 3) || sz > len - off)
      return -1;
    switch (op) {
    case TINYRT_GFX_CLEAR:
      if (sz != 8 || u32(p) > 65535)
        return -1;
      break;
    case TINYRT_GFX_RECT:
      if (sz != 28 || !rect(p) || u32(p + 16) > 65535 || u32(p + 20) > 255)
        return -1;
      break;
    case TINYRT_GFX_ROUND_RECT:
      if (sz != 28 || !rect(p) || u32(p + 16) > u32(p + 8) / 2 ||
          u32(p + 16) > u32(p + 12) / 2 || u32(p + 20) > 65535)
        return -1;
      break;
    case TINYRT_GFX_ARC:
      if (sz != 32 || !coord(u32(p)) || !coord(u32(p + 4)) || !u32(p + 8) ||
          u32(p + 8) > 32767 || !u32(p + 12) || u32(p + 12) > u32(p + 8) ||
          u32(p + 16) > u32(p + 20) || u32(p + 20) > 360 || u32(p + 24) > 65535)
        return -1;
      break;
    case TINYRT_GFX_SPRITE:
    case TINYRT_GFX_SOLID_SPRITE:
    case TINYRT_GFX_ADD_SPRITE:
      if (sz != 52 || !sprite(p, r) || u32(p + 44) > 65535)
        return -1;
      break;
    case TINYRT_GFX_GRID:
      if (sz < 40 || !coord(u32(p)) || !coord(u32(p + 4)) || !u32(p + 8) ||
          !u32(p + 12) || u32(p + 8) > 512 || u32(p + 12) > 512 ||
          !u32(p + 16) || !u32(p + 20) || u32(p + 16) > 32767 / u32(p + 8) ||
          u32(p + 20) > 32767 / u32(p + 12) || u32(p + 24) >= u32(p + 16) ||
          u32(p + 24) >= u32(p + 20) || !pal(r, u32(p + 28)) ||
          (u32(p + 32) & ~1u))
        return -1;
      a = u32(p + 8) * u32(p + 12);
      if (sz != 40 + ((a + 3) & ~3u))
        return -1;
      break;
    case TINYRT_GFX_TEXT:
      if (sz < 36 || !rect(p) ||
          (u32(p + 16) != 18 && u32(p + 16) != 24 && u32(p + 16) != 36 &&
           u32(p + 16) != 48) ||
          u32(p + 20) > 2 || u32(p + 24) > 65535 || !u32(p + 28) ||
          u32(p + 28) > 63)
        return -1;
      a = u32(p + 28);
      if (sz != 36 + ((a + 3) & ~3u) || !valid_utf8(p + 32, a))
        return -1;
      break;
    case TINYRT_GFX_CLIP:
      if (sz != 20 || (!rect(p) && u32(p + 8) != 0))
        return -1;
      break;
    case TINYRT_GFX_SET_PAL:
      if (sz != 8 || !pal(r, u32(p)))
        return -1;
      break;
    case TINYRT_GFX_SPRITE_BATCH:
      /* tex,sw,sh,flags,pal,count followed by count*(x,y,u,v), no scaling. */
      if (sz < 28 || !r || u32(p) >= 32 || !r->textures[u32(p)] ||
          !u32(p + 4) || !u32(p + 8) || (u32(p + 12) & ~31u) || !u32(p + 20) ||
          u32(p + 20) > 1024 || sz != 28 + 16 * u32(p + 20))
        return -1;
      for (uint32_t j = 0; j < u32(p + 20); j++) {
        uint8_t q[48] = {0};
        const uint8_t *v = p + 24 + 16 * j;
        memcpy(q, v, 8);
        memcpy(q + 8, p + 4, 8);
        memcpy(q + 16, p, 4);
        memcpy(q + 20, v + 8, 8);
        memcpy(q + 28, p + 4, 8);
        memcpy(q + 36, p + 12, 8);
        if (!sprite(q, r))
          return -1;
      }
      break;
    case TINYRT_GFX_TILEMAP:
      /* x,y,cols,rows,tile_w,tile_h,tex,scroll_x,scroll_y,flags,pal, then u8
       * ids. */
      if (sz < 48 || !r || !coord(u32(p)) || !coord(u32(p + 4)) ||
          !u32(p + 8) || !u32(p + 12) || u32(p + 8) > 512 ||
          u32(p + 12) > 512 || !u32(p + 16) || !u32(p + 20) ||
          u32(p + 16) > 32767 / u32(p + 8) ||
          u32(p + 20) > 32767 / u32(p + 12) || u32(p + 24) >= 32 ||
          !r->textures[u32(p + 24)] || !coord(u32(p + 28)) ||
          !coord(u32(p + 32)) || (u32(p + 36) & ~1u))
        return -1;
      {
        texture_t *t = r->textures[u32(p + 24)];
        uint32_t tw = u32(p + 16), th = u32(p + 20);
        if (tw > t->w || th > t->h || t->w % tw || t->h % th ||
            (t->format == 1 && !pal(r, u32(p + 40))))
          return -1;
        a = u32(p + 8) * u32(p + 12);
        if (sz != 48 + ((a + 3) & ~3u))
          return -1;
        for (uint32_t j = 0; j < a; j++)
          if (p[44 + j] >= (t->w / tw) * (t->h / th))
            return -1;
      }
      break;
    default:
      return -2;
    }
    off += sz;
    n++;
  }
  if (count)
    *count = n;
  return 0;
}
static uint64_t coverage(int64_t x, int64_t y, uint64_t w, uint64_t h,
                         uint32_t width, uint32_t height) {
  int64_t x1 = x + (int64_t)w, y1 = y + (int64_t)h;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x1 > width)
    x1 = width;
  if (y1 > height)
    y1 = height;
  if (x >= x1 || y >= y1)
    return 0;
  return (uint64_t)(x1 - x) * (uint64_t)(y1 - y);
}
uint64_t tinyrt_gfx_work(const void *data, uint32_t len, uint32_t width,
                         uint32_t height, uint32_t scale) {
  const uint8_t *b = data;
  uint64_t work = 0;
  for (uint32_t off = 0; off < len;) {
    uint32_t op = u16(b + off), sz = u16(b + off + 2);
    const uint8_t *p = b + off + 4;
    if (op == TINYRT_GFX_CLEAR)
      work += (uint64_t)width * height;
    else if (op == TINYRT_GFX_SPRITE_BATCH) {
      for (uint32_t i = 0; i < u32(p + 20); i++) {
        const uint8_t *a = p + 24 + i * 16;
        work += coverage((int64_t)(int32_t)u32(a) * scale,
                         (int64_t)(int32_t)u32(a + 4) * scale,
                         (uint64_t)u32(p + 4) * scale,
                         (uint64_t)u32(p + 8) * scale, width, height);
      }
    } else if (op != TINYRT_GFX_CLIP && op != TINYRT_GFX_SET_PAL) {
      int64_t x = (int32_t)u32(p), y = (int32_t)u32(p + 4);
      uint64_t w = u32(p + 8), h = u32(p + 12);
      if (op == TINYRT_GFX_GRID || op == TINYRT_GFX_TILEMAP) {
        w *= u32(p + 16);
        h *= u32(p + 20);
        if (op == TINYRT_GFX_TILEMAP) {
          x -= (int32_t)u32(p + 28);
          y -= (int32_t)u32(p + 32);
        }
      } else if (op == TINYRT_GFX_ARC) {
        x -= (int32_t)u32(p + 8);
        y -= (int32_t)u32(p + 8);
        w = h = 2u * u32(p + 8);
      }
      work +=
          coverage(x * scale, y * scale, w * scale, h * scale, width, height);
    }
    off += sz;
  }
  return work;
}
void tinyrt_frame_release(tinyrt_frame_t *f) {
  if (!f)
    return;
  tinyrt_gfx_resources_release(f->gfx_resources);
  f->gfx_resources = NULL;
  f->gfx_bytes = 0;
}
void tinyrt_gfx_damage_merge(tinyrt_frame_t *f, const tinyrt_gfx_damage_t *r) {
  if (!f || !r || r->w <= 0 || r->h <= 0)
    return;
  if (f->damage_count < 8) {
    f->damage[f->damage_count++] = *r;
    return;
  }
  int32_t x = r->x, y = r->y, x1 = r->x + r->w, y1 = r->y + r->h;
  for (unsigned i = 0; i < 8; i++) {
    tinyrt_gfx_damage_t *d = &f->damage[i];
    if (d->x < x)
      x = d->x;
    if (d->y < y)
      y = d->y;
    if (d->x + d->w > x1)
      x1 = d->x + d->w;
    if (d->y + d->h > y1)
      y1 = d->y + d->h;
  }
  f->damage_count = 1;
  f->damage[0] = (tinyrt_gfx_damage_t){x, y, x1 - x, y1 - y};
}
static uint16_t blend565(uint16_t d, uint16_t s, uint32_t a) {
  uint32_t r =
      (((s >> 11) & 31) * a + ((d >> 11) & 31) * (255 - a) + 127) / 255;
  uint32_t g = (((s >> 5) & 63) * a + ((d >> 5) & 63) * (255 - a) + 127) / 255;
  uint32_t b = ((s & 31) * a + (d & 31) * (255 - a) + 127) / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
static uint16_t add565(uint16_t d, uint16_t s) {
  uint32_t r = (d >> 11) + (s >> 11), g = ((d >> 5) & 63) + ((s >> 5) & 63),
           b = (d & 31) + (s & 31);
  return (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) |
                    (b > 31 ? 31 : b));
}
typedef struct {
  uint16_t *dst;
  uint32_t stride, width, height, y, rows, scale;
  tinyrt_gfx_damage_t clip;
  const tinyrt_gfx_resources_t *resources;
  tinyrt_gfx_text_fn text;
  void *ctx;
  int reference;
} render_t;
static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }
/* Wide fills use aligned native words without aliasing a uint16_t object. */
static void fill_color(uint16_t *dst, uint32_t n, uint16_t color) {
  if (n && ((uintptr_t)dst & 3u)) {
    *dst++ = color;
    --n;
  }
  uint32_t word = (uint32_t)color | ((uint32_t)color << 16);
  uint32_t block[4] = {word, word, word, word};
#if defined(__GNUC__) || defined(__clang__)
  dst = __builtin_assume_aligned(dst, 4);
#endif
  while (n >= 8) {
    memcpy(dst, block, 16);
    dst += 8;
    n -= 8;
  }
  while (n >= 2) {
    memcpy(dst, &word, 4);
    dst += 2;
    n -= 2;
  }
  if (n)
    *dst = color;
}
static void paint_grid(render_t *v, const uint8_t *p, const uint8_t *indices) {
  uint32_t cols = u32(p + 8), rows = u32(p + 12), cw = u32(p + 16),
           ch = u32(p + 20), gap = u32(p + 24), k = v->scale;
  int x = (int32_t)u32(p) * (int)k, y = (int32_t)u32(p + 4) * (int)k;
  int clipx0 = max_i(v->clip.x, 0),
      clipx1 = min_i(v->clip.x + v->clip.w, (int)v->width);
  int clipy0 = max_i(v->clip.y, (int)v->y),
      clipy1 = min_i(v->clip.y + v->clip.h, (int)(v->y + v->rows));
  const uint16_t *palette = v->resources->palettes[u32(p + 28)];
  uint32_t circle = u32(p + 32) & 1u;
  int64_t diameter = min_i((int)(cols * cw), (int)(rows * ch));
  for (uint32_t r = 0; r < rows; r++) {
    int top = y + (int)(r * ch * k), bottom = top + (int)((ch - gap) * k);
    if (top >= clipy1)
      break;
    if (bottom <= clipy0)
      continue;
    int y0 = max_i(top, clipy0), y1 = min_i(bottom, clipy1);
    int64_t dy = (int64_t)(2 * r + 1) * ch - (int64_t)rows * ch;
    for (uint32_t c = 0; c < cols; c++) {
      uint8_t index = indices[r * cols + c];
      if (!index)
        continue;
      int left = x + (int)(c * cw * k), right = left + (int)((cw - gap) * k);
      if (left >= clipx1)
        break;
      if (right <= clipx0)
        continue;
      if (circle) {
        int64_t dx = (int64_t)(2 * c + 1) * cw - (int64_t)cols * cw;
        if (dx * dx + dy * dy > diameter * diameter)
          continue;
      }
      int x0 = max_i(left, clipx0), x1 = min_i(right, clipx1);
      uint16_t color = palette[index];
      for (int yy = y0; yy < y1; yy++) {
        uint16_t *dst = v->dst + (uint32_t)(yy - (int)v->y) * v->stride;
        fill_color(dst + x0, (uint32_t)(x1 - x0), color);
      }
    }
  }
}
#if defined(_MSC_VER)
#define TINYRT_GFX_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define TINYRT_GFX_NOINLINE __attribute__((noinline))
#else
#define TINYRT_GFX_NOINLINE
#endif
/* The first and last clipped sample may have only one destination pixel.
 * Whole pairs use direct stores, with no tiny fill calls or offset array. */
static TINYRT_GFX_NOINLINE void expand_index2_row(
    uint16_t *dst, const uint8_t *src, uint32_t count, uint32_t leading,
    int step, const uint16_t *palette, int transparent) {
  int offset = 0;
  if (leading) {
    uint8_t index = src[0];
    if (index || !transparent) *dst = palette[index];
    dst++;
    count--;
    offset += step;
  }
  while (count >= 2) {
    uint8_t index = src[offset];
    if (index || !transparent) {
      uint16_t color = palette[index];
      dst[0] = color;
      dst[1] = color;
    }
    dst += 2;
    count -= 2;
    offset += step;
  }
  if (count) {
    uint8_t index = src[offset];
    if (index || !transparent) *dst = palette[index];
  }
}
/* Kept separate from the 512-entry fallback map: the target UI stack may be
 * external memory, and a large frame makes hot-loop spills much more costly. */
static TINYRT_GFX_NOINLINE int paint_integer_sprite(render_t *v, uint32_t op, const uint8_t *p) {
  uint32_t flags = u32(p + 36), rot = (flags >> 3) & 3, sw = u32(p + 28),
           sh = u32(p + 32), k = v->scale;
  texture_t *t = v->resources->textures[u32(p + 16)];
  uint32_t rw = (rot & 1) ? sh : sw, rh = (rot & 1) ? sw : sh;
  int x = (int32_t)u32(p) * (int)k, y = (int32_t)u32(p + 4) * (int)k,
      w = (int)u32(p + 8) * (int)k, h = (int)u32(p + 12) * (int)k;
  int x0 = max_i(max_i(x, 0), v->clip.x),
      x1 = min_i(min_i(x + w, (int)v->width), v->clip.x + v->clip.w);
  int y0 = max_i(max_i(y, (int)v->y), v->clip.y),
      y1 = min_i(min_i(y + h, (int)(v->y + v->rows)), v->clip.y + v->clip.h);
  if (x1 <= x0 || y1 <= y0)
    return 1;
  /* Integer nearest-neighbour magnification samples once per source pixel.
   * Transparent blocks retain each destination's own base; ADD still blends
   * each destination independently. No copied row can carry stale background. */
  if ((uint32_t)w % rw == 0 && (uint32_t)h % rh == 0) {
    uint32_t bw = (uint32_t)w / rw, bh = (uint32_t)h / rh;
    /* Scaling must also be integral in logical coordinates: otherwise a
     * physical integer ratio need not match the scalar floor-before-scale. */
    if (u32(p + 8) % rw == 0 && u32(p + 12) % rh == 0) {
      uint32_t a0 = (uint32_t)(x0 - x) / bw,
               a1 = ((uint32_t)(x1 - x) + bw - 1) / bw,
               b0 = (uint32_t)(y0 - y) / bh,
               b1 = ((uint32_t)(y1 - y) + bh - 1) / bh;
      uint32_t u = u32(p + 20), top = u32(p + 24);
      const uint16_t *palette =
          t->format == 1 ? v->resources->palettes[u32(p + 40)] : NULL;
      int solid = op == TINYRT_GFX_SOLID_SPRITE,
          add = op == TINYRT_GFX_ADD_SPRITE;
      int clone_row = !add && (t->format == 2 || !(flags & 1));
      uint16_t ink = (uint16_t)u32(p + 44);
      for (uint32_t b = b0; b < b1; b++) {
        int yy0 = max_i(y + (int)(b * bh), y0),
            yy1 = min_i(y + (int)((b + 1) * bh), y1);
        if (bw == 2 && !(rot & 1) && t->format == 1 &&
            op == TINYRT_GFX_SPRITE) {
          int reverse_x = !!(flags & 2) ^ (rot == 2),
              reverse_y = !!(flags & 4) ^ (rot == 2);
          uint32_t tx = reverse_x ? sw - 1 - a0 : a0,
                   ty = reverse_y ? sh - 1 - b : b;
          const uint8_t *src = t->data + (top + ty) * t->w + u + tx;
          for (int yy = yy0; yy < (clone_row ? yy0 + 1 : yy1); yy++)
            expand_index2_row(v->dst + (uint32_t)(yy - (int)v->y) * v->stride + x0,
                              src, (uint32_t)(x1 - x0),
                              (uint32_t)(x0 - x) & 1u,
                              reverse_x ? -1 : 1, palette, flags & 1);
        } else {
        for (uint32_t a = a0; a < a1; a++) {
          uint32_t tx, ty;
          if (rot == 0) { tx = a; ty = b; }
          else if (rot == 1) { tx = b; ty = sh - 1 - a; }
          else if (rot == 2) { tx = sw - 1 - a; ty = sh - 1 - b; }
          else { tx = sw - 1 - b; ty = a; }
          if (flags & 2) tx = sw - 1 - tx;
          if (flags & 4) ty = sh - 1 - ty;
          uint32_t at = (top + ty) * t->w + u + tx;
          uint16_t color;
          if (t->format == 1) {
            uint8_t index = t->data[at];
            if (!index && (flags & 1)) continue;
            color = solid ? ink : palette[index];
          } else color = solid ? ink : (uint16_t)u16(t->data + 2 * at);
          int xx0 = max_i(x + (int)(a * bw), x0),
              xx1 = min_i(x + (int)((a + 1) * bw), x1);
          for (int yy = yy0; yy < (clone_row ? yy0 + 1 : yy1); yy++) {
            uint16_t *dst = v->dst + (uint32_t)(yy - (int)v->y) * v->stride;
            if (add) {
              for (int xx = xx0; xx < xx1; xx++)
                dst[xx] = add565(dst[xx], color);
            } else if (bw == 2) {
              dst[xx0] = color;
              if (xx1 - xx0 == 2) dst[xx0 + 1] = color;
            } else fill_color(dst + xx0, (uint32_t)(xx1 - xx0), color);
          }
        }
        }
        /* An opaque source row overwrites the entire clipped span. Only then
         * is copying repeated rows safe even when their old bases differ. */
        if (clone_row) {
          uint16_t *first = v->dst + (uint32_t)(yy0 - (int)v->y) * v->stride;
          for (int yy = yy0 + 1; yy < yy1; yy++)
            memcpy(v->dst + (uint32_t)(yy - (int)v->y) * v->stride + x0,
                   first + x0, (uint32_t)(x1 - x0) * sizeof(*first));
        }
      }
      return 1;
    }
  }
  return 0;
}
static int paint_sprite(render_t *v, uint32_t op, const uint8_t *p) {
  uint32_t flags = u32(p + 36), rot = (flags >> 3) & 3, sw = u32(p + 28),
           sh = u32(p + 32), k = v->scale;
  texture_t *t = v->resources->textures[u32(p + 16)];
  uint32_t rw = (rot & 1) ? sh : sw, rh = (rot & 1) ? sw : sh;
  int x = (int32_t)u32(p) * (int)k, y = (int32_t)u32(p + 4) * (int)k,
      w = (int)u32(p + 8) * (int)k, h = (int)u32(p + 12) * (int)k;
  int x0 = max_i(max_i(x, 0), v->clip.x),
      x1 = min_i(min_i(x + w, (int)v->width), v->clip.x + v->clip.w);
  int y0 = max_i(max_i(y, (int)v->y), v->clip.y),
      y1 = min_i(min_i(y + h, (int)(v->y + v->rows)), v->clip.y + v->clip.h);
  if (x1 <= x0 || y1 <= y0)
    return 1;
  if (x1 - x0 > 512)
    return 0;
  uint32_t xmap[512];
  for (int xx = x0; xx < x1; xx++) {
    uint32_t a = ((uint32_t)(xx - x) / k) * rw / u32(p + 8);
    uint32_t component = (rot == 1 ? sh - 1 - a : rot == 2 ? sw - 1 - a : a);
    if (rot & 1) {
      if (flags & 4)
        component = sh - 1 - component;
      component *= t->w;
    } else if (flags & 2)
      component = sw - 1 - component;
    xmap[xx - x0] = component;
  }
  uint32_t base = u32(p + 24) * t->w + u32(p + 20);
  const uint16_t *palette =
      t->format == 1 ? v->resources->palettes[u32(p + 40)] : NULL;
  int solid = op == TINYRT_GFX_SOLID_SPRITE, add = op == TINYRT_GFX_ADD_SPRITE;
  uint16_t ink = (uint16_t)u32(p + 44);
  for (int yy = y0; yy < y1; yy++) {
    uint32_t b = ((uint32_t)(yy - y) / k) * rh / u32(p + 12);
    uint32_t component = rot == 2 ? sh - 1 - b : rot == 3 ? sw - 1 - b : b;
    if (rot & 1) {
      if (flags & 2)
        component = sw - 1 - component;
    } else {
      if (flags & 4)
        component = sh - 1 - component;
      component *= t->w;
    }
    uint32_t row_base = base + component;
    uint16_t *dst = v->dst + (uint32_t)(yy - (int)v->y) * v->stride;
    if (t->format == 2) {
      for (int xx = x0; xx < x1; xx++) {
        uint16_t color =
            solid ? ink
                  : (uint16_t)u16(t->data + 2 * (row_base + xmap[xx - x0]));
        dst[xx] = add ? add565(dst[xx], color) : color;
      }
    } else {
      for (int xx = x0; xx < x1; xx++) {
        uint8_t index = t->data[row_base + xmap[xx - x0]];
        if (!index && (flags & 1))
          continue;
        uint16_t color = solid ? ink : palette[index];
        dst[xx] = add ? add565(dst[xx], color) : color;
      }
    }
  }
  return 1;
}
static void paint(render_t *v, uint32_t op, const uint8_t *p,
                  const uint8_t *extra) {
  if (!v->reference &&
      (op == TINYRT_GFX_SPRITE || op == TINYRT_GFX_SOLID_SPRITE ||
       op == TINYRT_GFX_ADD_SPRITE) &&
      (paint_integer_sprite(v, op, p) || paint_sprite(v, op, p)))
    return;
  if (op == TINYRT_GFX_GRID && !v->reference) {
    paint_grid(v, p, extra);
    return;
  }
  int x = 0, y = 0, w = 0, h = 0;
  if (op != TINYRT_GFX_CLEAR) {
    x = (int32_t)u32(p);
    y = (int32_t)u32(p + 4);
    w = (int)u32(p + 8);
    h = (int)u32(p + 12);
  }
  uint32_t k = v->scale;
  uint16_t color = 0;
  uint32_t alpha = 255;
  if (op == TINYRT_GFX_CLEAR) {
    x = 0;
    y = 0;
    w = (int)v->width;
    h = (int)v->height;
    color = (uint16_t)u32(p);
    k = 1;
  } else if (op == TINYRT_GFX_GRID) {
    w = (int)(u32(p + 8) * u32(p + 16));
    h = (int)(u32(p + 12) * u32(p + 20));
  } else if (op == TINYRT_GFX_TILEMAP) {
    w = (int)(u32(p + 8) * u32(p + 16));
    h = (int)(u32(p + 12) * u32(p + 20));
    x -= (int32_t)u32(p + 28);
    y -= (int32_t)u32(p + 32);
  } else if (op == TINYRT_GFX_ARC) {
    int r = (int)u32(p + 8);
    x -= r;
    y -= r;
    w = h = 2 * r;
    color = (uint16_t)u32(p + 24);
  } else if (op == TINYRT_GFX_RECT) {
    color = (uint16_t)u32(p + 16);
    alpha = u32(p + 20);
  } else if (op == TINYRT_GFX_ROUND_RECT)
    color = (uint16_t)u32(p + 20);
  x *= (int)k;
  y *= (int)k;
  w *= (int)k;
  h *= (int)k;
  int x0 = max_i(max_i(x, 0), v->clip.x),
      x1 = min_i(min_i(x + w, (int)v->width), v->clip.x + v->clip.w);
  int y0 = max_i(max_i(y, (int)v->y), v->clip.y),
      y1 = min_i(min_i(y + h, (int)(v->y + v->rows)), v->clip.y + v->clip.h);
  if (x1 <= x0 || y1 <= y0)
    return;
  if (op == TINYRT_GFX_TEXT) {
    if (v->text) {
      tinyrt_gfx_text_t t;
      memcpy(&t, p - 4, sizeof(t));
      v->text(v->ctx, v->dst, v->stride, v->width, v->height, v->y, v->rows, &t,
              (const char *)extra, &v->clip, k);
    }
    return;
  }
  if (!v->reference && (op == TINYRT_GFX_CLEAR || op == TINYRT_GFX_RECT)) {
    for (int yy = y0; yy < y1; yy++) {
      uint16_t *row = v->dst + (uint32_t)(yy - (int)v->y) * v->stride;
      if (alpha == 255) {
        fill_color(row + x0, (uint32_t)(x1 - x0), color);
      } else if (alpha) {
        for (int xx = x0; xx < x1; xx++)
          row[xx] = blend565(row[xx], color, alpha);
      }
    }
    return;
  }
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = v->dst + (uint32_t)(yy - (int)v->y) * v->stride;
    for (int xx = x0; xx < x1; xx++) {
      int lx = (xx - x) / (int)k, ly = (yy - y) / (int)k;
      uint16_t c = color;
      if (op == TINYRT_GFX_GRID) {
        uint32_t cw = u32(p + 16), ch = u32(p + 20), gap = u32(p + 24),
                 cols = u32(p + 8), rows = u32(p + 12);
        uint32_t col = (uint32_t)lx / cw, r = (uint32_t)ly / ch;
        if ((uint32_t)lx % cw >= cw - gap || (uint32_t)ly % ch >= ch - gap)
          continue;
        if (u32(p + 32) & 1) {
          int64_t dx = (int64_t)(2 * col + 1) * cw - (int64_t)cols * cw,
                  dy = (int64_t)(2 * r + 1) * ch - (int64_t)rows * ch;
          int64_t diam = min_i((int)(cols * cw), (int)(rows * ch));
          if (dx * dx + dy * dy > diam * diam)
            continue;
        }
        uint8_t index = extra[r * cols + col];
        if (!index)
          continue;
        c = v->resources->palettes[u32(p + 28)][index];
      } else if (op == TINYRT_GFX_SPRITE || op == TINYRT_GFX_SOLID_SPRITE ||
                 op == TINYRT_GFX_ADD_SPRITE || op == TINYRT_GFX_TILEMAP) {
        uint32_t tx, ty, slot, flags, ps;
        texture_t *t;
        if (op == TINYRT_GFX_TILEMAP) {
          uint32_t tw = u32(p + 16), th = u32(p + 20), cols = u32(p + 8);
          slot = u32(p + 24);
          flags = u32(p + 36);
          ps = u32(p + 40);
          t = v->resources->textures[slot];
          uint32_t tile = extra[((uint32_t)ly / th) * cols + (uint32_t)lx / tw];
          tx = (tile % (t->w / tw)) * tw + (uint32_t)lx % tw;
          ty = (tile / (t->w / tw)) * th + (uint32_t)ly % th;
        } else {
          slot = u32(p + 16);
          t = v->resources->textures[slot];
          uint32_t sw = u32(p + 28), sh = u32(p + 32);
          flags = u32(p + 36);
          ps = u32(p + 40);
          uint32_t rot = (flags >> 3) & 3, rw = (rot & 1) ? sh : sw,
                   rh = (rot & 1) ? sw : sh;
          uint32_t a = (uint32_t)lx * rw / u32(p + 8),
                   b = (uint32_t)ly * rh / u32(p + 12);
          if (rot == 0) {
            tx = a;
            ty = b;
          } else if (rot == 1) {
            tx = b;
            ty = sh - 1 - a;
          } else if (rot == 2) {
            tx = sw - 1 - a;
            ty = sh - 1 - b;
          } else {
            tx = sw - 1 - b;
            ty = a;
          }
          if (flags & 2)
            tx = sw - 1 - tx;
          if (flags & 4)
            ty = sh - 1 - ty;
          tx += u32(p + 20);
          ty += u32(p + 24);
        }
        uint32_t at = ty * t->w + tx;
        if (t->format == 2)
          c = (uint16_t)u16(t->data + at * 2);
        else {
          uint8_t idx = t->data[at];
          if (!idx && (flags & 1))
            continue;
          c = v->resources->palettes[ps][idx];
        }
        if (op == TINYRT_GFX_SOLID_SPRITE)
          c = (uint16_t)u32(p + 44);
        if (op == TINYRT_GFX_ADD_SPRITE)
          c = add565(row[xx], c);
      } else if (op == TINYRT_GFX_ROUND_RECT) {
        int r = (int)u32(p + 16), dw = (int)u32(p + 8), dh = (int)u32(p + 12);
        int64_t dx = lx < r         ? 2 * (int64_t)lx + 1 - 2 * r
                     : lx >= dw - r ? 2 * (int64_t)lx + 1 - 2 * (dw - r)
                                    : 0;
        int64_t dy = ly < r         ? 2 * (int64_t)ly + 1 - 2 * r
                     : ly >= dh - r ? 2 * (int64_t)ly + 1 - 2 * (dh - r)
                                    : 0;
        if (dx * dx + dy * dy > 4 * (int64_t)r * r)
          continue;
      } else if (op == TINYRT_GFX_ARC) {
        int r = (int)u32(p + 8), thick = (int)u32(p + 12),
            dx = 2 * lx + 1 - 2 * r, dy = 2 * ly + 1 - 2 * r;
        int64_t d = (int64_t)dx * dx + (int64_t)dy * dy;
        if (d > 4 * (int64_t)r * r ||
            d < 4 * (int64_t)(r - thick) * (r - thick) ||
            u32(p + 16) == u32(p + 20))
          continue;
        if (u32(p + 16) != 0 || u32(p + 20) != 360) {
          double a = atan2((double)dy, (double)dx) * 57.29577951308232;
          if (a < 0)
            a += 360;
          if (a < u32(p + 16) || a > u32(p + 20))
            continue;
        }
      }
      row[xx] = alpha == 255 ? c : blend565(row[xx], c, alpha);
    }
  }
}
static void store32(uint8_t *p, uint32_t n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
  p[2] = (uint8_t)(n >> 16);
  p[3] = (uint8_t)(n >> 24);
}
static uint16_t rgb565(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x7e0) | ((c >> 3) & 31));
}
static tinyrt_gfx_damage_t intersect_clip(tinyrt_gfx_damage_t a,
                                         tinyrt_gfx_damage_t b) {
  int x = max_i(a.x, b.x), y = max_i(a.y, b.y);
  int right = min_i(a.x + a.w, b.x + b.w),
      bottom = min_i(a.y + a.h, b.y + b.h);
  return (tinyrt_gfx_damage_t){x, y, max_i(0, right - x),
                              max_i(0, bottom - y)};
}
static int render_region_impl(const tinyrt_frame_t *f, uint16_t *dst,
                             uint32_t stride, uint32_t width, uint32_t height,
                             uint32_t x, uint32_t y, uint32_t region_width,
                             uint32_t rows, tinyrt_gfx_text_fn text,
                             void *ctx, int reference) {
  if (!f || !dst || !width || !height || width > 32767 || height > 32767 ||
      stride < width || x > width || region_width > width - x || !region_width ||
      y > height || rows > height - y || !rows ||
      f->gfx_bytes > TINYRT_GFX_MAX_BYTES)
    return -1;
  if (f->gfx_bytes && (f->gfx_scale < 1 || f->gfx_scale > 3 ||
                       tinyrt_gfx_validate(f->gfx_records, f->gfx_bytes,
                                           f->gfx_resources, NULL, NULL)))
    return -1;
  if (f->gfx_bytes &&
      tinyrt_gfx_work(f->gfx_records, f->gfx_bytes, width, height,
                      f->gfx_scale) > (uint64_t)width * height * 8u)
    return -1;
  tinyrt_gfx_damage_t host_clip = {(int32_t)x, (int32_t)y,
                                  (int32_t)region_width, (int32_t)rows};
  render_t v = {dst,
                stride,
                width,
                height,
                y,
                rows,
                f->gfx_bytes ? f->gfx_scale : 1,
                host_clip,
                f->gfx_resources,
                text,
                ctx,
                reference};
  if (!(f->gfx_flags & TINYRT_GFX_KEEP_PREVIOUS) &&
      !(f->gfx_bytes >= 8 && u16(f->gfx_records) == TINYRT_GFX_CLEAR))
    for (uint32_t j = 0; j < rows; j++)
      memset(dst + j * stride + x, 0, region_width * 2);
  for (uint32_t off = 0; off < f->gfx_bytes;) {
    const uint8_t *p = f->gfx_records + off;
    uint32_t op = u16(p), sz = u16(p + 2);
    p += 4;
    if (op == TINYRT_GFX_CLIP) {
      if (!u32(p + 8))
        v.clip = host_clip;
      else
        v.clip = (tinyrt_gfx_damage_t){(int32_t)u32(p) * (int32_t)v.scale,
                                       (int32_t)u32(p + 4) * (int32_t)v.scale,
                                       (int32_t)u32(p + 8) * (int32_t)v.scale,
                                       (int32_t)u32(p + 12) * (int32_t)v.scale};
      v.clip = intersect_clip(v.clip, host_clip);
    } else if (op == TINYRT_GFX_SET_PAL) { /* Palette is explicit per record for
                                              atomic validation. */
    } else if (op == TINYRT_GFX_SPRITE_BATCH) {
      for (uint32_t j = 0; j < u32(p + 20); j++) {
        uint8_t q[48] = {0};
        const uint8_t *a = p + 24 + 16 * j;
        memcpy(q, a, 8);
        memcpy(q + 8, p + 4, 8);
        memcpy(q + 16, p, 4);
        memcpy(q + 20, a + 8, 8);
        memcpy(q + 28, p + 4, 8);
        memcpy(q + 36, p + 12, 8);
        paint(&v, TINYRT_GFX_SPRITE, q, NULL);
      }
    } else
      paint(&v, op, p,
            p + (op == TINYRT_GFX_GRID   ? 36
                 : op == TINYRT_GFX_TEXT ? 32
                                         : 44));
    off += sz;
  }
  /* Legacy commands after the raster marker share the same native surface. */
  v.scale = 1;
  v.clip = host_clip;
  for (uint32_t i = 0; i < f->count; i++) {
    const tinyrt_draw_command_t *c = &f->commands[i];
    uint8_t b[68] = {0}, *p = b + 4;
    uint32_t op = 0;
    store32(p, (uint32_t)c->x);
    store32(p + 4, (uint32_t)c->y);
    store32(p + 8, (uint32_t)c->w);
    store32(p + 12, (uint32_t)c->h);
    if (c->kind == TINYRT_DRAW_RASTER)
      continue;
    if (c->kind == TINYRT_DRAW_CLEAR) {
      op = TINYRT_GFX_CLEAR;
      store32(p, rgb565(c->rgb));
    } else if (c->kind == TINYRT_DRAW_RECT) {
      op = TINYRT_GFX_RECT;
      store32(p + 16, rgb565(c->rgb));
      store32(p + 20, 255);
    } else if (c->kind == TINYRT_DRAW_ROUND_RECT) {
      op = TINYRT_GFX_ROUND_RECT;
      store32(p + 16, (uint32_t)c->radius);
      store32(p + 20, rgb565(c->rgb));
    } else if (c->kind == TINYRT_DRAW_ARC) {
      op = TINYRT_GFX_ARC;
      store32(p + 8, (uint32_t)c->radius);
      store32(p + 12, (uint32_t)c->thickness);
      store32(p + 16, (uint32_t)c->start_angle);
      store32(p + 20, (uint32_t)c->end_angle);
      store32(p + 24, rgb565(c->rgb));
    } else if (c->kind == TINYRT_DRAW_TEXT || c->kind == TINYRT_DRAW_TEXT_BOX) {
      op = TINYRT_GFX_TEXT;
      b[0] = (uint8_t)op;
      b[2] = 36;
      if (c->kind == TINYRT_DRAW_TEXT) {
        store32(p + 8, width - (uint32_t)c->x);
        store32(p + 12, height - (uint32_t)c->y);
      }
      store32(p + 16,
              (uint32_t)(c->kind == TINYRT_DRAW_TEXT ? 24 : c->font_px));
      store32(p + 20, (uint32_t)c->align |
                          (c->kind == TINYRT_DRAW_TEXT ? 0x80000000u : 0u));
      store32(p + 24, 0x80000000u | (c->rgb & 0xffffffu));
      store32(p + 28, (uint32_t)strlen(c->text));
      paint(&v, op, p, (const uint8_t *)c->text);
      continue;
    } else if (c->kind == TINYRT_DRAW_RGB565 ||
               c->kind == TINYRT_DRAW_RGB565_SCALED) {
      int sw = c->kind == TINYRT_DRAW_RGB565 ? c->w : c->source_w,
          sh = c->kind == TINYRT_DRAW_RGB565 ? c->h : c->source_h;
      if (sw <= 0 || sh <= 0 || c->w <= 0 || c->h <= 0 ||
          (uint32_t)sw * (uint32_t)sh * 2 > f->pixel_bytes)
        return -1;
      int x0 = max_i(c->x, (int)x),
          x1 = min_i(c->x + c->w, (int)(x + region_width)),
          y0 = max_i(c->y, (int)y), y1 = min_i(c->y + c->h, (int)(y + rows));
      for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
          uint32_t sx = (uint32_t)(xx - c->x) * (uint32_t)sw / (uint32_t)c->w,
                   sy = (uint32_t)(yy - c->y) * (uint32_t)sh / (uint32_t)c->h;
          dst[(uint32_t)(yy - (int)y) * stride + (uint32_t)xx] =
              (uint16_t)u16(f->pixels + 2 * (sy * (uint32_t)sw + sx));
        }
      continue;
    } else
      return -1;
    paint(&v, op, p, NULL);
  }
  return 0;
}
int tinyrt_gfx_framebuffer_patch(tinyrt_gfx_resources_t **rp, uint32_t slot,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 const void *src, uint32_t len) {
  if (!rp || !*rp || slot >= 32 || !src)
    return -1;
  tinyrt_gfx_resources_t *r = *rp;
  texture_t *t = r->textures[slot];
  if (!t || t->format != 2 || !w || !h || x > t->w || y > t->h ||
      w > t->w - x || h > t->h - y || len != w * h * 2)
    return -1;
  texture_t *n = r->allocate((unsigned int)sizeof(*n) + t->len);
  if (!n)
    return -1;
  memcpy((uint8_t *)n + sizeof(n->refs), (const uint8_t *)t + sizeof(t->refs),
         sizeof(*n) + t->len - sizeof(n->refs));
  n->refs = 1;
  for (uint32_t row = 0; row < h; row++)
    memcpy(n->data + 2 * ((y + row) * t->w + x),
           (const uint8_t *)src + row * w * 2, w * 2);
  if (writable(rp)) {
    tex_drop(n);
    return -1;
  }
  r = *rp;
  t = r->textures[slot];
  r->textures[slot] = n;
  tex_drop(t);
  return 0;
}

int tinyrt_gfx_render_strip(const tinyrt_frame_t *f, uint16_t *p,
                            uint32_t stride, uint32_t w, uint32_t h, uint32_t y,
                            uint32_t rows, tinyrt_gfx_text_fn text, void *ctx) {
  return render_region_impl(f, p, stride, w, h, 0, y, w, rows, text, ctx, 0);
}
int tinyrt_gfx_render_reference_strip(const tinyrt_frame_t *f, uint16_t *p,
                                      uint32_t stride, uint32_t w, uint32_t h,
                                      uint32_t y, uint32_t rows,
                                      tinyrt_gfx_text_fn text, void *ctx) {
  return render_region_impl(f, p, stride, w, h, 0, y, w, rows, text, ctx, 1);
}
int tinyrt_gfx_render_region(const tinyrt_frame_t *f, uint16_t *p,
                             uint32_t stride, uint32_t w, uint32_t h,
                             uint32_t x, uint32_t y, uint32_t region_width,
                             uint32_t rows, tinyrt_gfx_text_fn text, void *ctx) {
  return render_region_impl(f, p, stride, w, h, x, y, region_width, rows,
                            text, ctx, 0);
}
int tinyrt_gfx_render_reference_region(const tinyrt_frame_t *f, uint16_t *p,
                                       uint32_t stride, uint32_t w, uint32_t h,
                                       uint32_t x, uint32_t y,
                                       uint32_t region_width, uint32_t rows,
                                       tinyrt_gfx_text_fn text, void *ctx) {
  return render_region_impl(f, p, stride, w, h, x, y, region_width, rows,
                            text, ctx, 1);
}
