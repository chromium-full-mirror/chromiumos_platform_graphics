// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"
	"time"
)

type profileReader struct {
	filename string

	// Column indices for the various profile data items in each line of data.
	// These indices are derived from the header line at the begining of the profile.
	idxColumnCallID      int
	idxColumnCallName    int
	idxColumnProgramID   int
	idxColumnGPUStart    int
	idxColumnGPUDuration int
	idxColumnCPUStart    int
	idxColumnCPUDuration int
	idxMax               int

	header string

	file    *os.File
	scanner *bufio.Scanner
}

// Open a profile file given its filename.
func (reader *profileReader) openProfile(filename string) (err error) {
	if reader.file, err = os.Open(filename); err != nil {
		return
	}

	var bufSize = 64 * 1024
	var buffer = make([]byte, bufSize)
	reader.scanner = bufio.NewScanner(reader.file)
	reader.scanner.Buffer(buffer, bufSize)

	reader.filename = filename
	if err = reader.parseHeader(); err != nil {
		return
	}

	err = nil
	return
}

// Read lines from a profile and feed them to channel [lines].
func (reader *profileReader) readLines(lines chan string) {
	go func() {
		lines <- reader.header
		for reader.scanner.Scan() {
			lines <- reader.scanner.Text()
		}
		close(lines)
	}()
}

func (reader *profileReader) close() {
	if reader.scanner != nil {
		reader.file.Close()
		reader.scanner = nil
	}
}

// Parse the header line from a profile and derive the column indices for the
// data we're interested in.
func (reader *profileReader) parseHeader() (err error) {
	if !reader.scanner.Scan() {
		return fmt.Errorf("error: no header in profile <%s>", reader.filename)
	}

	header := reader.scanner.Text()
	if !strings.HasPrefix(header, "#") {
		return fmt.Errorf("error: header line doesn't start with # - \"%s\"", header)
	}

	var columns = strings.Split(strings.TrimLeft(header[1:], " "), " ")
	for idx, columnName := range columns {
		switch columnName {
		case "no":
			reader.idxColumnCallID = idx
		case "gpu_start":
			reader.idxColumnGPUStart = idx
		case "gpu_dura":
			reader.idxColumnGPUDuration = idx
		case "cpu_start":
			reader.idxColumnCPUStart = idx
		case "cpu_dura":
			reader.idxColumnCPUDuration = idx
		case "program":
			reader.idxColumnProgramID = idx
		case "name":
			reader.idxColumnCallName = idx
		case "call", "vsize_start", "vsize_dura", "rss_start", "rss_dura", "pixels":
			// Ignore those columns.
			continue
		default:
			err = fmt.Errorf("error: unexpected column name in profile: %s", columnName)
			return
		}

		if idx > reader.idxMax {
			reader.idxMax = idx
		}
	}

	reader.header = header
	return nil
}

// Check that profile readers [reader] and r2 are compatible. To be compatible,
// they must have the same column indices.
func (reader *profileReader) checkCompatibility(r2 *profileReader) error {
	if reader.idxColumnCallID != r2.idxColumnCallID {
		return fmt.Errorf("error: mismatched call-id column index")
	}
	if reader.idxColumnProgramID != r2.idxColumnProgramID {
		return fmt.Errorf("error: mismatched program-id column index")
	}
	if reader.idxColumnCallName != r2.idxColumnCallName {
		return fmt.Errorf("error: mismatched call-name column index")
	}
	if reader.idxColumnCPUDuration != r2.idxColumnCPUDuration {
		return fmt.Errorf("error: mismatched cpu-duration column index")
	}
	if reader.idxColumnCPUStart != r2.idxColumnCPUStart {
		return fmt.Errorf("error: mismatched cpu-start column index")
	}
	if reader.idxColumnGPUStart != r2.idxColumnGPUStart {
		return fmt.Errorf("error: mismatched gpu-start column index")
	}
	if reader.idxColumnGPUDuration != r2.idxColumnGPUDuration {
		return fmt.Errorf("error: mismatched gpu-duration column index")
	}
	if reader.idxMax != r2.idxMax {
		return fmt.Errorf("error: mismatched number of columns")
	}
	return nil
}

// Merge profile prof1 and prof2 and output the result to stdout.
func mergeProfiles(prof1, prof2 string) (err error) {
	r1 := new(profileReader)
	err = r1.openProfile(prof1)
	defer r1.close()
	if err != nil {
		return err
	}

	r2 := new(profileReader)
	err = r2.openProfile(prof2)
	defer r2.close()
	if err != nil {
		return err
	}

	err = r1.checkCompatibility(r2)
	if err != nil {
		return err
	}

	lines1 := make(chan string, 100)
	lines2 := make(chan string, 100)

	r1.readLines(lines1)
	r2.readLines(lines2)

	errOut := make(chan error, 1)
	lineOut := make(chan string, 500)

	go func() {
		defer func() { close(lineOut) }()
		for {
			l1 := <-lines1
			l2 := <-lines2

			// Note that we return as soon as we find the "Rendered" line or an error
			// occurs. Any extra tracing info inserted by the Profiler will show up
			// after the "Rendered" line and will thus be safely ignored.
			switch {
			case strings.HasPrefix(l1, "Rendered"):
				if !strings.HasPrefix(l2, "Rendered") {
					errOut <- fmt.Errorf("error: mismatched lines:\n  %s\n  %s", l1, l2)
				}
				lineOut <- fmt.Sprintf("%s\n", l1)
				return
			case strings.HasPrefix(l1, "#"):
				if !strings.HasPrefix(l2, "#") {
					errOut <- fmt.Errorf("error: mismatched lines:\n  %s\n  %s", l1, l2)
					return
				}
				lineOut <- fmt.Sprintf("%s\n", l1)
			case l1 == "frame_end":
				if l2 != "frame_end" {
					errOut <- fmt.Errorf("error: mismatched lines:\n  %s\n  %s", l1, l2)
					return
				}
				lineOut <- fmt.Sprintf("%s\n", l1)
			case strings.HasPrefix(l1, "call "):
				mergeAndOutputLines(r1, l1, l2, lineOut)
			default:
				errOut <- fmt.Errorf("error: unrecognized line in profile: %s", l1)
				return
			}
		}
	}()

	for l := range lineOut {
		fmt.Print(l)
	}

	select {
	case err := <-errOut:
		return err
	default:
		return nil
	}
}

// Merge two profile lines, lGPU with GPU data and lCPU with CPU data to a single
// line and feed the result to channel lineOut. Profile reader [r] is needed for
// column-index information.
func mergeAndOutputLines(r *profileReader, lGPU, lCPU string, lineOut chan string) (err error) {
	var gpuTokens = strings.Split(strings.TrimRight(lGPU, "\r\n"), " ")
	var cpuTokens = strings.Split(strings.TrimRight(lCPU, "\r\n"), " ")

	if len(gpuTokens) <= r.idxMax {
		return fmt.Errorf("error: not enough columns in GPU line: %s: ", lGPU)
	}
	if len(cpuTokens) <= r.idxMax {
		return fmt.Errorf("error: not enough columns in CPU line: %s: ", lCPU)
	}

	merged := ""
	for i := 0; i <= r.idxMax; i++ {
		switch i {
		case r.idxColumnCPUDuration:
			merged = merged + cpuTokens[i] + " "
		case r.idxColumnCPUStart:
			merged = merged + cpuTokens[i] + " "
		case r.idxColumnCallID:
			if gpuTokens[i] != cpuTokens[i] {
				err = fmt.Errorf("error: mismatched call id %s, v.s. %s", gpuTokens[i], cpuTokens[i])
				return
			}
			merged = merged + gpuTokens[i] + " "
		case r.idxColumnCallName:
			if gpuTokens[i] != cpuTokens[i] {
				err = fmt.Errorf("error: mismatched call name %s, v.s. %s", gpuTokens[i], cpuTokens[i])
				return
			}
			merged = merged + gpuTokens[i] + " "
		case r.idxColumnGPUDuration:
			merged = merged + gpuTokens[i] + " "
		case r.idxColumnGPUStart:
			merged = merged + gpuTokens[i] + " "
		case r.idxColumnProgramID:
			if gpuTokens[i] != cpuTokens[i] {
				err = fmt.Errorf("error: mismatched program id %s, v.s. %s", gpuTokens[i], cpuTokens[i])
				return
			}
			merged = merged + gpuTokens[i] + " "
		default:
			merged = merged + gpuTokens[i] + " "
		}
	}

	lineOut <- fmt.Sprintf("%s\n", merged)
	return nil
}

func main() {
	start := time.Now()

	var argGPUFilePath string
	var argCPUFilePath string

	flag.StringVar(&argGPUFilePath, "gpu", "", "Profile with GPU data")
	flag.StringVar(&argCPUFilePath, "cpu", "", "Profile with CPU data")
	flag.Parse()

	if argGPUFilePath == "" || argCPUFilePath == "" {
		fmt.Fprintf(os.Stderr, "Usage: merge -gpu=file1 -cpu=file2 > output-file\n")
		return
	}

	err := mergeProfiles(argGPUFilePath, argCPUFilePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
	}

	fmt.Fprintf(os.Stderr, "%v\n", time.Since(start))
}
