// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"bufio"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"regexp"
	"strconv"
	"strings"
	"time"

	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/timestamppb"
)

// Package-global vars.
var (
	// Whether to print warnings to Stderr; true by default.
	FlagEnablePrintWarnings = true
)

// Convert a string to a 32-bit unsigned integer.
func strToUint32(str string) uint32 {
	u64, _ := strconv.ParseUint(str, 10, 32)
	return uint32(u64)
}

// Print a formatted warning string to stderr if enabled.
func printWarningToStderr(format string, a ...interface{}) {
	if FlagEnablePrintWarnings {
		fmt.Fprintf(os.Stderr, format, a...)
	}
}

// WriteProtobuf writes the protobuf data to the output file. If the output file
// is the empty string, the output goes to stdout. The generated output is JSON
// if the file name ends with ".json" or is stdout. Otherwise it is protobuf
// streaming binary format.
func WriteProtobuf(protobuf proto.Message, outputFile string) error {
	var file *os.File = os.Stdout
	var err error

	generateJSON := true
	if outputFile != "" {
		ext := path.Ext(outputFile)
		generateJSON = (ext == ".json")

		file, err = os.Create(outputFile)
		if err != nil {
			return err
		}
		defer file.Close()
	}

	if generateJSON {
		marshaler := protojson.MarshalOptions{Indent: "  "}
		jsonData, err := marshaler.Marshal(protobuf)

		// Write output finishing with a newline.
		if err == nil {
			file.Write(append(jsonData, []byte("\n")...))
		}
	} else {
		data, err := proto.Marshal(protobuf)
		if err == nil {
			_, err = file.Write(data)
		}
	}

	return err
}

// ReadProtoFromFile reads a protobuf object from a file. The filename extension
// determines whether the file is read as json or binary.
func ReadProtoFromFile(filename string, p proto.Message) error {
	if strings.HasSuffix(filename, ".json") {
		return readProtoFromJSON(filename, p)
	}

	return readProtoFromBin(filename, p)
}

// Read a protobuf object from a raw (binary) file.
func readProtoFromBin(filename string, p proto.Message) error {
	pbData, err := ioutil.ReadFile(filename)
	if err != nil {
		return err
	}

	return proto.Unmarshal(pbData, p)
}

// Read a protobuf object from a JSON file.
func readProtoFromJSON(filename string, p proto.Message) error {
	data, err := os.ReadFile(filename)
	if err != nil {
		return err
	}

	return protojson.Unmarshal(data, p)
}

// Create a return a Timestamp protobuf object with the given time.
func createTimestamp(t *time.Time) *timestamppb.Timestamp {
	timestamp := timestamppb.Timestamp{
		Seconds: t.Unix(),
	}
	return &timestamp
}

// Read a Chrome-OS bios-info file and returns a key-value map.
func readBiosKeyValFile(filename string) (map[string]string, error) {
	// Parse bios-info key-value lines, such as:
	// hwid                    = SONA F5V-A9G-F52-O6G-O2Q-Q86   # [RO/str] Hardware ID
	re := regexp.MustCompile(`^(\S+)\s+(?:[|=])\s+([^#]+)`)
	return parseKeyValFileWithRegex(filename, re)
}

// Read and parse a key-value file with the given regexp. The regexp must be structured
// to produce two subgroup for each line of interest in the file. The first subgroup
// is the key and the second is the corresponding value. Return all the key-value pairs
// as a dictionary.
func parseKeyValFileWithRegex(filename string, re *regexp.Regexp) (map[string]string, error) {
	var dict = map[string]string{}
	var feed = make(chan string, 5)
	var err error

	go func() {
		err = readLines(filename, feed)
	}()

	for line := range feed {
		line = strings.TrimSpace(line)
		match := re.FindStringSubmatch(line)
		if match != nil && len(match) > 2 {
			key := match[1]
			value := strings.Trim(match[2], "\"' ")
			if value != "" {
				dict[key] = value
			}
		}
	}

	return dict, err
}

// Read lines if text from a file and feed them one by one to channel feed.
func readLines(filename string, feed chan string) error {
	defer close(feed)

	f, err := os.Open(filename)
	if err != nil {
		return err
	}

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		feed <- scanner.Text()
	}

	return nil
}
