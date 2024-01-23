/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tests.h"   // NOLINT(build/include_directory)

// We set a low init priority so the vector is created before tests are added
// through ADD_TEST()
std::map<std::string, test> test_list __attribute__((init_priority(101)));

int test_register(const char *name, bool (*func)(void)) {
  test t;
  t.run = func;
  snprintf(t.name, sizeof(t.name), "%s", name);
  test_list.emplace(std::string(name), t);

  return 0;
}
