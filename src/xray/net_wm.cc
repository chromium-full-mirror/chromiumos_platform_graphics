/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstddef>
#include <cstring>
#include <sstream>

#include "./tests.h"
#include "./utils.h"

// Test makes fullscreen window with _NET_WM_STATE_MAXIMIZED_HORZ
// and _NET_WM_STATE_MAXIMIZED_VERT
static bool test_net_wm_state_maximized() {
  Display *d = XOpenDisplay(NULL);
  Window w = utils_create_simple_window(d, 200, 300, 200, 200, 1);
  int s = DefaultScreen(d);

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
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);
  XWindowAttributes win_attr;
  XGetWindowAttributes(d, RootWindow(d, s), &win_attr);
  // TODO(b/326989847): The atom does not expand the window to cover the shelf.
  // We should hide the shelf in the Tast test so that it is not a factor when
  // checking dimensions for pass/fail.
  success &=
      utils_check_dimensions_at_least(d, w, win_attr.width,
            win_attr.height * 0.9);  // At least display height minus shelf.

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_wm_state_maximized);

// Test _NET_WM_STATE_SKIP_TASKBAR.
// TODO(b/321101802): Need to verify shelf was skipped. Possibly through a
// screenshot in tast. Might be good to log a warning that the test always
// passes.
static bool test_net_wm_state_skip_taskbar() {
  Display *d = XOpenDisplay(NULL);
  Window w = utils_create_simple_window(d, 200, 300, 200, 200, 1);
  int s = DefaultScreen(d);

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
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);

  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_wm_state_skip_taskbar);

// Log the ID of windows in the given list and return as std::ostringstream.
static std::ostringstream log_windows(unsigned int num_windows,
    Window *windows) {
  std::ostringstream logs;
  for (unsigned int i = 0; i < num_windows; ++i) {
    logs << "  window[" << i << "]: " << windows[i] << std::endl;
  }
  return logs;
}

// For the given window, retrieve a handle to its parent as well as the list of
// children to the root. Store both the pointer to the parent and children in
// the passed in args. Then return log a of all the aformentioned window IDs.
static std::ostringstream get_parent_and_children(Display *d, Window *w,
    Window *parent, Window **children, unsigned int *num_children) {
  Window root;
  Window root_parent;

  // Capture logs in a variable
  std::ostringstream logs;
  logs << "  window: " << *w << std::endl;

  // Get the parent of the created window.
  XQueryTree(d, *w, &root, parent, children, num_children);
  logs << "  window parent: " << *parent << std::endl;

  // Get the list of child windows to the root in order from bottom to top.
  XQueryTree(d, DefaultRootWindow(d), &root, &root_parent, children,
      num_children);
  logs << "  root children: " << std::endl;
  logs << log_windows(*num_children, *children).str();

  return logs;
}

// Test keeping window on top of other windows.
static bool test_net_wm_state_above() {
  Display *d = XOpenDisplay(NULL);
  Window w = utils_create_simple_window(d, 0, 0, 1080, 1080, 1);
  int s = DefaultScreen(d);

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
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);
  Window parent;
  Window *children;
  unsigned int num_children;
  std::ostringstream logs =
      get_parent_and_children(d, &w, &parent, &children, &num_children);

  // Parent window should be at the top, so check the end of the list.
  success &= num_children > 0 && (parent == children[num_children-1]);

  XCloseDisplay(d);
  if (!success)
    std::cout << logs.str();
  return success;
}

ADD_TEST(test_net_wm_state_above);

// Test keeping window below other windows.
static bool test_net_wm_state_below() {
  Display *d = XOpenDisplay(NULL);
  Window w = utils_create_simple_window(d, 200, 300, 200, 200, 1);
  int s = DefaultScreen(d);

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
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);
  Window parent;
  Window *children;
  unsigned int num_children;
  std::ostringstream logs =
      get_parent_and_children(d, &w, &parent, &children, &num_children);

  // Parent window should be at the bottom, so check the start of the list.
  success &= num_children > 0 && (parent == children[0]);

  XCloseDisplay(d);
  if (!success)
    std::cout << logs.str();
  return success;
}

ADD_TEST(test_net_wm_state_below);

// Test making fullscreen window with _net_wm_moveresize_window.
static bool test_net_moveresize_window() {
  Display *d = XOpenDisplay(NULL);
  Window w = utils_create_simple_window(d, 200, 300, 200, 200, 1);
  int s = DefaultScreen(d);

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
  XGetWindowAttributes(d, RootWindow(d, s), &win_attr);
  ev.xclient.data.l[3] = win_attr.width;
  ev.xclient.data.l[4] = win_attr.height;

  bool success = true;
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;

  utils_x11_flush(d);
  success &= utils_check_dimensions(d, w, win_attr.width, win_attr.height);
  XCloseDisplay(d);
  return success;
}

ADD_TEST(test_net_moveresize_window);

// Test _NET_ACTIVE_WINDOW. This test creates a window and sends a
// _NET_ACTIVE_WINDOW message hint to the XServer. Sommelier is expected to
// intercept that message and should send a message to Exo signaling that this
// window, or its parent, should be the focused window (ready to receive
// keyboard input).
// BUT only the Steam app is allowed to self activate for security reasons.
// This test asserts that a non-steam window cannot self-activate.
static bool test_net_active_window() {
  Display *d = XOpenDisplay(NULL);
  int s = DefaultScreen(d);
  Window w1 = utils_create_simple_window(d, 200, 300, 200, 200, 1);
  // Create a new window so w1 isn't active by default.
  Window w2 = utils_create_simple_window(d, 200, 300, 400, 400, 1);

  Atom message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w1;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 1;

  bool success = true;
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;
  utils_x11_flush(d);

  Window focused_window;
  int revert_to;
  XGetInputFocus(d, &focused_window, &revert_to);
  // Normal (non steam) borealis windows are not allowed to self-activate
  // so this test fails if w1 regains focus.
  if (w1 == focused_window || w2 != focused_window)
    success &= false;

  utils_x11_flush(d);
  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_active_window);

// Test _NET_ACTIVE_WINDOW. This test is nearly identical to
// test_net_active_window() except it spoofs the Steam app, which should allow
// the window to self-activate.
static bool test_net_active_window_steam() {
  Display *d = XOpenDisplay(NULL);
  int s = DefaultScreen(d);
  Window w1 = XCreateSimpleWindow(d, RootWindow(d, s), 200, 300, 200, 200,
      1, BlackPixel(d, s), WhitePixel(d, s));

  // Use Steam game ID before mapping window to allow self-activation.
  int32_t appID = 769;
  // char name[] = "Steam";
  XChangeProperty(d, w1, XInternAtom(d, "STEAM_GAME", False), XA_CARDINAL,
      32, PropModeReplace, (unsigned char *) &appID, 1L);
  XMapWindow(d, w1);
  utils_x11_flush(d);

  // Create a new window so w1 isn't active by default.
  Window w2 = utils_create_simple_window(d, 200, 300, 400, 400, 1);

  Atom message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
  XEvent ev;
  ev.type = ClientMessage;
  ev.xclient.window = w1;
  ev.xclient.send_event = True;
  ev.xclient.message_type = message_type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 1;

  bool success = true;
  if (!XSendEvent(d, RootWindow(d, s), False, SubstructureNotifyMask, &ev))
    success &= false;
  utils_x11_flush(d);

  Window focused_window;
  int revert_to;
  XGetInputFocus(d, &focused_window, &revert_to);
  // Normal (non steam) borealis windows are not allowed to self-activate
  // so we expect w1 to be the active window.
  if (w1 != focused_window || w2 == focused_window)
    success &= false;

  utils_x11_flush(d);
  XCloseDisplay(d);

  return success;
}

ADD_TEST(test_net_active_window_steam);
