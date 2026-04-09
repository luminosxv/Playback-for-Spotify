#include "list.h"
#include "comm.h"

// Forward declaration — implemented in main.c
extern void now_playing_window_push(void);

static Window *s_list_window;
static MenuLayer *s_list_menu;

static ListItem s_items[MAX_LIST_ITEMS];
static int s_item_count = 0;
static int s_items_received = 0;
static bool s_loading = true;
static ListType s_current_type;

static const char *s_type_titles[] = { "Playlists", "Artists", "Albums", "Liked Songs" };

// --- MenuLayer callbacks ---

static uint16_t get_num_sections(MenuLayer *menu, void *data) {
  return 1;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *data) {
  if (s_loading && s_items_received == 0) return 1; // "Loading..." row
  return s_items_received;
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section,
                         void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, s_type_titles[s_current_type]);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                     void *data) {
  if (s_loading && s_items_received == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "Loading...", NULL, NULL);
    return;
  }

  int row = cell_index->row;
  if (row < s_items_received) {
    menu_cell_basic_draw(ctx, cell_layer, s_items[row].title,
                         s_items[row].subtitle[0] ? s_items[row].subtitle : NULL,
                         NULL);
  }
}

static void select_callback(MenuLayer *menu, MenuIndex *cell_index, void *data) {
  if (s_loading && s_items_received == 0) return; // Don't select "Loading..."

  int row = cell_index->row;
  if (row >= s_items_received) return;

  // Play the selected item
  comm_send_command(CMD_PLAY_CONTEXT, s_items[row].uri);
  now_playing_window_push();
}

// --- Window handlers ---

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_list_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_list_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections = get_num_sections,
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .draw_row = draw_row,
    .select_click = select_callback,
  });

  menu_layer_set_normal_colors(s_list_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_list_menu,
    PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite),
    PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  menu_layer_set_click_config_onto_window(s_list_menu, window);

  layer_add_child(root, menu_layer_get_layer(s_list_menu));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_list_menu);
  s_list_menu = NULL;
}

// --- Public API ---

void list_window_push(ListType type) {
  s_current_type = type;
  s_item_count = 0;
  s_items_received = 0;
  s_loading = true;

  s_list_window = window_create();
  window_set_background_color(s_list_window, GColorBlack);
  window_set_window_handlers(s_list_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_list_window, true);
}

void list_add_item(int index, const char *title, const char *subtitle,
                   const char *uri) {
  if (index >= MAX_LIST_ITEMS) return;

  strncpy(s_items[index].title, title, sizeof(s_items[index].title) - 1);
  s_items[index].title[sizeof(s_items[index].title) - 1] = '\0';

  strncpy(s_items[index].subtitle, subtitle, sizeof(s_items[index].subtitle) - 1);
  s_items[index].subtitle[sizeof(s_items[index].subtitle) - 1] = '\0';

  strncpy(s_items[index].uri, uri, sizeof(s_items[index].uri) - 1);
  s_items[index].uri[sizeof(s_items[index].uri) - 1] = '\0';

  if (index >= s_items_received) {
    s_items_received = index + 1;
  }

  if (s_list_menu) {
    menu_layer_reload_data(s_list_menu);
  }
}

void list_set_count(int count) {
  s_item_count = (count > MAX_LIST_ITEMS) ? MAX_LIST_ITEMS : count;
}

void list_mark_done(void) {
  s_loading = false;
  if (s_list_menu) {
    menu_layer_reload_data(s_list_menu);
  }
}
