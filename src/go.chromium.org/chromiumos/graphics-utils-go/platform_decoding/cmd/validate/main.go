// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"regexp"
	"strings"
)

// extractChecksumData extracts MD5 checksum data
func extractChecksumData(input []string) []string {
	var data []string
	re := regexp.MustCompile("^[0-9a-fA-F]{32}$")
	for i := range input {
		// Checks to see if string is a MD5 checksum
		if re.MatchString(input[i]) {
			data = append(data, input[i])
		}
	}

	return data
}

// readMetadata reads metadata from metadata json.
func readMetadata(metadataPath string) (map[string]interface{}, error) {
	metadataJSONBytes, err := os.ReadFile(metadataPath)
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

	// Extracts MD5 checksum data from data source
	actual := strings.Split(strings.TrimSpace(actualOutput), "\n")
	for i := range actual {
		actual[i] = strings.TrimSpace(actual[i])
	}
	actual = extractChecksumData(actual)

	if len(expected) != len(actual) {
		return fmt.Errorf("expected and actual number of frames mismatched (%d != %d)", len(expected), len(actual))
	}

	var first string
	var count int
	for i, ex := range expected {
		if _, ok := ex.(string); !ok {
			return fmt.Errorf("failed to cast expected hash %v of type %T to string", ex, ex)
		}
		if got, wanted := actual[i], strings.TrimSpace(ex.(string)); got != wanted {
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

// exitWithError dumps all output to the appropriate streams and exits with errcode 1.
func exitWithError(err error, stdout, stderr string) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
	}
	if stdout != "" {
		fmt.Fprintln(os.Stdout, stdout)
	}
	if stderr != "" {
		fmt.Fprintln(os.Stderr, stderr)
	}
	os.Exit(1)
}

func main() {
	execPtr := flag.String("exec", "", "path to decoder executable")
	argsPtr := flag.String("args", "", "full args to decoder")
	metaPtr := flag.String("metadata", "", "path to metadata JSON")
	md5Ptr := flag.String("md5", "", "path to md5 checksum file")
	flag.Parse()

	fmt.Printf("Running `%s %s`\n", *execPtr, *argsPtr)
	ctx := context.Background()
	stdout, stderr, err := runDecode(ctx, *execPtr, strings.Fields(*argsPtr)...)
	stdout, stderr = strings.TrimSpace(stdout), strings.TrimSpace(stderr)
	if err != nil {
		exitWithError(err, stdout, stderr)
	}

	if *md5Ptr == "" {
		if err := verifyContent(*metaPtr, stdout); err != nil {
			exitWithError(err, stdout, stderr)
		}
	} else {
		md5Log, err := os.ReadFile(*md5Ptr)
		if err != nil {
			exitWithError(err, stdout, stderr)
		}

		if err := verifyContent(*metaPtr, string(md5Log)); err != nil {
			exitWithError(err, stdout, stderr)
		}
	}

}
