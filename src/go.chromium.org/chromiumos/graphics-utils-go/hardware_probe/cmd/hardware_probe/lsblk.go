// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
)

// lsblk command output differs depending on the version. Attempt parsing in multiple ways to accept all the cases.

// lsblk from util-linux 2.32
type blockDevices2_32 struct {
	Name      string `json:"name"`
	Removable string `json:"removable"`
	Size      string `json:"size"`
	Type      string `json:"type"`
}

type lsblkRoot2_32 struct {
	BlockDevices []blockDevices2_32 `json:"blockdevices"`
}

// lsblk from util-linux 2.36.1
type blockDevices struct {
	Name      string `json:"name"`
	Removable bool   `json:"removable"`
	Size      int64  `json:"size"`
	Type      string `json:"type"`
}

type lsblkRoot struct {
	BlockDevices []blockDevices `json:"blockdevices"`
}

func parseLsblk2_32(jsonData []byte) (*lsblkRoot, error) {
	var old lsblkRoot2_32
	err := json.Unmarshal(jsonData, &old)
	if err != nil {
		return nil, err
	}

	var r lsblkRoot
	for _, e := range old.BlockDevices {
		s, err := strconv.ParseInt(e.Size, 10, 64)
		if err != nil {
			return nil, err
		}
		var rm bool
		if e.Removable == "0" || e.Removable == "" {
			rm = false
		} else if e.Removable == "1" {
			rm = true
		} else {
			return nil, fmt.Errorf("unknown value for rm: %q", e.Removable)
		}
		r.BlockDevices = append(r.BlockDevices, blockDevices{
			Name:      e.Name,
			Removable: rm,
			Size:      s,
			Type:      e.Type,
		})
	}
	return &r, nil
}

func parseLsblk2_36(jsonData []byte) (*lsblkRoot, error) {
	var r lsblkRoot
	err := json.Unmarshal(jsonData, &r)
	if err != nil {
		return nil, err
	}
	return &r, nil
}

func parseLsblk(jsonData []byte) (*lsblkRoot, error) {
	var errs []error
	parsers := []func([]byte) (*lsblkRoot, error){parseLsblk2_36, parseLsblk2_32}
	for _, p := range parsers {
		r, err := p(jsonData)
		if err == nil {
			return r, nil
		}
		errs = append(errs, err)
	}
	var errStrings []string
	for _, e := range errs {
		errStrings = append(errStrings, e.Error())
	}
	return nil, fmt.Errorf("failed to parse JSON in all the expected formats: %s", strings.Join(errStrings, "; "))
}
