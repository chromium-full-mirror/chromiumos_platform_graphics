/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "includes.h"
#include "tests.h"

int main (int argc, char *argv[])
{
	bool list_all_tests = false;
	vector<test> requested_tests;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		// TODO(mrfemi): add --help option
		if (arg == "--list-all") {
			list_all_tests = true;
			break;
		} else if (test_list.find(arg) != test_list.end()){
			requested_tests.push_back(test_list.find(arg)->second);
		} else {
			cout << "Error: Parsing command line - check args: "
				 << arg << std::endl;
			return -1;
		}
	}

	if (list_all_tests) {
		int i = 0;
		cout << "Test List:" << endl;
		for (auto t = test_list.begin (); t != test_list.end (); ++t) {
			cout << "  " << t->first << endl;
			++i;
		}
		return 0;
	}

	if (requested_tests.empty()) {
		for (auto t = test_list.begin (); t != test_list.end (); ++t) {
			requested_tests.push_back(t->second);
		}
	}

	int passed = 0;
	int tried = 0;

	cout << "\nRunning " << requested_tests.size () << " tests" << endl;
	for (auto t = requested_tests.begin (); t != requested_tests.end (); ++t) {
		tried++;
		cout << "[ RUN      ] " << (*t).name << endl;
		if (!(*t).run ()) {
			cout << "[  FAILED  ] " << (*t).name << endl;
		} else {
			cout << "[       OK ] " << (*t).name << endl;
			passed++;
		}
	}

	cout << "[  PASSED  ] " << passed << " tests." << endl;
	cout << "[  FAILED  ] " << tried - passed << " tests." << endl;
	if (tried - passed) return 1;
	return 0;
}
