// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package utils contains utils function to run trace_replay.
package utils

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"regexp"
	"syscall"
)

// GetFileMD5Sum calculates MD5 checksum for the specified file
func GetFileMD5Sum(ctx context.Context, fileName string) (string, error) {
	file, err := os.Open(fileName)
	if err != nil {
		return "", err
	}
	defer file.Close()
	hash := md5.New()
	if err = CopyWithContext(ctx, hash, file); err != nil {
		return "", err
	}
	hashInBytes := hash.Sum(nil)[:16]
	return hex.EncodeToString(hashInBytes), nil
}

// FormatSize returns the formatted string for the specified integer value by
// grouping decimals and separate thousands with commas
func FormatSize(siz uint64) string {
	str := fmt.Sprintf("%d", siz)
	re := regexp.MustCompile(`(\d+)(\d{3})`)
	for n := ""; n != str; {
		n = str
		str = re.ReplaceAllString(str, "$1,$2")
	}
	return str
}

// GetFreeSpace returns the available free space at given location in bytes
func GetFreeSpace(path string) (uint64, error) {
	var stat syscall.Statfs_t
	err := syscall.Statfs(path, &stat)
	if err != nil {
		return uint64(0), err
	}

	return stat.Bavail * uint64(stat.Bsize), nil
}

// ClearDirectory deletes all the files and subdirectories in the given directory,
// but keeps the empty directory itself
func ClearDirectory(dir string) error {
	items, err := ioutil.ReadDir(dir)
	if err != nil {
		return err
	}
	for _, item := range items {
		if err = os.RemoveAll(path.Join(dir, item.Name())); err != nil {
			return err
		}
	}
	return nil
}

// MinInt returns the smallest of its two integer arguments
func MinInt(x, y int) int {
	if x < y {
		return x
	}
	return y
}

// MaxInt returns the largest of its two integer arguments
func MaxInt(x, y int) int {
	if x > y {
		return x
	}
	return y
}

// MaxOfInt returns the largest of its integer arguments
func MaxOfInt(num1 int, nums ...int) int {
	v := num1
	for _, num := range nums {
		v = MaxInt(v, num)
	}
	return v
}
