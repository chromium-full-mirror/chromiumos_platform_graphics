/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "includes.h"
#include "tests.h"
#include "utils.h"

// Test fullscreen with _NET_WM_STATE_FULLSCREEN

static bool test_net_wm_state_fullscreen ()
{
	Display *d;
	Window w;
	int s;

	d = XOpenDisplay (NULL);
	s = DefaultScreen (d);
	w = XCreateSimpleWindow (d, RootWindow (d, s), 200, 300, 200, 200, 1, BlackPixel (d, s),
				 WhitePixel (d, s));
	XSelectInput (d, w, ExposureMask | KeyPressMask);

	Atom atoms[1];
	atoms[0] = XInternAtom (d, "_NET_WM_STATE_FULLSCREEN", False);

	Atom property = XInternAtom (d, "_NET_WM_STATE", False);
	XChangeProperty (d, w, property, XA_ATOM, 32, PropModeReplace, (unsigned char *) atoms, 1);

	XMapWindow (d, w);

	utils_x11_flush (d);

	XCloseDisplay (d);

	return true;
}

ADD_TEST (test_net_wm_state_fullscreen);
