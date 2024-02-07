// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package util

import (
	"fmt"
	"github.com/pkg/errors"
	"os"
	"path/filepath"
	"strings"
)

const (
	// Maximum DRM device minor number.
	maxDRMDeviceNumber = 64
)

// GetValidKernelDriverDebugFile search the open nodes in /sys/kernel/debug/dri with a list of files, and returns the first path which exists.
func GetValidKernelDriverDebugFile(relPaths []string) (string, error) {
	resultErr := errors.Errorf("failed to find %v", relPaths)
	for _, relPath := range relPaths {
		p, err := getKernelDriverDebugFile(relPath)
		if err != nil {
			resultErr = errors.Wrap(err, resultErr.Error())
			continue
		}
		return p, nil
	}
	return "", resultErr
}

// getKernelDriverDebugFile search the open nodes in /sys/kernel/debug/dri and returns the a valid path.
func getKernelDriverDebugFile(relPath string) (string, error) {
	sysPath := "/sys/kernel/debug/dri/"
	paths, err := filepath.Glob(filepath.Join(sysPath, "*", relPath))
	if err != nil || paths == nil {
		return "", errors.Wrap(err, "failed to glob")
	}
	for _, path := range paths {
		// Skip if minor number is greater than 64.
		var minor uint64
		if _, err := fmt.Sscanf(path, sysPath+"%d", &minor); err != nil {
			// testing.ContextLogf(ctx, "Failed to determine the minor number of %v, skipping", path)
			continue
		}
		if minor >= maxDRMDeviceNumber {
			// testing.ContextLogf(ctx, "Minor number %d > %d, skipping reading %v in it", minor, maxDRMDeviceNumber, relPath)
			continue
		}

		name, err := os.ReadFile(fmt.Sprintf("%v/%v/name", sysPath, minor))
		if err != nil {
			return "", errors.Wrap(err, "failed to read driver name")
		}
		// Skipping virtual gem object.
		if strings.HasPrefix(string(name), "vgem") {
			continue
		}
		// Try read the content, sometimes the file exist but device is not.
		if _, err := os.ReadFile(path); err != nil {
			return "", errors.Wrap(err, "file exist but not readable")
		}
		return path, nil
	}
	return "", errors.Errorf("can't find any %v in kernel", relPath)
}
