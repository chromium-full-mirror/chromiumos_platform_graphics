/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "./tests.h"
#include "./utils.h"

// Test size hints
static bool test_min_size_hints() {
  Display *d;
  Window w;
  int s;
  bool success = true;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 10, 10, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  XSizeHints *sh;
  sh = XAllocSizeHints();
  sh->flags = PMinSize;
  sh->min_width = 400;
  sh->min_height = 400;
  XSetWMSizeHints(d, w, sh, XA_WM_NORMAL_HINTS);
  XFree(sh);

  success &= utils_x11_flush(d);

  success &= utils_check_dimensions_at_least(d, w, sh->min_width,
      sh->min_height);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_min_size_hints);

static bool test_max_size_hints() {
  Display *d;
  Window w;
  int s;
  bool success = true;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 10, 10, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  XSizeHints *sh;
  sh = XAllocSizeHints();
  sh->flags = PMaxSize;
  sh->max_width = 20;
  sh->max_height = 20;
  XSetWMSizeHints(d, w, sh, XA_WM_NORMAL_HINTS);
  XFree(sh);

  success &= utils_x11_flush(d);

  success &= utils_check_dimensions_at_most(d, w, sh->max_width,
      sh->max_height);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_max_size_hints);

static bool test_min_max_size_hints() {
  Display *d;
  Window w;
  int s;
  bool success = true;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 10, 10, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  XSizeHints *sh;
  sh = XAllocSizeHints();
  sh->flags = PMinSize | PMaxSize;
  sh->min_width = sh->max_width = 200;
  sh->min_height = sh->max_height = 200;
  XSetWMSizeHints(d, w, sh, XA_WM_NORMAL_HINTS);
  XFree(sh);

  success &= utils_x11_flush(d);

  success &= utils_check_dimensions(d, w, sh->min_width, sh->min_height);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_min_max_size_hints);
