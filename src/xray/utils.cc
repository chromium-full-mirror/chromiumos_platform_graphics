/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "utils.h"  // NOLINT(build/include_directory)

#include <string>

static bool global_verbose = false;

void utils_set_verbose_logs(bool enabled) {
  global_verbose = enabled;
}

void print_on_verbose(const char * str) {
  if (global_verbose)
    std::cout << str << std::endl;
}

void log_rect(std::string detail, int x, int y, int xx, int yy) {
  if (global_verbose) {
    std::cout << "  Expected " << detail << ": (" << x << ", " << y
        << "), got: (" << xx << ", " << yy << ")" << std::endl;
  }
}

// This function creates a window then immediately maps it then flushes.
Window utils_create_simple_window(Display *d, int x, int y,
    int width, int height, int border_width) {
  if (d == nullptr) {
    return 0;
  }
  int s = DefaultScreen(d);
  Window w = XCreateSimpleWindow(d, RootWindow(d, s), x, y, width, height,
      border_width, BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);
  utils_x11_flush(d);
  return w;
}

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
  log_rect("width, height greater than", width, height, win_attr.width,
      win_attr.height);

  return (width <= win_attr.width) && (height <= win_attr.height);
}

bool utils_check_dimensions_at_most(Display *d, Window w, int width,
  int height) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);
  log_rect("width, height less than", width, height, win_attr.width,
      win_attr.height);

  return (width >= win_attr.width) && (height >= win_attr.height);
}

bool utils_check_dimensions(Display *d, Window w, int width, int height) {
  XWindowAttributes win_attr;

  XGetWindowAttributes(d, w, &win_attr);
  log_rect("width, height equal", width, height, win_attr.width,
      win_attr.height);

  return (width == win_attr.width) && (height == win_attr.height);
}

bool utils_check_position(Display *d, Window w, int screen, int x, int y,
    int epsilon) {
  int xx, yy;
  Window root = RootWindow(d, screen);
  Window child;

  XTranslateCoordinates(d, w, root, 0, 0, &xx, &yy, &child);
  log_rect("xpos, ypos equal", x, y, xx, yy);

  return (x - xx) * (x - xx) <= epsilon
      && (y - yy) * (y - yy) <= epsilon;
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
