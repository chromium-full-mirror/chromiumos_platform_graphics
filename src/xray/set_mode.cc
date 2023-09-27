/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "includes.h"
#include "tests.h"
#include "utils.h"

// Test XF86VidMode

static bool test_set_mode_xf86vm ()
{
	Display *d;
	int s;
	bool success = true;

	d = XOpenDisplay (NULL);
	s = DefaultScreen (d);

	int num_modes;
	XF86VidModeModeInfo **modes;

	// If we don't support the extension, we succeed.
	if (!XF86VidModeGetAllModeLines (d, s, &num_modes, &modes))
		return true;

	if (num_modes == 0)
		return true;

	int i = 0;		// the only mode guaranteed is 0 :-(
	XF86VidModeSwitchToMode (d, s, modes[i]);
	XF86VidModeSetViewPort (d, s, 0, 0);
	success &= utils_x11_flush (d);
	success &=
		utils_check_dimensions (d, RootWindow (d, s), modes[i]->hdisplay,
					modes[i]->vdisplay);

	XFree (modes);

	return success;
}

ADD_TEST (test_set_mode_xf86vm);

// Test Xrandr
static bool test_set_mode_xrandr ()
{
	Display *d;
	Window w;
	int s;
	bool success = true;

	d = XOpenDisplay (NULL);
	s = DefaultScreen (d);

	// TODO

	return success;
}

ADD_TEST (test_set_mode_xrandr);
