// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"strings"
)

// readMetadata reads metadata from metadata json.
func readMetadata(metadataPath string) (map[string]interface{}, error) {
	metadataJSONBytes, err := ioutil.ReadFile(metadataPath)
	if err != nil {
		return nil, fmt.Errorf("%w: failed to read metadata file at %s", err, metadataPath)
	}

	var meta map[string]interface{}
	if err = json.Unmarshal(metadataJSONBytes, &meta); err != nil {
		return nil, fmt.Errorf("%w: failed to read json from metadata file at %s", err, metadataPath)
	}

	return meta, nil
}

// verifyContent compares expected per-frame hashes from metadata json to actual
// hashes.
func verifyContent(expectedHashesPath, actualOutput string) error {
	meta, err := readMetadata(expectedHashesPath)
	if err != nil {
		return fmt.Errorf("%w: failed to verify per-frame hashes", err)
	}
	expected, ok := meta["md5_checksums"].([]interface{})
	if !ok {
		return fmt.Errorf("`md5_checksums` in metadata at %s not a slice; got %v", expectedHashesPath, meta["md5_checksums"])
	}

	actual := strings.Split(strings.TrimSpace(actualOutput), "\n")
	if len(expected) != len(actual) {
		return fmt.Errorf("expected and actual number of frames mismatched (%d != %d)", len(expected), len(actual))
	}

	var first string
	var count int
	for i, ex := range expected {
		if _, ok := ex.(string); !ok {
			return fmt.Errorf("failed to cast expected hash %v of type %T to string", ex, ex)
		}
		if got, wanted := strings.TrimSpace(actual[i]), strings.TrimSpace(ex.(string)); got != wanted {
			count++
			if first == "" {
				first = fmt.Sprintf("frame %d (got %s, want %s)", i, got, wanted)
			}
		}
	}

	if count > 0 {
		return fmt.Errorf("%d mismatched hashes, first at %s", count, first)
	}

	return nil
}

// runDecode runs the executable at the given path with the given args,
// returning split output and any errors.
func runDecode(ctx context.Context, execPath string, args ...string) (stdout, stderr string, err error) {
	var outbuf, errbuf bytes.Buffer
	cmd := exec.CommandContext(ctx, execPath, args...)
	cmd.Stdout, cmd.Stderr = &outbuf, &errbuf

	err = cmd.Run()
	stdout, stderr = outbuf.String(), errbuf.String()
	return
}

// exitWithError dumps all output and exits with errcode 1.
func exitWithError(stdout, stderr string, err error) {
	fmt.Println(stdout)
	fmt.Println(stderr)
	fmt.Println(err.Error())
	os.Exit(1)
}

func main() {
	execPtr := flag.String("exec", "", "path to decoder executable")
	argsPtr := flag.String("args", "", "full args to decoder")
	metaPtr := flag.String("metadata", "", "path to metadata JSON")
	flag.Parse()

	fmt.Printf("Running `%s %s`\n", *execPtr, *argsPtr)
	ctx := context.Background()
	stdout, stderr, err := runDecode(ctx, *execPtr, strings.Fields(*argsPtr)...)
	if err != nil {
		exitWithError(stdout, stderr, err)
	}

	if err := verifyContent(*metaPtr, stdout); err != nil {
		exitWithError(stdout, stderr, err)
	}
}
