#ifndef TINYRT_GFX_H
#define TINYRT_GFX_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TINYRT_GFX_MAX_BYTES 65536u
#define TINYRT_GFX_MAX_SUBMIT 16384u
#define TINYRT_GFX_MAX_RECORDS 4096u
#define TINYRT_GFX_TEXTURES 32u
#define TINYRT_GFX_PALETTES 16u
#define TINYRT_GFX_RESOURCE_BYTES 262144u
#define TINYRT_GFX_KEEP_PREVIOUS 1u
/* Host-only: full resident framebuffer can reconstruct merged damage. */
#define TINYRT_GFX_RECONSTRUCTIBLE 2u
#define TINYRT_GFX_SCALE_SHIFT 8u
#define TINYRT_GFX_INDEX8 1u
#define TINYRT_GFX_RGB565 2u
#define TINYRT_GFX_FROM_ASSET 1u
#define TINYRT_GFX_TRANSPARENT 1u
#define TINYRT_GFX_FLIP_X 2u
#define TINYRT_GFX_FLIP_Y 4u
#define TINYRT_GFX_ROTATE_SHIFT 3u
#define TINYRT_GFX_CIRCLE 1u
#define TINYRT_GFX_CAP_RECT (1u<<0)
#define TINYRT_GFX_CAP_GRID (1u<<1)
#define TINYRT_GFX_CAP_TEXT (1u<<2)
#define TINYRT_GFX_CAP_SPRITE (1u<<3)
#define TINYRT_GFX_CAP_ROTATE90 (1u<<4)
#define TINYRT_GFX_CAP_DAMAGE (1u<<5)
#define TINYRT_GFX_CAP_FRAMEBUFFER (1u<<6)
#define TINYRT_GFX_CAP_ADD_SPRITE (1u<<7)
#define TINYRT_GFX_CAP_TILEMAP (1u<<8)
#define TINYRT_GFX_CAP_SCALE3 (1u<<9)
#define TINYRT_GFX_CAP_ROUND_RECT (1u<<10)
#define TINYRT_GFX_CAP_ARC (1u<<11)
#define TINYRT_GFX_CAP_SPRITE_BATCH (1u<<12)
#define TINYRT_GFX_CAPS 8191u
/* All records use little-endian u16 op/u16 total byte size (multiple of 4),
 * followed by signed i32 coordinates / u32 other fields. Inline data is padded
 * with zero to four-byte alignment. No pointers or offsets in records. */
enum { TINYRT_GFX_CLEAR=1,TINYRT_GFX_RECT=2,TINYRT_GFX_ROUND_RECT=3,
 TINYRT_GFX_ARC=4,TINYRT_GFX_SPRITE=5,TINYRT_GFX_SOLID_SPRITE=6,
 TINYRT_GFX_ADD_SPRITE=7,TINYRT_GFX_SPRITE_BATCH=8,TINYRT_GFX_GRID=9,
 TINYRT_GFX_TILEMAP=10,TINYRT_GFX_TEXT=11,TINYRT_GFX_SET_PAL=12,TINYRT_GFX_CLIP=13 };
typedef struct { uint16_t op,size; } tinyrt_gfx_record_t;
typedef struct { uint16_t op,size; int32_t x,y,w,h; uint32_t color,alpha; } tinyrt_gfx_rect_t;
typedef struct { uint16_t op,size; int32_t x,y; uint32_t cols,rows,cell_w,cell_h,gap,pal,flags; } tinyrt_gfx_grid_t;
typedef struct { uint16_t op,size; int32_t x,y,w,h; uint32_t tex,u,v,sw,sh,flags,pal,color; } tinyrt_gfx_sprite_t;
typedef struct { uint16_t op,size; int32_t x,y,w,h; uint32_t font_px,align,color,length; } tinyrt_gfx_text_t;
typedef struct { int32_t x,y,w,h; } tinyrt_gfx_damage_t;
typedef struct tinyrt_gfx_resources tinyrt_gfx_resources_t;
typedef void *(*tinyrt_gfx_alloc_fn)(unsigned int);
typedef void (*tinyrt_gfx_free_fn)(void *);
/* Host backend: configure before starting any drawing threads. Returning false
 * requests the bit-identical software fill. The buffer owns stride*rows pixels;
 * x/y/w/h are already clipped and y is relative to its first row. */
typedef bool (*tinyrt_gfx_fill_fn)(void *,uint16_t *,uint32_t,uint32_t,
                                 uint32_t,uint32_t,uint32_t,uint32_t,uint16_t);
void tinyrt_gfx_set_fill_accelerator(tinyrt_gfx_fill_fn,void *);
tinyrt_gfx_resources_t *tinyrt_gfx_resources_create(tinyrt_gfx_alloc_fn,tinyrt_gfx_free_fn);
/* Separate bounded 1152KiB pool; includes metadata and queued COW snapshots. */
size_t tinyrt_gfx_memory_used(void);
void tinyrt_gfx_resources_retain(tinyrt_gfx_resources_t *);
void tinyrt_gfx_resources_release(tinyrt_gfx_resources_t *);
int tinyrt_gfx_texture_upload(tinyrt_gfx_resources_t **,uint32_t,uint32_t,uint32_t,uint32_t,const void *,uint32_t);
int tinyrt_gfx_texture_rows(tinyrt_gfx_resources_t **,uint32_t,uint32_t,uint32_t,const void *,uint32_t);
int tinyrt_gfx_framebuffer_patch(tinyrt_gfx_resources_t **,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,const void *,uint32_t);
int tinyrt_gfx_palette_upload(tinyrt_gfx_resources_t **,uint32_t,uint32_t,uint32_t,const void *);
int tinyrt_gfx_texture_free(tinyrt_gfx_resources_t **,uint32_t);
/* Returns 0, -1 invalid, -2 unsupported. error_record is zero based. */
int tinyrt_gfx_validate(const void *,uint32_t,const tinyrt_gfx_resources_t *,uint32_t *,uint32_t *);
/* Conservative physical pixel visits, called after validation. */
uint64_t tinyrt_gfx_work(const void *,uint32_t,uint32_t,uint32_t,uint32_t);
struct tinyrt_frame;
typedef void (*tinyrt_gfx_text_fn)(void *ctx,uint16_t *pixels,uint32_t stride,
 uint32_t width,uint32_t height,uint32_t strip_y,uint32_t strip_rows,
 const tinyrt_gfx_text_t *,const char *,const tinyrt_gfx_damage_t *clip,uint32_t scale);
/* Existing strip pixels are the base for KEEP_PREVIOUS; otherwise clear black.
 * Writes only the strip. Host font callback shares the same clip/scale. */
int tinyrt_gfx_render_strip(const struct tinyrt_frame *,uint16_t *,uint32_t stride,
 uint32_t width,uint32_t height,uint32_t strip_y,uint32_t strip_rows,tinyrt_gfx_text_fn,void *);
int tinyrt_gfx_render_reference_strip(const struct tinyrt_frame *,uint16_t *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,tinyrt_gfx_text_fn,void *);
/* Internal host API: physical region, independent of gfx_scale. dst points to
 * full-width row y (not pixel x); stride is uint16_t pixels. Requires a nonempty
 * region within the viewport. Neither reads nor writes destination pixels
 * outside it. Font callbacks must obey the intersected physical clip supplied.
 * Guest CLIP/reset and legacy overlays cannot expand this host region. */
int tinyrt_gfx_render_region(const struct tinyrt_frame *,uint16_t *,uint32_t stride,
 uint32_t width,uint32_t height,uint32_t x,uint32_t y,uint32_t region_width,
 uint32_t rows,tinyrt_gfx_text_fn,void *);
int tinyrt_gfx_render_reference_region(const struct tinyrt_frame *,uint16_t *,uint32_t,
 uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,tinyrt_gfx_text_fn,void *);
void tinyrt_frame_release(struct tinyrt_frame *);
void tinyrt_gfx_damage_merge(struct tinyrt_frame *,const tinyrt_gfx_damage_t *);
#ifdef __cplusplus
}
#endif
#endif
