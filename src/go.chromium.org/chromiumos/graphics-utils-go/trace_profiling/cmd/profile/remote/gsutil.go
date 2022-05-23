// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package remote

import (
	"fmt"
	"os/exec"
	"strings"
)

// IsGoogleStorageURI returns whether the URI string points to a file in
// Google storage.
func IsGoogleStorageURI(uri string) bool {
	return strings.HasPrefix(uri, "gs://")
}

// FetchFromGS copies a file from Google Storage URI to a local path.
// Returns the output from command gsutil and an optional error.
func FetchFromGS(localPath, gsURI string) ([]byte, error) {
	cmd := exec.Command("gsutil", "cp", gsURI, localPath)
	result, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("'gsutil cp' failed: err = %s", err)
	}
	return result, nil
}
