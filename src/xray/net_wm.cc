/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "./tests.h"
#include "./utils.h"

// TODO(b/321101802): All of these tests pass without verification.
// Some of that can be done programmatically by checking Wayland state,
// but I believe some (like skip_taskbar) might need a more involved solution
// like tast's screenshot comparison.

// Test makes fullscreen window with _NET_WM_STATE_MAXIMIZED_HORZ
// and _NET_WM_STATE_MAXIMIZED_VERT
static bool test_net_wm_state_maximized() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 200, 300, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  Atom message_type = XInternAtom(d, "_NET_WM_STATE", False);
  Atom max_horz = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  Atom max_vert = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_VERT", False);

  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = _NET_WM_STATE_ADD;
  ev.xclient.data.l[1] = max_horz;
  ev.xclient.data.l[2] = max_vert;

  bool success = true;
  if (!XSendEvent(d, DefaultRootWindow(d), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);
  XWindowAttributes win_attr;
  XGetWindowAttributes(d, DefaultRootWindow(d), &win_attr);
  success &=
      utils_check_dimensions_at_least(d, w, win_attr.width,
            win_attr.height - 100);  // At least display height minus taskbar.

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_wm_state_maximized);

// Test _NET_WM_STATE_SKIP_TASKBAR.
// TODO(mrfemi): Need to verify taskbar was skipped. Possibly through a
// screenshot in tast. Might be good to log a warning that the test always
// passes.
static bool test_net_wm_state_skip_taskbar() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 200, 300, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  Atom message_type = XInternAtom(d, "_NET_WM_STATE", False);
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);

  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = _NET_WM_STATE_ADD;
  ev.xclient.data.l[1] = atom;

  bool success = true;
  if (!XSendEvent(d, DefaultRootWindow(d), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_wm_state_skip_taskbar);

// Test keeping window on top of other windows.
static bool test_net_wm_state_above() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, 1080, 1080, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  Atom message_type = XInternAtom(d, "_NET_WM_STATE", False);
  Atom atom = XInternAtom(d, "_NET_WM_STATE_ABOVE", False);

  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = _NET_WM_STATE_ADD;
  ev.xclient.data.l[1] = atom;

  bool success = true;
  if (!XSendEvent(d, DefaultRootWindow(d), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_wm_state_above);

// Test keeping window below other windows.
static bool test_net_wm_state_below() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 200, 300, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  Atom message_type = XInternAtom(d, "_NET_WM_STATE", False);
  Atom atom = XInternAtom(d, "_NET_WM_STATE_BELOW", False);

  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = _NET_WM_STATE_ADD;
  ev.xclient.data.l[1] = atom;

  bool success = true;
  if (!XSendEvent(d, DefaultRootWindow(d), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_wm_state_below);

// Test making fullscreen window with _net_wm_moveresize_window.
static bool test_net_moveresize_window() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 200, 300, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  Atom message_type = XInternAtom(d, "_NET_MOVERESIZE_WINDOW", False);

  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 1 << 2 | 1 << 3 | 1 << 8 | 1 << 9 | 1 << 10 | 1 << 11
    | 1 <<12;
  ev.xclient.data.l[1] = 0;
  ev.xclient.data.l[2] = 0;

  // XGetWIndowAttributes is currently more useful for getting the display
  // resolution.
  XWindowAttributes win_attr;
  XGetWindowAttributes(d, DefaultRootWindow(d), &win_attr);
  ev.xclient.data.l[3] = win_attr.width;
  ev.xclient.data.l[4] = win_attr.height;

  bool success = true;
  if (!XSendEvent(d, DefaultRootWindow(d), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);
  success &= utils_check_dimensions(d, w, win_attr.width, win_attr.height);
  XCloseDisplay(d);
  return success;
}

ADD_TEST(test_net_moveresize_window);

// Test _NET_ACTIVE_WINDOW.
static bool test_net_active_window() {
  Display *d;
  Window w;
  int s;

  d = XOpenDisplay(NULL);
  s = DefaultScreen(d);
  w = XCreateSimpleWindow(d, RootWindow(d, s), 200, 300, 200, 200, 1,
      BlackPixel(d, s), WhitePixel(d, s));
  XSelectInput(d, w, ExposureMask | KeyPressMask);
  XMapWindow(d, w);

  Atom message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);

  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 1;
  ev.xclient.data.l[1] = 0;
  ev.xclient.data.l[2] = w;

  bool success = true;
  if (!XSendEvent(d, DefaultRootWindow(d), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_active_window);
