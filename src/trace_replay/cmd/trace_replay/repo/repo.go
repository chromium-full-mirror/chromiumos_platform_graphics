// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package repo contains trace repository configuration related structures.
package repo

// StorageFileInfo struct contains all the necessary storage file information
type StorageFileInfo struct {
	Name      string `json:"Name"`
	Size      uint64 `json:"Size,string"`
	SHA256Sum string `json:"SHA256Sum"`
}

// TraceFileInfo struct contains all the necessary .trace file information
type TraceFileInfo struct {
	Name        string `json:"Name"`
	Size        uint64 `json:"Size,string"`
	MD5Sum      string `json:"MD5Sum"`
	Version     string `json:"Version"`
	FramesCount uint32 `json:"FramesCount,string"`
}

// TraceListEntry struct contains the detailed information about one trace file
type TraceListEntry struct {
	Name        string          `json:"Name"`
	Labels      []string        `json:"Labels"`
	Board       string          `json:"Board"`
	Model       string          `json:"Model"`
	Chipset     string          `json:"Chipset"`
	Time        string          `json:"Time"`
	StorageFile StorageFileInfo `json:"StorageFile"`
	TraceFile   TraceFileInfo   `json:"TraceFile"`
}

// TraceList struct contains the list of trace entries available on repository
type TraceList struct {
	Version string           `json:"Version"`
	Entries []TraceListEntry `json:"Entries"`
}
