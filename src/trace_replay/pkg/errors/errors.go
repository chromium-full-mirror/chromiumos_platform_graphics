// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package errors

// Package errors provides basic utilities to construct errors.
//
// To construct new errors or wrap other errors, use this package rather than
// standard libraries (errors.New, fmt.Errorf) or any other third-party
// libraries. This package records line number and file executed.

import (
	"errors"
	"fmt"
	"path"
	"runtime"
)

// Werror implements the error interface and contains additional info for debugging.
type Werror struct {
	msg   string // error message to be prepend to cause.
	file  string // filename of the line where the error is generated.
	line  int    // line of the file where the error is generated.
	cause error  // original error that cuased this error if non-nil
}

//func (e *Werror) Wrap()

// Error implements the error interface.
func (e *Werror) Error() string {
	if e.cause == nil {
		return fmt.Sprintf("%v [%s:%d]", e.msg, e.file, e.line)
	}
	return fmt.Sprintf("%s [%s:%d]>%s", e.msg, e.file, e.line, e.cause.Error())
}

// New creates a new error with the given message.
// This is similar to the standard errors.New, but also records the location where it was called.
func New(format string, args ...interface{}) *Werror {
	_, file, line, ok := runtime.Caller(1)
	if !ok {
		file = "unknown"
	}
	msg := fmt.Sprintf(format, args...)
	return &Werror{msg, path.Base(file), line, nil}
}

// Wrap creates a new error with the given message, wrapping another error.
// This function is similar to standard errors.Wrap but also records the location where it was called.
func Wrap(cause error, format string, args ...interface{}) *Werror {
	_, file, line, ok := runtime.Caller(1)
	if !ok {
		file = "unknown"
	}
	msg := fmt.Sprintf(format, args...)
	return &Werror{msg, path.Base(file), line, cause}
}

// As is a wrapper of built-in errors.As. It finds the first error in err's
// chain that matches target, and if so, sets target to that error value and
// returns true.
func As(err error, target interface{}) bool {
	return errors.As(err, target)

}

// Is is a wrapper of built-in errors.Is. It reports whether any error in err's
// chain matches target.
func Is(err, target error) bool {
	return errors.Is(err, target)

}
