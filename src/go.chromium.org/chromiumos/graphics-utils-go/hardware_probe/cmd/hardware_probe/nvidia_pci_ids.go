// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

func getNvidiaPCIIDMap() map[string]string {
	return map[string]string{
		"0x1430": "maxwell",
		"0x1dba": "volta",
		"0x25a0": "ampere",
		"0x25ac": "ampere",
	}
}
