/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unistd.h>
#include "./tests.h"
#include "./utils.h"

// Move the window position (+inc_w, +inc_h) for iteration times.
static bool move_loop(Display *d, Window *w, int iterations, int dx, int dy) {
  bool success = true;
  for (int i = 1; i < iterations; i++) {
    int x = i * dx;
    int y = i * dy;
    // TODO(b/325311899): XMoveWindow() moves the window to the correct position
    // the first time around, but for some reason XTranslateCoordinates() only
    // reports the x and y values from the previous XMoveWindow() call. So we
    // call XMoveWindow() twice as a temporary workaround to both move the
    // window and update the reported window position.
    XMoveWindow(d, *w, x, y);
    XMoveWindow(d, *w, x, y);
    utils_x11_flush(d);
    success &= utils_check_position(d, *w, DefaultScreen(d), x, y, 4);
  }
  return success;
}

// Test if the app can move its own window. This window spoofs the STEAM_GAME ID
// of Path of Exile in an attempt to trigger the borealis window positiioning
// quirk enabled for certain games.
static bool test_position() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  // Create window manually instead of using utils_create_simple_window()
  // since we will need to call XMapWindow() after some extra set up.
  w = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, 400, 400,
      1, BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);

  auto size_hints2 = XAllocSizeHints();
  size_hints2->flags = PPosition| PMinSize | PWinGravity;
  size_hints2->min_width = 400;
  size_hints2->max_width = 400;
  size_hints2->min_height = 400;
  size_hints2->max_height = 400;
  size_hints2->win_gravity = NorthWestGravity;
  XSetWMNormalHints(d, w, size_hints2);
  utils_x11_flush(d);
  // Get window parent.
  Window parent;
  Window root;
  Window *children;
  unsigned int num_children;
  XQueryTree(d, w, &root, &parent, &children, &num_children);

  // Use the Path of exile 2 game ID before mapping window.
  // This should enable movement quirk.
  int32_t appID = 238960;
  XChangeProperty(d, w, XInternAtom(d, "STEAM_GAME", False), XA_CARDINAL,
      32, PropModeReplace, (unsigned char *) &appID, 1L);
  XMapWindow(d, w);
  utils_x11_flush(d);

  bool success = move_loop(d, &w, 6, 100, 100);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_position);
