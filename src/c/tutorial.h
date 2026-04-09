#pragma once
#include <pebble.h>

// Returns true if tutorial needs to be shown (first launch)
bool tutorial_needed(void);

// Show the tutorial. Calls done_cb when user finishes it.
void tutorial_show(void (*done_cb)(void));
