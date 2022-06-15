// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package utils contains utils function to run trace_replay.
package utils

import (
	"context"
	"io"
)

type readerFunc func(p []byte) (n int, err error)

func (rf readerFunc) Read(p []byte) (n int, err error) { return rf(p) }

// CopyWithContext is a io.Copy wrapper which aborts copy operation and returns a timeout error if context
// deadline occurred earlier
func CopyWithContext(ctx context.Context, writer io.Writer, reader io.Reader) error {
	_, err := io.Copy(writer, readerFunc(func(p []byte) (int, error) {
		select {
		case <-ctx.Done():
			return 0, ctx.Err()
		default:
			return reader.Read(p)
		}
	}))
	return err
}
