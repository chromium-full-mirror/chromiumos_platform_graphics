/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "./tests.h"
#include "./utils.h"

// Test window resizing. This is not a part of the spec, but we
// want to guarantee this behavior as applications rely on it.

static bool test_resize() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 10, 10, 50, 50, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);

  XMapWindow(d, w);

  bool success = true;
  for (int i = 0; i < 5; i++) {
    int width = 100 + i * 10;
    int height = 100 + i * 5;
    XResizeWindow(d, w, width, height);
    utils_x11_flush(d);
    success &= utils_check_dimensions(d, w, width, height);
  }

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_resize);
