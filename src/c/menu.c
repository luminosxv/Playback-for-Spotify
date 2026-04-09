#include "menu.h"
#include "comm.h"
#include "list.h"
#include "playback_data.h"
#include "tutorial.h"

// Forward declaration — implemented in main.c
extern void now_playing_window_push(void);

// --- About window ---
static Window *s_about_window;
static MenuLayer *s_about_menu;

#define APP_VERSION "1.0.0"
#define NUM_ABOUT_ROWS 3

static void tutorial_restart_done(void) {
  // Tutorial finished, return to about/menu
}

static uint16_t about_get_num_rows(MenuLayer *menu, uint16_t s, void *d) {
  return NUM_ABOUT_ROWS;
}

static void about_draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *d) {
  switch (idx->row) {
    case 0:
      menu_cell_basic_draw(ctx, cell, "Tutorial", "View controls guide", NULL);
      break;
    case 1:
      menu_cell_basic_draw(ctx, cell, "Author", "alex_pavlov", NULL);
      break;
    case 2:
      menu_cell_basic_draw(ctx, cell, "Version", APP_VERSION, NULL);
      break;
  }
}

static void about_select(MenuLayer *menu, MenuIndex *idx, void *d) {
  if (idx->row == 0) {
    tutorial_show(tutorial_restart_done);
  }
}

static void about_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_about_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_about_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = about_get_num_rows,
    .draw_row = about_draw_row,
    .select_click = about_select,
  });
  menu_layer_set_normal_colors(s_about_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_about_menu,
    PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite),
    PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  menu_layer_set_click_config_onto_window(s_about_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_about_menu));
}

static void about_unload(Window *window) {
  menu_layer_destroy(s_about_menu);
  s_about_menu = NULL;
}

static void about_window_push(void) {
  s_about_window = window_create();
  window_set_background_color(s_about_window, GColorBlack);
  window_set_window_handlers(s_about_window, (WindowHandlers) {
    .load = about_load,
    .unload = about_unload,
  });
  window_stack_push(s_about_window, true);
}

static Window *s_menu_window;
static MenuLayer *s_menu_layer;

static char s_now_playing_subtitle[64] = "Not playing";

static const char *s_titles[] = { "Now Playing", "Playlists", "Artists", "Albums", "Liked Songs", "About" };
#define NUM_ROWS 6

// --- MenuLayer callbacks ---

static uint16_t get_num_sections(MenuLayer *menu, void *data) {
  return 1;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *data) {
  return NUM_ROWS;
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *data) {
  return 0;
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                     void *data) {
  const char *subtitle = NULL;
  if (cell_index->row == 0) {
    subtitle = s_now_playing_subtitle;
  }
  menu_cell_basic_draw(ctx, cell_layer, s_titles[cell_index->row], subtitle, NULL);
}

static void select_callback(MenuLayer *menu, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      now_playing_window_push();
      // Request latest now playing data
      comm_send_command(CMD_FETCH_NOW_PLAYING, NULL);
      break;
    case 1:
      list_window_push(LIST_TYPE_PLAYLISTS);
      comm_send_command(CMD_FETCH_PLAYLISTS, NULL);
      break;
    case 2:
      list_window_push(LIST_TYPE_ARTISTS);
      comm_send_command(CMD_FETCH_ARTISTS, NULL);
      break;
    case 3:
      list_window_push(LIST_TYPE_ALBUMS);
      comm_send_command(CMD_FETCH_ALBUMS, NULL);
      break;
    case 4:
      list_window_push(LIST_TYPE_LIKED_SONGS);
      comm_send_command(CMD_FETCH_LIKED_SONGS, NULL);
      break;
    case 5:
      about_window_push();
      break;
  }
}

// --- Window handlers ---

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = get_num_sections,
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_row = draw_row,
    .select_click = select_callback,
  });

  menu_layer_set_normal_colors(s_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu_layer,
    PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite),
    PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  menu_layer_set_click_config_onto_window(s_menu_layer, window);

  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

// --- Public API ---

void menu_window_push(void) {
  s_menu_window = window_create();
  window_set_background_color(s_menu_window, GColorBlack);
  window_set_window_handlers(s_menu_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_menu_window, true);
}

void menu_set_now_playing_subtitle(const char *subtitle) {
  strncpy(s_now_playing_subtitle, subtitle, sizeof(s_now_playing_subtitle) - 1);
  s_now_playing_subtitle[sizeof(s_now_playing_subtitle) - 1] = '\0';
  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
  }
}
