/* Included after the core context/memory helpers, before native registration.
 */
static int32_t gfx_error(tinyrt_runtime_t *r, const char *message) {
  snprintf(r->error, sizeof(r->error), "%s", message);
  if (r->frame)
    r->gfx_rejected = true;
  return -1;
}
static bool gfx_permission(tinyrt_runtime_t *r) {
  return permission(r, TINYRT_PERMISSION_DRAW);
}
static bool gfx_resources(tinyrt_runtime_t *r) {
  if (r->gfx)
    return true;
  r->gfx = tinyrt_gfx_resources_create(NULL, NULL);
  return r->gfx != NULL;
}
static const void *gfx_pointer(tinyrt_runtime_t *r, uint32_t ptr,
                               uint32_t len) {
  if (!len || !wasm_runtime_validate_app_addr(r->instance, ptr, len))
    return NULL;
  return wasm_runtime_addr_app_to_native(r->instance, ptr);
}
static uint32_t gfx_caps(wasm_exec_env_t e) {
  (void)e;
  return TINYRT_GFX_CAPS;
}
static int32_t gfx_begin(wasm_exec_env_t e, uint32_t flags) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_permission(r))
    return -1;
  uint32_t scale = (flags >> 8) & 3u;
  if (!scale)
    scale = 1;
  if (!r->frame || r->skipped || r->frame->count || r->gfx_open ||
      (flags & ~769u))
    return gfx_error(r, "gfx_begin stage or flags invalid");
  tinyrt_frame_t *f = r->frame;
  f->gfx_bytes = 0;
  f->gfx_resources = NULL;
  f->gfx_flags = flags & 1u;
  f->gfx_scale = scale;
  f->damage_count = 0;
  f->pixel_bytes = 0;
  r->gfx_open = true;
  r->gfx_record_count = 0;
  r->gfx_work = 0;
  return 0;
}
static int32_t gfx_submit(wasm_exec_env_t e, uint32_t ptr, uint32_t len) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_permission(r))
    return -1;
  if (!r->frame || !r->gfx_open || r->gfx_rejected || !len ||
      len > TINYRT_GFX_MAX_SUBMIT ||
      len > TINYRT_GFX_MAX_BYTES - r->frame->gfx_bytes)
    return gfx_error(r, "gfx_submit stage or length invalid");
  const void *src = gfx_pointer(r, ptr, len);
  if (!src)
    return gfx_error(r, "gfx records memory invalid");
  uint32_t count = 0, index = 0;
  int code = tinyrt_gfx_validate(src, len, r->gfx, &count, &index);
  if (code || count > TINYRT_GFX_MAX_RECORDS - r->gfx_record_count) {
    snprintf(r->error, sizeof(r->error), "gfx record %u rejected (%d)",
             r->gfx_record_count + index, code);
    r->gfx_rejected = true;
    return -1;
  }
  uint64_t work = tinyrt_gfx_work(src, len, (uint32_t)r->width,
                                  (uint32_t)r->height, r->frame->gfx_scale);
  if (work + r->gfx_work > (uint64_t)r->width * (uint32_t)r->height * 8u)
    return gfx_error(r, "gfx pixel work limit exceeded");
  r->gfx_work += work;
  memcpy(r->frame->gfx_records + r->frame->gfx_bytes, src, len);
  r->frame->gfx_bytes += len;
  r->gfx_record_count += count;
  return 0;
}
static int32_t gfx_damage(wasm_exec_env_t e, int32_t x, int32_t y, int32_t w,
                          int32_t h) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_permission(r))
    return -1;
  if (!r->frame || !r->gfx_open || x < 0 || y < 0 || w <= 0 || h <= 0 ||
      x > r->width || y > r->height || w > r->width - x || h > r->height - y)
    return gfx_error(r, "gfx damage out of bounds");
  tinyrt_gfx_damage_t d = {x, y, w, h};
  tinyrt_gfx_damage_merge(r->frame, &d);
  return 0;
}
static int32_t gfx_end(wasm_exec_env_t e) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_permission(r))
    return -1;
  if(r->gfx_rejected)return -1; /* Preserve the original offending record index. */
  if (!r->frame || !r->gfx_open)
    return gfx_error(r, "gfx_end rejected frame");
  r->gfx_open = false;
  if (!r->frame->gfx_bytes) {
    r->skipped = true;
    return 0;
  }
  tinyrt_frame_t *f = r->frame;
  f->gfx_resources = r->gfx;
  tinyrt_gfx_resources_retain(f->gfx_resources);
  f->count = 1;
  memset(&f->commands[0], 0, sizeof(f->commands[0]));
  f->commands[0].kind = TINYRT_DRAW_RASTER;
  return 0;
}
static bool gfx_resource_stage(tinyrt_runtime_t *r) {
  return gfx_permission(r) && r->callback <= 1 && gfx_resources(r);
}
static int32_t gfx_pal_upload(wasm_exec_env_t e, uint32_t slot, uint32_t first,
                              uint32_t count, uint32_t ptr) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_resource_stage(r) || !count || count > 256)
    return gfx_error(r, "palette stage or count invalid");
  const void *p = gfx_pointer(r, ptr, count * 2);
  return p ? tinyrt_gfx_palette_upload(&r->gfx, slot, first, count, p)
           : gfx_error(r, "palette memory invalid");
}
static int32_t gfx_tex_upload(wasm_exec_env_t e, uint32_t slot, uint32_t format,
                              uint32_t w, uint32_t h, uint32_t ptr,
                              uint32_t len, uint32_t flags) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_resource_stage(r) || (r->fb_w && slot == 31) || slot >= 32 ||
      (format != 1 && format != 2) || !w || !h || w > 512 || h > 512 ||
      len != w * h * (format == 2 ? 2u : 1u) ||
      len > TINYRT_GFX_RESOURCE_BYTES || (flags & ~1u))
    return gfx_error(r, "texture stage, format or length invalid");
  if (!(flags & 1)) {
    const void *p = gfx_pointer(r, ptr, len);
    return p ? tinyrt_gfx_texture_upload(&r->gfx, slot, format, w, h, p, len)
             : gfx_error(r, "texture memory invalid");
  }
  if (!r->host.asset_read || ptr > UINT32_MAX - len)
    return gfx_error(r, "texture resource range invalid");
  uint8_t *copy = allocate(len);
  if (!copy)
    return -1;
  bool ok = true;
  for (uint32_t off = 0; off < len;) {
    uint32_t n = len - off;
    if (n > 4096)
      n = 4096;
    if (r->host.asset_read(r->host.ctx, ptr + off, copy + off, n) !=
        TINYRT_OK) {
      ok = false;
      break;
    }
    off += n;
  }
  int status =
      ok ? tinyrt_gfx_texture_upload(&r->gfx, slot, format, w, h, copy, len)
         : -1;
  release(copy);
  return status;
}
static int32_t gfx_tex_update_rows(wasm_exec_env_t e, uint32_t slot, uint32_t y,
                                   uint32_t rows, uint32_t ptr, uint32_t len) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_resource_stage(r) || (r->fb_w && slot == 31) ||
      len > TINYRT_GFX_RESOURCE_BYTES)
    return gfx_error(r, "texture rows stage invalid");
  const void *p = gfx_pointer(r, ptr, len);
  return p ? tinyrt_gfx_texture_rows(&r->gfx, slot, y, rows, p, len)
           : gfx_error(r, "texture rows memory invalid");
}
static int32_t gfx_tex_free(wasm_exec_env_t e, uint32_t slot) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_resource_stage(r) || (r->fb_w && slot == 31))
    return gfx_error(r, "texture free stage invalid");
  return tinyrt_gfx_texture_free(&r->gfx, slot);
}
static int32_t fb_config(wasm_exec_env_t e, uint32_t w, uint32_t h,
                         uint32_t scale) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_permission(r) || r->callback != 0 || r->fb_w || !w || !h ||
      w > 512 || h > 512 || (scale != 1 && scale != 2) || w * h * 2 > 131072 ||
      w * scale > (uint32_t)r->width || h * scale > (uint32_t)r->height ||
      !gfx_resources(r))
    return gfx_error(r, "framebuffer configuration invalid");
  uint32_t len = w * h * 2;
  void *copy = allocate(len);
  if (!copy)
    return -1;
  memset(copy, 0, len);
  int status = tinyrt_gfx_texture_upload(&r->gfx, 31, 2, w, h, copy, len);
  release(copy);
  if (!status) {
    r->fb_w = w;
    r->fb_h = h;
    r->fb_scale = scale;
  }
  return status;
}
static int32_t fb_present(wasm_exec_env_t e, uint32_t ptr, uint32_t len,
                          int32_t x, int32_t y, int32_t w, int32_t h) {
  tinyrt_runtime_t *r = context(e);
  if (!gfx_permission(r))
    return -1;
  if (!r->frame || r->skipped || r->frame->count || r->gfx_open || !r->fb_w ||
      x < 0 || y < 0 || w <= 0 || h <= 0 || (uint32_t)x > r->fb_w ||
      (uint32_t)y > r->fb_h || (uint32_t)w > r->fb_w - (uint32_t)x ||
      (uint32_t)h > r->fb_h - (uint32_t)y ||
      len != (uint32_t)w * (uint32_t)h * 2)
    return gfx_error(r, "framebuffer patch invalid");
  const void *p = gfx_pointer(r, ptr, len);
  if (!p || tinyrt_gfx_framebuffer_patch(&r->gfx, 31, (uint32_t)x, (uint32_t)y,
                                         (uint32_t)w, (uint32_t)h, p, len))
    return gfx_error(r, "framebuffer patch copy failed");
  tinyrt_frame_t *f = r->frame;
  f->gfx_bytes = 52;
  f->gfx_flags = TINYRT_GFX_KEEP_PREVIOUS | TINYRT_GFX_RECONSTRUCTIBLE;
  f->gfx_scale = 1;
  f->damage_count = 0;
  f->pixel_bytes = 0;
  tinyrt_gfx_sprite_t s = {TINYRT_GFX_SPRITE,
                           52,
                           0,
                           0,
                           (int32_t)(r->fb_w * r->fb_scale),
                           (int32_t)(r->fb_h * r->fb_scale),
                           31,
                           0,
                           0,
                           r->fb_w,
                           r->fb_h,
                           0,
                           0,
                           0};
  memcpy(f->gfx_records, &s, sizeof(s));
  if (r->fb_presented) {
    tinyrt_gfx_damage_t d = {x * (int32_t)r->fb_scale, y * (int32_t)r->fb_scale,
                             w * (int32_t)r->fb_scale,
                             h * (int32_t)r->fb_scale};
    tinyrt_gfx_damage_merge(f, &d);
  } else {
    f->gfx_flags &= ~TINYRT_GFX_KEEP_PREVIOUS;
    r->fb_presented = true;
  }
  f->gfx_resources = r->gfx;
  tinyrt_gfx_resources_retain(r->gfx);
  f->count = 1;
  memset(&f->commands[0], 0, sizeof(f->commands[0]));
  f->commands[0].kind = TINYRT_DRAW_RASTER;
  return 0;
}
