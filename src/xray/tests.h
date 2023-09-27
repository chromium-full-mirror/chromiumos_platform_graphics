/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TESTS_H__
#define __TESTS_H__

#include "includes.h"

#define ADD_TEST(f) static int test_gen_##f = test_register(#f, f)

struct test
{
	char name[128];
	bool (*run) (void);
};

int test_register (const char *name, bool (*func) (void));
extern vector < test > test_list;

#endif
