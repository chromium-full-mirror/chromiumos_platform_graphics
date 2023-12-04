/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "includes.h"
#include "tests.h"

// We set a low init priority so the vector is created before tests are added
// through ADD_TEST()
map < string, test > test_list __attribute__((init_priority (101)));

int test_register (const char *name, bool (*func) (void))
{
	test t;
	t.run = func;
	strcpy (t.name, name);
	test_list.emplace (string(name), t);

	return 0;
}
