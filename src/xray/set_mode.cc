/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "./tests.h"
#include "./utils.h"

// Test XF86VidMode
static bool test_set_mode_xf86vm() {
  Display *d;
  int s;
  bool success = true;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);

  int num_modes;
  XF86VidModeModeInfo **modes;

  // If we don't support the extension, we succeed.
  if (!XF86VidModeGetAllModeLines(d, s, &num_modes, &modes))
    return true;

  if (num_modes == 0)
    return true;

  int i = 0;    // the only mode guaranteed is 0 :-(
  XF86VidModeSwitchToMode(d, s, modes[i]);
  XF86VidModeSetViewPort(d, s, 0, 0);
  success &= utils_x11_flush(d);
  success &=
    utils_check_dimensions(d, RootWindow(d, s), modes[i]->hdisplay,
          modes[i]->vdisplay);

  XFree(modes);

  return success;
}

ADD_TEST(test_set_mode_xf86vm);

// Test Xrandr
static bool test_set_mode_xrandr() {
  Display *d;
  int s;
  bool success = true;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  Window root = RootWindow(d, s);

  XRRScreenConfiguration *screen_config = XRRGetScreenInfo(d, root);
  if (!screen_config)
    return false;

  int num_modes;
  XRRScreenSize *modes = XRRConfigSizes(screen_config, &num_modes);
  if (!modes) {
    XRRFreeScreenConfigInfo(screen_config);
    return false;
  }
  // We try the largest mode (TODO: go back & forth between modes)
  int mode_id = 0;

  XRRSetScreenConfig(d, screen_config, root, mode_id, RR_Rotate_0, CurrentTime);

  success &= utils_x11_flush(d);
  success &=
    utils_check_dimensions(d, RootWindow(d, s), modes[mode_id].width,
          modes[mode_id].height);

  XRRFreeScreenConfigInfo(screen_config);
  return success;
}

ADD_TEST(test_set_mode_xrandr);
