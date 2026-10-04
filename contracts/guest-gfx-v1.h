#ifndef TINYRT_GUEST_GFX_H
#define TINYRT_GUEST_GFX_H
#include <stdint.h>
#include <stddef.h>
#define TINYRT_GFX_MAX_BYTES 65536u
#define TINYRT_GFX_MAX_SUBMIT 16384u
#define TINYRT_GFX_MAX_RECORDS 4096u
#define TINYRT_GFX_TEXTURES 32u
#define TINYRT_GFX_PALETTES 16u
#define TINYRT_GFX_RESOURCE_BYTES 262144u
#define TINYRT_GFX_KEEP_PREVIOUS 1u
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
#if defined(__wasm__)
#define TINYRT_GFX_IMPORT(name) __attribute__((import_module("tinyrt"), import_name(#name)))
#else
#define TINYRT_GFX_IMPORT(name)
#endif
TINYRT_GFX_IMPORT(gfx_begin) int32_t gfx_begin(uint32_t flags);
TINYRT_GFX_IMPORT(gfx_caps) uint32_t gfx_caps(void);
TINYRT_GFX_IMPORT(gfx_damage) int32_t gfx_damage(int32_t x,int32_t y,int32_t w,int32_t h);
TINYRT_GFX_IMPORT(gfx_end) int32_t gfx_end(void);
TINYRT_GFX_IMPORT(gfx_pal_upload) int32_t gfx_pal_upload(uint32_t slot,uint32_t first,uint32_t count,const uint16_t *colors);
TINYRT_GFX_IMPORT(gfx_submit) int32_t gfx_submit(const void *records,uint32_t length);
TINYRT_GFX_IMPORT(gfx_tex_free) int32_t gfx_tex_free(uint32_t slot);
TINYRT_GFX_IMPORT(gfx_tex_update_rows) int32_t gfx_tex_update_rows(uint32_t slot,uint32_t y,uint32_t rows,const void *source,uint32_t length);
TINYRT_GFX_IMPORT(gfx_tex_upload) int32_t gfx_tex_upload(uint32_t slot,uint32_t format,uint32_t w,uint32_t h,const void *source,uint32_t length,uint32_t flags);
TINYRT_GFX_IMPORT(fb_config) int32_t fb_config(uint32_t w,uint32_t h,uint32_t scale);
TINYRT_GFX_IMPORT(fb_present) int32_t fb_present(const void *pixels,uint32_t length,int32_t x,int32_t y,int32_t w,int32_t h);
#endif
