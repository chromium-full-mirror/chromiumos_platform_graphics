/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_XRAY_UTILS_H_
#define SRC_XRAY_UTILS_H_

#include "./includes.h"

#define _NET_WM_STATE_REMOVE  0
#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE  2

void utils_set_verbose_logs(bool enabled);
void print_on_verbose(const char *);
Window utils_create_simple_window(Display *d, int x, int y,
    int width, int height, int border_width);
bool utils_x11_flush(Display * d);
bool utils_check_dimensions(Display * d, Window w, int width, int height);
bool utils_check_dimensions_at_least(Display * d, Window w, int width,
    int height);
bool utils_check_dimensions_at_most(Display * d, Window w, int width,
    int height);
bool utils_check_position(Display * d, Window w, int screen, int x, int y,
        int epsilon = 0);
bool utils_check_mapped(Display * d, Window w);
bool utils_check_unmapped(Display * d, Window w);

#endif  // SRC_XRAY_UTILS_H_
