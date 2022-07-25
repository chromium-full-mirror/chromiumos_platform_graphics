// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

func getAMDPCIIDMap() map[string]GPUFamily {
	return map[string]GPUFamily{
		"0x1506": "gc_10_3_7",
		"0x15d8": "picasso",
		"0x15e7": "cezanne",
		"0x1638": "cezanne",
		"0x9870": "carrizo",
		"0x9874": "carrizo",
		"0x9875": "carrizo",
		"0x9876": "carrizo",
		"0x9877": "carrizo",
		"0x98e4": "stoney",
	}
}
