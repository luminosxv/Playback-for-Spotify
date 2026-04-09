#pragma once
#include <pebble.h>

// Commands sent from watch to JS (via Command message key)
typedef enum {
  CMD_FETCH_NOW_PLAYING  = 1,
  CMD_FETCH_PLAYLISTS    = 2,
  CMD_FETCH_ARTISTS      = 3,
  CMD_FETCH_ALBUMS       = 4,
  CMD_FETCH_LIKED_SONGS  = 5,
  CMD_PLAY_PAUSE         = 10,
  CMD_NEXT_TRACK         = 11,
  CMD_PREV_TRACK         = 12,
  CMD_VOLUME_UP          = 13,
  CMD_VOLUME_DOWN        = 14,
  CMD_LIKE_TRACK         = 15,
  CMD_DISLIKE_TRACK      = 16,
  CMD_PLAY_CONTEXT       = 20,
  CMD_FETCH_ART          = 30,
} AppCommand;

// List types
typedef enum {
  LIST_TYPE_PLAYLISTS    = 0,
  LIST_TYPE_ARTISTS      = 1,
  LIST_TYPE_ALBUMS       = 2,
  LIST_TYPE_LIKED_SONGS  = 3
} ListType;

#define MAX_LIST_ITEMS 20

typedef struct {
  char title[40];
  char subtitle[32];
  char uri[80];
} ListItem;
