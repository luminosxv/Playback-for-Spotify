#include "ui.h"

static BitmapLayer *s_art_layer;
static TextLayer *s_title_layer;
static TextLayer *s_artist_layer;
static TextLayer *s_time_layer;
static Layer *s_overlay_layer;
static Layer *s_progress_layer;

static char s_title_buf[64];
static char s_artist_buf[64];
static char s_time_buf[24];
static char s_status_buf_ui[64];
static bool s_showing_status = false;
static AppTimer *s_status_timer = NULL;
static float s_progress = 0.0f;

static void status_clear_cb(void *data) {
  s_status_timer = NULL;
  s_showing_status = false;
  text_layer_set_text(s_time_layer, s_time_buf);
}

static void overlay_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

static void progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Unfilled background
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Filled portion
  int filled_w = (int)(bounds.size.w * s_progress);
  if (filled_w > 0) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite));
    graphics_fill_rect(ctx, GRect(0, 0, filled_w, bounds.size.h), 0, GCornerNone);
  }
}

void ui_init(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int W = bounds.size.w;
  int H = bounds.size.h;

#if defined(PBL_ROUND)
  // Round Layout (Chalk/Gabbro): 50/50 split
  int overlay_h = H / 2;
  int art_h = H - overlay_h;

  // Album art — centered in the top half
  s_art_layer = bitmap_layer_create(GRect(0, 0, W, art_h));
  bitmap_layer_set_alignment(s_art_layer, GAlignCenter);
  bitmap_layer_set_background_color(s_art_layer, GColorBlack);
  bitmap_layer_set_compositing_mode(s_art_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_art_layer));

  // Overlay layer for bottom half
  s_overlay_layer = layer_create(GRect(0, art_h, W, overlay_h));
  layer_set_update_proc(s_overlay_layer, overlay_update_proc);
  layer_add_child(root, s_overlay_layer);

  // Centered text for round screens with vertical padding
  s_title_layer = text_layer_create(GRect(10, 5, W - 20, 24));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorWhite);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, "Playback");
  layer_add_child(s_overlay_layer, text_layer_get_layer(s_title_layer));

  s_artist_layer = text_layer_create(GRect(10, 28, W - 20, 20));
  text_layer_set_background_color(s_artist_layer, GColorClear);
  text_layer_set_text_color(s_artist_layer, GColorWhite);
  text_layer_set_font(s_artist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_artist_layer, GTextAlignmentCenter);
  text_layer_set_text(s_artist_layer, "No track");
  layer_add_child(s_overlay_layer, text_layer_get_layer(s_artist_layer));

  s_progress_layer = layer_create(GRect(W / 4, 52, W / 2, 4));
  layer_set_update_proc(s_progress_layer, progress_update_proc);
  layer_add_child(s_overlay_layer, s_progress_layer);

  s_time_layer = text_layer_create(GRect(10, 58, W - 20, 20));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorLightGray);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_text(s_time_layer, "0:00 / 0:00");
  layer_add_child(s_overlay_layer, text_layer_get_layer(s_time_layer));

#else
  // Original Rectangular Layout
  int overlay_h = 62;
  int art_h = H - overlay_h;

  s_art_layer = bitmap_layer_create(GRect(0, 0, W, art_h));
  bitmap_layer_set_alignment(s_art_layer, GAlignCenter);
  bitmap_layer_set_background_color(s_art_layer, GColorBlack);
  bitmap_layer_set_compositing_mode(s_art_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_art_layer));

  s_overlay_layer = layer_create(GRect(0, art_h, W, overlay_h));
  layer_set_update_proc(s_overlay_layer, overlay_update_proc);
  layer_add_child(root, s_overlay_layer);

  s_title_layer = text_layer_create(GRect(4, 2, W - 8, 20));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorWhite);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_overflow_mode(s_title_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(s_title_layer, "Playback");
  layer_add_child(s_overlay_layer, text_layer_get_layer(s_title_layer));

  s_artist_layer = text_layer_create(GRect(4, 21, W - 8, 18));
  text_layer_set_background_color(s_artist_layer, GColorClear);
  text_layer_set_text_color(s_artist_layer, GColorWhite);
  text_layer_set_font(s_artist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(s_artist_layer, "No track playing");
  layer_add_child(s_overlay_layer, text_layer_get_layer(s_artist_layer));

  s_progress_layer = layer_create(GRect(4, 39, W - 8, 4));
  layer_set_update_proc(s_progress_layer, progress_update_proc);
  layer_add_child(s_overlay_layer, s_progress_layer);

  s_time_layer = text_layer_create(GRect(4, 44, W - 8, 16));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorLightGray);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_time_layer, "0:00 / 0:00");
  layer_add_child(s_overlay_layer, text_layer_get_layer(s_time_layer));
#endif

  snprintf(s_time_buf, sizeof(s_time_buf), "0:00 / 0:00");
}

void ui_deinit(void) {
  if (s_status_timer) {
    app_timer_cancel(s_status_timer);
    s_status_timer = NULL;
  }
  text_layer_destroy(s_time_layer);
  s_time_layer = NULL;
  layer_destroy(s_progress_layer);
  s_progress_layer = NULL;
  text_layer_destroy(s_artist_layer);
  s_artist_layer = NULL;
  text_layer_destroy(s_title_layer);
  s_title_layer = NULL;
  layer_destroy(s_overlay_layer);
  s_overlay_layer = NULL;
  bitmap_layer_destroy(s_art_layer);
  s_art_layer = NULL;
}

void ui_set_album_art(GBitmap *bitmap) {
  if (!s_art_layer) return;
  bitmap_layer_set_bitmap(s_art_layer, bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(s_art_layer));
}

void ui_set_status(const char *text) {
  strncpy(s_status_buf_ui, text, sizeof(s_status_buf_ui) - 1);
  s_status_buf_ui[sizeof(s_status_buf_ui) - 1] = '\0';
  s_showing_status = true;

  if (!s_time_layer) return;
  text_layer_set_text(s_time_layer, s_status_buf_ui);

  // Auto-clear status after 3 seconds
  if (s_status_timer) app_timer_cancel(s_status_timer);
  s_status_timer = app_timer_register(3000, status_clear_cb, NULL);
}

void ui_set_track_info(const char *title, const char *artist) {
  strncpy(s_title_buf, title, sizeof(s_title_buf) - 1);
  s_title_buf[sizeof(s_title_buf) - 1] = '\0';
  strncpy(s_artist_buf, artist, sizeof(s_artist_buf) - 1);
  s_artist_buf[sizeof(s_artist_buf) - 1] = '\0';

  if (!s_title_layer) return;
  text_layer_set_text(s_title_layer, s_title_buf);
  text_layer_set_text(s_artist_layer, s_artist_buf);
}

void ui_set_progress(int elapsed_sec, int total_sec) {
  s_progress = (total_sec > 0) ? (float)elapsed_sec / total_sec : 0.0f;
  if (s_progress > 1.0f) s_progress = 1.0f;

  int e_min = elapsed_sec / 60, e_sec = elapsed_sec % 60;
  int t_min = total_sec / 60, t_sec = total_sec % 60;
  snprintf(s_time_buf, sizeof(s_time_buf), "%d:%02d / %d:%02d",
           e_min, e_sec, t_min, t_sec);

  if (!s_progress_layer) return;
  layer_mark_dirty(s_progress_layer);

  if (!s_showing_status) {
    text_layer_set_text(s_time_layer, s_time_buf);
  }
}
