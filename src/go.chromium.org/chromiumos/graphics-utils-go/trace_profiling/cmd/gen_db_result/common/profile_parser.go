// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"bufio"
	"fmt"
	"os"
	"path"
	"regexp"
	"strconv"
	"strings"
	"time"

	gfx "go.chromium.org/chromiumos/config/go/api/test/results/graphics/v1"
	db "go.chromium.org/chromiumos/config/go/api/test/results/v1"
)

// Relevant labels for GLX info.
var glxInfoLabels = []string{
	"vendor",
	"renderer",
	"core profile version",
	"core profile shading language version",
	"version",
	"shading language version",
	"ES profile version",
	"ES profile shading language version",
}

const (
	benchmarkApitrace = "apitrace"
	groupingGlxInfo   = "glxinfo"
	groupingHarvest   = "harvest"
)

var (
	// Form a regexp for extracting lines with relevant GLX info from profile data.
	glxLabels = strings.Join(glxInfoLabels, "|")
	glxInfo   = fmt.Sprintf("OpenGL (?P<name>%s) string: (?P<value>.*)", glxLabels)
	reGlxInfo = regexp.MustCompile(glxInfo)
	// A regexp for extracting the cmd line from profile data.
	reCmdLine = regexp.MustCompile("(?P<name>CMD): +(?P<value>.*)")
	// A regexp for extracting the trace-id line from profile data.
	reTraceIDLine = regexp.MustCompile("(?P<name>TRACE_ID): +(?P<value>.*)")
	// A regexp for extracting relevant trace info (name and date) from the profile file name.
	reTraceInfo = regexp.MustCompile("(?P<name>.*)\\.(?P<date_time>[0-9]{8}-[0-9]{6}).*")
	// A regexp to extract perf info from Apitrace's "Rendered" line.
	reRenderedLine = regexp.MustCompile("Rendered (?P<frames>[0-9]+) frames in " +
		"(?P<seconds>[0-9.]+) secs, average of (?P<fps>[0-9.]+) fps")
	// A regexp for extracting the exec-env line from profile data.
	reExecEnvLine = regexp.MustCompile("(?P<name>EXEC_ENV): +(?P<value>.*)")
	// A regexp for extracting the machine-name line from profile data.
	reEMachineNameLine = regexp.MustCompile("(?P<name>MACHINE_NAME): +(?P<value>.*)")
	// A regexp for extracting the harvest delay or timeoutline from profile data.
	reHarvestLine = regexp.MustCompile("(?P<name>(?:DELAY|TIMEOUT)): +(?P<value>.*)")
)

// Struct profileParser provides methods to parse Apitrace profile data and
// generate protobuf for uploading to the Graphics Result DB. The relevant
// protobuf definition is found in chromiumos.config.api.test.results.v1.
type profileParser struct {
	filename string         // Full file path to file with profile data
	file     *os.File       // Same file opened for reading
	scanner  *bufio.Scanner // Line-by-line scanner on that file.

	labels       []*db.Result_Label // List of result labels parsed from profile.
	traceID      string             // Trace ID parsed from profile.
	execEnv      string             // Execution env. parsed from profile, e.g. "crouton"
	machineName  string             // Machine name parse from profile, e.g. gwink-sona-C135643
	renderedLine string             // Apitrace's "Rendered" line read from profile.
}

// Create a return a frame-count ResultMetric protobuf object with the given
// value for the frame count.
func createFrameCountMetric(value string) *db.Result_Metric {
	v, _ := strconv.Atoi(value)
	metric := db.Result_Metric{
		Name:  "frame_count",
		Units: "frames",
		Value: float64(v),
	}
	return &metric
}

// Create a return a duration ResultMetric protobuf object with the given
// value for the duration.
func createDurationMetric(value string) *db.Result_Metric {
	v, _ := strconv.ParseFloat(value, 64)
	metric := db.Result_Metric{
		Name:  "duration",
		Units: "seconds",
		Value: v,
	}
	return &metric
}

// Create a return a frame-rate ResultMetric protobuf object with the given
// value for the frame rate.
func createFrameRateMetric(value string) *db.Result_Metric {
	v, _ := strconv.ParseFloat(value, 64)
	metric := db.Result_Metric{
		Name:           "frame_rate",
		Units:          "fps",
		LargerIsBetter: true,
		Value:          v,
	}
	return &metric
}

// Make and return an invocation-source string, using the username garnered from the env.
func makeInvocationSource() string {
	return "user/" + os.Getenv("USER")
}

// Initialize and return a new profileParser instance.
func newProfileParser() *profileParser {
	reader := profileParser{
		labels: make([]*db.Result_Label, 0, 16),
	}
	return &reader
}

// Open a profile file given its file name.
func (reader *profileParser) openProfile(filename string) error {
	var err error
	if reader.file, err = os.Open(filename); err != nil {
		return err
	}

	reader.scanner = bufio.NewScanner(reader.file)
	reader.filename = filename

	return nil
}

// Read all lines from a profile and feed them one-by-one to channel <lines>.
func (reader *profileParser) readLines(lines chan string) {
	go func() {
		for reader.scanner.Scan() {
			lines <- reader.scanner.Text()
		}
		close(lines)
	}()
}

// Close the profile parser.
func (reader *profileParser) close() {
	if reader.scanner != nil {
		reader.scanner = nil
		reader.file.Close()
	}
}

// Create and record a ResultLabel protobuf objects with the given grouping, name
// and value. Labels are recorded in the profileParser.labels list.
func (reader *profileParser) recordLabel(grouping, name, value string) {
	label := db.Result_Label{
		Name:     name,
		Value:    value,
		Grouping: grouping,
	}
	reader.labels = append(reader.labels, &label)
}

// Find and return the value of the label with the given grouping and name.
// The grouping tag is optional and may be set to the empty string. Returns
// an empty string if not found.
func (reader *profileParser) findLabelValue(grouping, name string) string {
	for _, l := range reader.labels {
		if (grouping == "" || grouping == l.Grouping) && name == l.Name {
			return l.Value
		}
	}
	return ""
}

// Form and return a trace name from the profile file name. The trace name
// consists of the file name without extension. E.g. Borderlands_2-49520.
func (reader *profileParser) getTraceName() string {
	fileName := path.Base(reader.filename)
	result := reTraceInfo.FindStringSubmatch(fileName)
	if result != nil && len(result) >= 3 {
		return strings.Split(result[1], ".")[0] // Extract file name without extension.
	}
	return ""
}

// Extract and return the test date/time extracted from the profile file name.
func (reader *profileParser) getTestStartTime() *time.Time {
	fileName := path.Base(reader.filename)
	result := reTraceInfo.FindStringSubmatch(fileName)
	if result != nil && len(result) >= 3 {
		// Interpret the date parsed from the file name in the local time zone.
		zone, _ := time.Now().Zone()
		dateTime, err := time.Parse("20060102-150405MST", result[2]+zone)
		if err == nil {
			return &dateTime
		}
	}
	return nil
}

// Form and return a unique ResultId protobuf object by concatenating the
// invocation source and trace name and test date together.
// E.g. user/gwink-Borderlands_2-49520-20200520-170519
func (reader *profileParser) getResultID() *db.ResultId {
	fileName := path.Base(reader.filename)
	traceName := reader.getTraceName()
	testDate := ""

	// Get the test date from the profile file name.
	match := reTraceInfo.FindStringSubmatch(fileName)
	if match != nil && len(match) >= 3 {
		testDate = match[2]
	}

	return &db.ResultId{
		Value: fmt.Sprintf("%s-%s-%s", makeInvocationSource(), traceName, testDate),
	}
}

// Create and returns a fully configured protobuf Result object. This function
// must be called after the entire profile has been parsed.
func (reader *profileParser) createResult() (*db.Result, error) {
	if reader.renderedLine == "" {
		return nil, fmt.Errorf("profile has no 'Rendered' line")
	}

	// Extract perf info from "Rendered" line.
	match := reRenderedLine.FindStringSubmatch(reader.renderedLine)
	if match == nil {
		return nil, fmt.Errorf("failed to parse 'Rendered' line: %s", reader.renderedLine)
	}

	dbResult := db.Result{
		Id:                   reader.getResultID(),
		Machine:              &db.MachineId{Value: reader.machineName},
		SoftwareConfig:       nil, // TODO(gwink)
		InvocationSource:     makeInvocationSource(),
		CommandLine:          reader.findLabelValue(benchmarkApitrace, "CMD"),
		Benchmark:            benchmarkApitrace,
		Trace:                &gfx.TraceId{Value: reader.traceID},
		PrimaryMetricName:    "frame_rate",
		Labels:               reader.labels,
		Overrides:            nil, // TODO(gwink)
		ExecutionEnvironment: strToExecEnv(reader.execEnv),
	}

	dbResult.Metrics = append(dbResult.Metrics, createFrameCountMetric(match[1]))
	dbResult.Metrics = append(dbResult.Metrics, createDurationMetric(match[2]))
	dbResult.Metrics = append(dbResult.Metrics, createFrameRateMetric(match[3]))

	// Derive end time as start time + duration.
	durationSec, _ := strconv.ParseFloat(match[2], 64)
	startTime := reader.getTestStartTime()
	if startTime != nil {
		endTime := startTime.Add(time.Duration(durationSec) * time.Second)
		dbResult.StartTime = createTimestamp(startTime)
		dbResult.EndTime = createTimestamp(&endTime)
	}

	return &dbResult, nil
}

// Parse the entire profile and return a protobuf Result object crated from it.
func parseProfile(prof string) (*db.Result, error) {
	reader := newProfileParser()
	err := reader.openProfile(prof)
	if err != nil {
		return nil, err
	}
	defer reader.close()

	// Read the profile line-by-line and feed each line to channel lines.
	done := make(chan bool)
	lines := make(chan string, 100)
	reader.readLines(lines)

	// Asynchronously feed each line to the profile parser.
	go func() {
		defer func() { done <- true }()

		// Profile generated by the Harvest/Profiler tools follow the Apitrace data
		// with extra info. That extra info is prefixed by a unique header line.
		// We make the parsing a little more robust by recognizing that header line
		// and switching modes.
		isParsingExtra := false
		for scanLine := range lines {
			if isParsingExtra {
				if result := reCmdLine.FindStringSubmatch(scanLine); result != nil {
					reader.recordLabel(benchmarkApitrace, result[1], result[2])
				} else if result := reTraceIDLine.FindStringSubmatch(scanLine); result != nil {
					reader.traceID = result[2]
				} else if result := reExecEnvLine.FindStringSubmatch(scanLine); result != nil {
					reader.execEnv = result[2]
				} else if result := reEMachineNameLine.FindStringSubmatch(scanLine); result != nil {
					reader.machineName = result[2]
				} else if result := reGlxInfo.FindStringSubmatch(scanLine); result != nil {
					reader.recordLabel(groupingGlxInfo, result[1], result[2])
				} else if result := reHarvestLine.FindStringSubmatch(scanLine); result != nil {
					reader.recordLabel(groupingHarvest, result[1], result[2])
				}
			} else if strings.HasPrefix(scanLine, "Rendered") {
				reader.renderedLine = scanLine
			} else if strings.HasPrefix(scanLine, ">>>>>> Extra") {
				isParsingExtra = true
			}
		}
	}()

	// Wait for parsing to terminate before creating the Result protobuf object.
	<-done
	return reader.createResult()
}

// Utility function to convert a string, such as "crouton", to its protobuf
// enum value, e.g. db.Result_CROUTON.
func strToExecEnv(str string) db.Result_ExecutionEnvironment {
	if execEnv, ok := db.Result_ExecutionEnvironment_value[strings.ToUpper(str)]; ok {
		return db.Result_ExecutionEnvironment(execEnv)
	}

	return db.Result_UNKNOWN
}
