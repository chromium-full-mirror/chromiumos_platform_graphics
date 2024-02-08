/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "utils.h"  // NOLINT(build/include_directory)

bool utils_x11_flush(Display *d) {
  XEvent e;

  XSync(d, False);

  while (XPending(d)) {
    XSync(d, False);
    XNextEvent(d, &e);
  }

  // There is a fantastic alternate reality where the WM updates the
  // window size in finite time, but it's not this one.
  sleep(1);

  return true;
}

bool utils_check_dimensions_at_least(Display *d, Window w, int width,
    int height) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);

  return (width <= win_attr.width) && (height <= win_attr.height);
}

bool utils_check_dimensions_at_most(Display *d, Window w, int width,
  int height) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);

  return (width >= win_attr.width) && (height >= win_attr.height);
}

bool utils_check_dimensions(Display *d, Window w, int width, int height) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);

  printf("%d %d %d %d\n", width, win_attr.width, height, win_attr.height);
  return (width == win_attr.width) && (height == win_attr.height);
}

bool utils_check_position(Display *d, Window w, int screen, int x, int y) {
  int xx, yy;
  Window root = RootWindow(d, screen);
  Window child;

  XTranslateCoordinates(d, w, root, 0, 0, &xx, &yy, &child);

  return (x == xx) && (y == yy);
}

bool utils_check_mapped(Display *d, Window w) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);

  return(win_attr.map_state == IsViewable);
}

bool utils_check_unmapped(Display *d, Window w) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);

  return (win_attr.map_state == IsUnmapped);
}
