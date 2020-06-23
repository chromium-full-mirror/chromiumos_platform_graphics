// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"bufio"
	"os"
	"path"
	"regexp"
	"strings"
	"time"

	"github.com/golang/protobuf/jsonpb"
	"github.com/golang/protobuf/proto"
	timestamppb "github.com/golang/protobuf/ptypes/timestamp"
)

// Parse bios-info key-value lines, such as:
// hwid                    = SONA F5V-A9G-F52-O6G-O2Q-Q86   # [RO/str] Hardware ID
var reBiosKeyValLine = regexp.MustCompile(`^(\S+)\s+(?:[|=])\s+([^#]+)`)

// Read a Chrome-OS bios-info file and returns a key-value map.
func readBiosKeyValFile(filename string) (map[string]string, error) {
	file, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	dict := make(map[string]string)
	for scanner.Scan() {
		line := scanner.Text()
		result := reBiosKeyValLine.FindStringSubmatch(line)
		if result != nil && len(result) >= 3 {
			dict[result[1]] = strings.TrimSpace(result[2])
		}
	}

	return dict, nil
}

// WriteProtobuf writes the protobuf data to the output file. If the output file
// is the empty string, the output goes to stdout. The generated output is JSON
// if the file name ends with ".json" or is stdout. Otherwise it is protobuf
// streaming binary format.
func writeProtobuf(protobuf proto.Message, outputFile string) error {
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
		marshaler := jsonpb.Marshaler{Indent: "  "}
		err = marshaler.Marshal(file, protobuf)

		// Finish output with a newline.
		if err == nil {
			file.Write([]byte("\n"))
		}
	} else {
		data, err := proto.Marshal(protobuf)
		if err == nil {
			_, err = file.Write(data)
		}
	}

	return err
}

// Create a return a Timestamp protobuf object with the given time.
func createTimestamp(t *time.Time) *timestamppb.Timestamp {
	timestamp := timestamppb.Timestamp{
		Seconds: t.Unix(),
	}
	return &timestamp
}
