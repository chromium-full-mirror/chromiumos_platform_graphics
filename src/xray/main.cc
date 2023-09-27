/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "includes.h"
#include "tests.h"

int main (int argc, char *argv[])
{
	int passed = 0;
	int tried = 0;
	cout << "Running " << test_list.size () << " tests" << endl;


	for (auto t = test_list.begin (); t != test_list.end (); ++t) {
		tried++;
		cout << "Running test " << (*t).name << endl;
		if (!(*t).run ()) {
			cout << "Failed." << endl;
		} else {
			cout << "Passed." << endl;
			passed++;
		}
	}

	cout << passed << "/" << tried << " tests passed." << endl;
}
