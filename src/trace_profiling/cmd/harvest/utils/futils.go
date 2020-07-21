// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// FileExists returns whether file with path filename exists.
func FileExists(filename string) bool {
	fileInfo, err := os.Stat(filename)
	return err == nil && !fileInfo.IsDir()
}

// SplitFileExt splits a filename from all its extensions and returns as pair
// (name, extensions). E.g. filename "blahblah.txt.tar" produces "blahblah" and
// ".txt.tar".
func SplitFileExt(filename string) (string, string) {
	ext := ""
	for {
		e := filepath.Ext(filename)
		if e == "" {
			break
		}
		ext = e + ext
		filename = strings.TrimSuffix(filename, e)
	}

	return filename, ext
}

// ReadJSONData reads JSON data from a file and return it as a byte array.
func ReadJSONData(jsonFilepath string) ([]byte, error) {
	file, err := os.Open(jsonFilepath)
	if err != nil {
		return nil, fmt.Errorf("could not open JSON file <%s>; error=%w", jsonFilepath, err)
	}
	defer file.Close()

	jsonData, err := ioutil.ReadAll(file)
	if err != nil {
		return nil, fmt.Errorf("could not read JSON file <%s>; error=%w", jsonFilepath, err)
	}

	return jsonData, nil
}

// GetFileMD5Hash returns the MD5 hash from file <filename>.
func GetFileMD5Hash(fileName string) (string, error) {
	file, err := os.Open(fileName)
	if err != nil {
		return "", err
	}
	defer file.Close()

	hash := md5.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", err
	}
	hashInBytes := hash.Sum(nil)[:16]

	return hex.EncodeToString(hashInBytes), nil
}

// GetUTCDateString return the UTC date as a string with format "YYYMMDD-HHMMSS".
func GetUTCDateString() string {
	utc := time.Now().UTC()
	return utc.Format("20060102-150405")
}

// GetUTCTimeString return the UTC time as a string with format "HHMMSS".
func GetUTCTimeString() string {
	utc := time.Now().UTC()
	return utc.Format("150405")
}
