// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"fmt"
	"github.com/pkg/errors"
	"strconv"
	"strings"
)

type Version struct {
	n []int
}

// String returns the string representation of the Version, e.g. 1.3.21
func (v Version) String() string {
	return fmt.Sprintf("%v", strings.Trim(strings.Join(strings.Fields(fmt.Sprint(v.n)), "."), "[]"))
}

// MarshalJSON marshals the enum as a quoted json string.
func (v *Version) MarshalJSON() ([]byte, error) {
	presentation := fmt.Sprintf("\"%v\"", strings.Trim(strings.Join(strings.Fields(fmt.Sprint(v.n)), "."), "[]"))
	return []byte(presentation), nil
}

func NewVersion(str string) (result Version, resultErr error) {
	segments := strings.Split(str, ".")
	if len(segments) == 0 {
		return result, errors.Errorf("invalid version string: %v", str)
	}

	for _, segment := range segments {
		v, err := strconv.Atoi(segment)
		if err != nil {
			return result, errors.Wrapf(err, "failed to parse %v to integer", segment)
		}
		result.n = append(result.n, v)
	}
	return result, nil
}
