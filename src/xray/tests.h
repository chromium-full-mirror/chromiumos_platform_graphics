/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_XRAY_TESTS_H_
#define SRC_XRAY_TESTS_H_

#include <map>
#include <string>

#include "./includes.h"

#define ADD_TEST(f) static int test_gen_##f = test_register(#f, f)

struct test {
  char name[128];
  bool (*run)(void);
};

int test_register(const char *name, bool (*func)(void));
extern std::map<std::string, test> test_list;

#endif  // SRC_XRAY_TESTS_H_
