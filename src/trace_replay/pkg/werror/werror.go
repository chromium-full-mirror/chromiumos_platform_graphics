// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package werror

// Package werror provides basic utilities to construct errors.
//
// To construct new errors or wrap other errors, use this package rather than
// standard libraries (errors.New, fmt.Errorf) or any other third-party
// libraries. This package records line number and file executed.

import (
	"fmt"
	"path"
	"runtime"
)

// Werror is short for Wrapped error. This contains additional information than standard error.
type Werror struct {
	err  error
	msg  string
	file string
	line int
}

// WrapError wraps an error with additional message and file:line information.
func WrapError(err error, format string, args ...interface{}) *Werror {
	_, file, line, ok := runtime.Caller(1)
	if !ok {
		file = "unknown"
	}
	msg := fmt.Sprintf(format, args...)
	return &Werror{err, msg, path.Base(file), line}
}

func (e *Werror) String() string {
	errMsg := "nil"
	if e.err != nil {
		errMsg = e.err.Error()
	}
	return fmt.Sprintf("ERROR: %s (%s)! [%s:%d]", e.msg, errMsg, e.file, e.line)
}
