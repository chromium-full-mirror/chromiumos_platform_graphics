/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "./tests.h"
#include "./utils.h"

int main(int argc, char *argv[]) {
  bool list_all_tests = false;
  std::vector<test> requested_tests;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    // TODO(mrfemi): add --help option
    if (arg == "--list-all") {
      list_all_tests = true;
      break;
    } else if (arg == "--verbose") {
      utils_set_verbose_logs(true);
    } else if (test_list.find(arg) != test_list.end()) {
      requested_tests.push_back(test_list.find(arg)->second);
    } else {
      std::cout << "Error: Parsing command line - check args: "
         << arg << std::endl;
      return -1;
    }
  }

  if (list_all_tests) {
    int i = 0;
    std::cout << "Test List:" << std::endl;
    for (auto t = test_list.begin(); t != test_list.end(); ++t) {
      std::cout << "  " << t->first << std::endl;
      ++i;
    }
    return 0;
  }

  if (requested_tests.empty()) {
    for (auto t = test_list.begin(); t != test_list.end(); ++t) {
      requested_tests.push_back(t->second);
    }
  }

  int passed = 0;
  int tried = 0;

  std::cout << "\nRunning " << requested_tests.size() << " tests" << std::endl;
  for (auto t = requested_tests.begin(); t != requested_tests.end(); ++t) {
    tried++;
    std::cout << "[ RUN      ] " << (*t).name << std::endl;
    if (!(*t).run()) {
      std::cout << "[  FAILED  ] " << (*t).name << std::endl;
    } else {
      std::cout << "[       OK ] " << (*t).name << std::endl;
      passed++;
    }
  }

  std::cout << "[  PASSED  ] " << passed << " tests." << std::endl;
  std::cout << "[  FAILED  ] " << tried - passed << " tests." << std::endl;
  if (tried - passed) return 1;
  return 0;
}
