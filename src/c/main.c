#include <pebble.h>
#include "comm.h"
#include "controls.h"
#include "ui.h"
#include "menu.h"
#include "list.h"
#include "playback_data.h"
#include "tutorial.h"

static Window *s_np_window;
static bool s_np_visible = false;

// Playback state (synced from Spotify, interpolated locally)
static bool s_playing = false;
static int s_elapsed = 0;
static int s_total = 0;
static AppTimer *s_tick_timer = NULL;

// --- Progress timer (local interpolation between Spotify polls) ---

static void tick_cb(void *data) {
  s_tick_timer = NULL;
  if (s_playing && s_elapsed < s_total) {
    s_elapsed++;
    ui_set_progress(s_elapsed, s_total);
    s_tick_timer = app_timer_register(1000, tick_cb, NULL);
  }
}

static void start_ticking(void) {
  if (!s_tick_timer && s_playing) {
    s_tick_timer = app_timer_register(1000, tick_cb, NULL);
  }
}

static void stop_ticking(void) {
  if (s_tick_timer) {
    app_timer_cancel(s_tick_timer);
    s_tick_timer = NULL;
  }
}

// --- Comm callbacks ---

static void image_ready_cb(GBitmap *bitmap) {
  ui_set_album_art(bitmap);
}

static void status_cb(const char *status) {
  ui_set_status(status);
}

static void track_info_cb(const char *title, const char *artist,
                           int duration, int elapsed, bool is_playing) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "track_info_cb enter");

  // Update playback state
  s_total = duration;
  s_elapsed = elapsed;
  s_playing = is_playing;

  // Update Now Playing UI (no-ops if window not showing)
  APP_LOG(APP_LOG_LEVEL_DEBUG, "track_info_cb: ui update");
  ui_set_track_info(title, artist);
  ui_set_progress(elapsed, duration);

  // Update menu subtitle
  APP_LOG(APP_LOG_LEVEL_DEBUG, "track_info_cb: menu subtitle");
  menu_set_now_playing_subtitle(title);

  // Only tick when Now Playing window is visible
  APP_LOG(APP_LOG_LEVEL_DEBUG, "track_info_cb: timer");
  stop_ticking();
  if (s_playing && s_np_visible) {
    start_ticking();
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "track_info_cb done");
}

static void list_item_cb(int list_type, int index, int count,
                          const char *title, const char *subtitle,
                          const char *uri) {
  list_set_count(count);
  list_add_item(index, title, subtitle, uri);
}

static void list_done_cb(int list_type) {
  list_mark_done();
}

static void auth_status_cb(bool authenticated) {
  if (authenticated) {
    ui_set_status("Spotify connected");
    // Fetch current playback on auth
    comm_send_command(CMD_FETCH_NOW_PLAYING, NULL);
  } else {
    ui_set_status("Open Settings to login");
  }
}

// --- Controls callbacks ---

static void music_command_cb(MusicCommand cmd) {
  switch (cmd) {
    case MUSIC_CMD_PLAY_PAUSE:
      comm_send_command(CMD_PLAY_PAUSE, NULL);
      ui_set_status(s_playing ? "Pausing..." : "Playing...");
      break;
    case MUSIC_CMD_NEXT:
      comm_send_command(CMD_NEXT_TRACK, NULL);
      ui_set_status("Next...");
      break;
    case MUSIC_CMD_PREV:
      comm_send_command(CMD_PREV_TRACK, NULL);
      ui_set_status("Previous...");
      break;
    case MUSIC_CMD_VOL_UP:
      comm_send_command(CMD_VOLUME_UP, NULL);
      ui_set_status("Volume +");
      break;
    case MUSIC_CMD_VOL_DOWN:
      comm_send_command(CMD_VOLUME_DOWN, NULL);
      ui_set_status("Volume -");
      break;
    case MUSIC_CMD_LIKE:
      comm_send_command(CMD_LIKE_TRACK, NULL);
      ui_set_status("Liked!");
      break;
    case MUSIC_CMD_DISLIKE:
      comm_send_command(CMD_DISLIKE_TRACK, NULL);
      ui_set_status("Removed");
      break;
  }
}

static void mode_changed_cb(ControlMode mode) {
  if (mode == CONTROL_MODE_VOLUME) {
    ui_set_status("Volume Mode");
  } else {
    ui_set_status("Track Mode");
  }
}

// --- Now Playing window (pushed from menu) ---

static void np_window_load(Window *window) {
  s_np_visible = true;
  ui_init(window);
  window_set_click_config_provider(window, controls_click_config_provider);
}

static void np_window_unload(Window *window) {
  s_np_visible = false;
  stop_ticking();
  ui_deinit();
}

void now_playing_window_push(void) {
  s_np_window = window_create();
  window_set_background_color(s_np_window, GColorBlack);
  window_set_window_handlers(s_np_window, (WindowHandlers) {
    .load = np_window_load,
    .unload = np_window_unload,
  });
  window_stack_push(s_np_window, true);
}

// --- Tutorial callback ---

static void tutorial_done(void) {
  // Tutorial finished, now show the menu
}

// --- App lifecycle ---

static void init(void) {
  CommCallbacks cbs = {
    .on_image_ready = image_ready_cb,
    .on_status = status_cb,
    .on_track_info = track_info_cb,
    .on_list_item = list_item_cb,
    .on_list_done = list_done_cb,
    .on_auth_status = auth_status_cb,
  };
  comm_init(cbs);
  controls_init(music_command_cb, mode_changed_cb);
  menu_window_push();

  if (tutorial_needed()) {
    tutorial_show(tutorial_done);
  }
}

static void deinit(void) {
  stop_ticking();
  controls_deinit();
  comm_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
