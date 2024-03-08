// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "./tests.h"
#include "./utils.h"

// test_always_pass is an empty function. This test should always pass,
// it is short and simple so it can be run in the CQ.
static bool test_always_pass() {
  return true;
}

ADD_TEST(test_always_pass);

// test_smoke makes the bare minimum calls to Xserver then exits.
// This test should always pass, it is short and simple so it can be run in the
// CQ.
static bool test_smoke() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 10, 10, 50, 50, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  return true;
}

ADD_TEST(test_smoke);
