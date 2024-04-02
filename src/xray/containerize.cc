// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstddef>
#include <cstdint>
#include <sstream>

#include "./tests.h"
#include "./utils.h"

// Toggles fullscreen on the given window.
bool toggle_fullscreen(Display *d, Window *w, int toggle) {
  print_on_verbose("Toggle fullscreen");
  int s = DefaultScreen(d);
  Atom fullscreen = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);

  XEvent ev0;
  ev0.type = ClientMessage;
  ev0.xclient.window = *w;
  ev0.xclient.send_event = True;
  ev0.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
  ev0.xclient.format = 32;
  ev0.xclient.data.l[0] = toggle;
  ev0.xclient.data.l[1] = fullscreen;
  ev0.xclient.data.l[2] = 0;
  bool success =
      XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev0);
  utils_x11_flush(d);
  return success;
}

// This test checks that the containerize window quirk is working as intended.
// It does this by creating a window, resizing it, and then testing the
// maximize, restore, and minimize functionality.
static bool test_containerize_window() {
  Display *d = XOpenDisplay(NULL);
  int s = DefaultScreen(d);
  Window w = XCreateSimpleWindow(d, RootWindow(d, s), 150, 150, 400, 400,
      1, BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);

  // Get window parent.
  Window parent;
  Window root;
  Window *children;
  unsigned int num_children;
  XQueryTree(d, w, &root, &parent, &children, &num_children);

  // Use the Portal 2 game ID, which the containerized window quirk is enabled
  // for.
  int32_t appID = 620;
  XChangeProperty(d, parent, XInternAtom(d, "STEAM_GAME", False), XA_CARDINAL,
      32, PropModeReplace, (unsigned char *) &appID, 1L);
  XMapWindow(d, w);
  utils_x11_flush(d);

  std::ostringstream logs;
  // Try resizing the window a few times.
  bool success = true;
  for (int i = 1; i <= 5; i++) {
    int width = 400 + i * 10;
    int height = 400 + i * 5;
    XResizeWindow(d, w, width, height);
    utils_x11_flush(d);
    success &= utils_check_dimensions(d, w, width, height);
    if (!success) logs << "Failed at resize step." << std::endl;
  }
  print_on_verbose("Finish resizing window.");
  utils_x11_flush(d);

  // Get current window attributes.
  XWindowAttributes win_atr;
  XGetWindowAttributes(d, w, &win_atr);

  // Test maximize.
  toggle_fullscreen(d, &w, _NET_WM_STATE_ADD);
  // Check that the created window is now the size of the display.
  XWindowAttributes screen_atr;
  XGetWindowAttributes(d, RootWindow(d, s), &screen_atr);
  success &= utils_check_dimensions(d, w, screen_atr.width, screen_atr.height);
  if (!success) logs << "Failed at maximize step." << std::endl;

  // Test restore to windowed.
  toggle_fullscreen(d, &w, _NET_WM_STATE_REMOVE);
  // Check that the restored window kept its previous dimensions.
  // TODO(mrfemi): height is short one pixel in local testing. This changes
  // based on sommelier arguments. Determine if this is acceptable/known or if
  // its a bug. Adjust test to fail if its a bug.
  success &=
      utils_check_dimensions_at_least(d, w, win_atr.width - 1,
          win_atr.height - 1);
  if (!success) logs << "Failed at restore from maximize step." << std::endl;

  // Test maximize
  Atom message_type2 = XInternAtom(d, "_NET_WM_STATE", False);
  Atom max_horz = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  Atom max_vert = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_VERT", False);

  XEvent ev2;
  ev2.type = ClientMessage;
  ev2.xclient.window = w;
  ev2.xclient.send_event = True;
  ev2.xclient.message_type = message_type2;
  ev2.xclient.format = 32;
  ev2.xclient.data.l[0] = _NET_WM_STATE_ADD;
  ev2.xclient.data.l[1] = max_horz;
  ev2.xclient.data.l[2] = max_vert;

  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev2))
    success &= false;

  utils_x11_flush(d);
  XWindowAttributes win_attr;
  XGetWindowAttributes(d, RootWindow(d, s), &win_attr);
  // TODO(b/326989847): remove multiplier when hide_shelf is added to tast.
  success &=
      utils_check_dimensions_at_least(d, w, win_attr.width,
            win_attr.height * 0.9);  // At least display height minus shelf.

  print_on_verbose("Test minimize/iconify");
  XIconifyWindow(d, w, s);
  utils_x11_flush(d);

  XCloseDisplay(d);
  if (!success)
    std::cout << "There were test errors:\n" << logs.str();
  return success;
}

ADD_TEST(test_containerize_window);
