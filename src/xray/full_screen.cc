/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "./tests.h"
#include "./utils.h"

// test fullscreen with the VidMode extension and override redirect
static bool test_fullscreen() {
  Display *d;
  Window w;
  int s;
  bool success = true;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  int width = DisplayWidth(d, s);
  int height = DisplayHeight(d, s);

  Visual *vid;
  vid = DefaultVisual(d, s);

  int depth;
  depth = DefaultDepth(d, s);

  XSetWindowAttributes xattr;
  xattr.override_redirect = True;
  xattr.background_pixel = 0;
  xattr.border_pixel = 0;
  xattr.colormap = XCreateColormap(d, RootWindow(d, s), vid, AllocNone);

  w = XCreateWindow(d, RootWindow(d, s), 0, 0, width, height, 0, depth,
      InputOutput, vid, (CWOverrideRedirect | CWBackPixel | CWBorderPixel |
      CWColormap), &xattr);

  XSelectInput(d, w, ExposureMask | KeyPressMask);

  XSetTransientForHint(d, w, RootWindow(d, s));

  XMapWindow(d, w);

  success &= utils_x11_flush(d);

  success &= utils_check_dimensions(d, w, width, height);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_fullscreen);
