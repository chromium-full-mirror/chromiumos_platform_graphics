// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"math"
	"os"
	"os/exec"
	"path"
	"strconv"
	"strings"
	"time"
	"trace_profiling/cmd/profile/profile"
)

// FPSRecord records frame-per-second performance data for crostini/crouton comparisons.
type FPSRecord struct {
	FpsError        error
	TraceName       string
	CrostiniFps     float64
	CroutonFps      float64
	CrostiniPercent float64
}

// TraceProfile takes profiler configuration for crostini and crouton devices and
// is able to run traces on these platform.
type TraceProfile struct {
	crostiniConfig    *profile.ProfilerConfigRecord // Profiler configuration for Crostini device. May be nil.
	croutonConfig     *profile.ProfilerConfigRecord // Profiler configuration for Crouton device. May be nil.
	profilerBinPath   string                        // Local path to Profiler tool.
	fpsData           []FPSRecord                   // FPS comparison between Crostini and Crouton.
	errorOut          chan error                    // This channel receives errors during profiling.
	keepTracesInCache bool                          // Whether to keep trace files in the cache.
	verbose           bool                          //  Whether to be verbose during profiling.
}

// NewTraceProfile creates and returns a new TraceProfile object. Either crostiniConfig
// or croutonConfig may be set to nil to ignore that platform.
func NewTraceProfile(
	crostiniConfig *profile.ProfilerConfigRecord,
	croutonConfig *profile.ProfilerConfigRecord,
	errorOut chan error,
	profilerBinPath string,
	keepTracesInCache bool,
	verbose bool) *TraceProfile {

	tp := TraceProfile{
		crostiniConfig:    crostiniConfig,
		croutonConfig:     croutonConfig,
		errorOut:          errorOut,
		profilerBinPath:   profilerBinPath,
		keepTracesInCache: keepTracesInCache,
		verbose:           verbose,
	}

	return &tp
}

// GetFPSData returns the FPS data gathered during profiling. Call this function
// only after TraceProfile is done running all the traces.
func (tp *TraceProfile) GetFPSData() []FPSRecord {
	return tp.fpsData
}

// RunTraces run the profiler on the given traces found in cacheDir.
func (tp *TraceProfile) RunTraces(traces []string, cacheDir string) {
	tp.fpsData = make([]FPSRecord, 0, len(traces))

	traceQueue := make(chan *TraceRecord)
	fetcher := NewGSFetcher(cacheDir, traceQueue, tp.errorOut, tp.verbose)
	go fetcher.FetchTraceData(traces)

	// Feed queues are used to feed trace-file data to profilers for crostini
	// and crouton, which run in parallel. Conversely, result queues are used
	// to receive data from these profilers.
	var crostFeedQueue = make(chan string)
	var croutFeedQueue = make(chan string)
	var crostResultQueue = make(chan string)
	var croutResultQueue = make(chan string)

	// Run goroutines to profile on Crostini and Crouton in parallel.
	go tp.profileTracesOnTarget(crostFeedQueue, "Crostini", tp.crostiniConfig, crostResultQueue)
	go tp.profileTracesOnTarget(croutFeedQueue, "Crouton", tp.croutonConfig, croutResultQueue)

	for traceRecord := range traceQueue {
		if traceRecord == nil {
			break
		}

		if tp.crostiniConfig != nil {
			crostFeedQueue <- traceRecord.GetTraceFilePath()
		}
		if tp.croutonConfig != nil {
			croutFeedQueue <- traceRecord.GetTraceFilePath()
		}

		// A short pause gives the profile goroutines a chance to grab the traces
		// and print their verbose output before we print "Waiting...". It just
		// looks better.
		time.Sleep(5 * time.Millisecond)
		tp.printIfVerbose("Waiting for profiling to complete... \n")

		// Wait for result from profilers.
		var crostProfile = ""
		var croutProfile = ""
		if tp.crostiniConfig != nil {
			crostProfile = <-crostResultQueue
			if crostProfile != "" {
				appendTraceIDToProfile(traceRecord.GetTraceID(), crostProfile)
			}
		}
		if tp.croutonConfig != nil {
			croutProfile = <-croutResultQueue
			if croutProfile != "" {
				appendTraceIDToProfile(traceRecord.GetTraceID(), croutProfile)
			}
		}

		if tp.crostiniConfig != nil && tp.croutonConfig != nil {
			_, traceName := path.Split(traceRecord.GetTraceFilePath())
			tp.gatherProfileResult(traceName, crostProfile, croutProfile)
		} else {
			if crostProfile != "" {
				tp.printIfVerbose("Crostini profile ready in: %s\n", crostProfile)
			}
			if croutProfile != "" {
				tp.printIfVerbose("Crouton profile ready in: %s\n", croutProfile)
			}
		}

		// Delete the trace file unless the option to keep it is enabled. However,
		// if the original file was the local trace file, keep it.
		if !tp.keepTracesInCache && (traceRecord.isFromArchive || !traceRecord.isFromLocalFile) {
			tp.printIfVerbose("Delete trace file %s\n", traceRecord.GetTraceFilePath())
			traceRecord.DeleteTraceFile()
		}

		// Delete the folder and content extracted from the archive.
		traceRecord.DeleteTraceDir()
	}

	// Closing the feed queues lets the goroutines we launched above exit gracefully.
	close(crostFeedQueue)
	close(croutFeedQueue)
}

// Profile traces coming in queue feedQueue onto the target specified in
// targetBundle and feed the resulting profile file paths to resultQueue.
func (tp *TraceProfile) profileTracesOnTarget(
	feedQueue chan string,
	targetName string,
	profilerConfig *profile.ProfilerConfigRecord,
	resultQueue chan string) {

	for trace := range feedQueue {
		tp.printIfVerbose("Tracing on %s with %s\n", targetName, trace)
		profFile, err := tp.runProfile(trace, profilerConfig)
		if err != nil {
			tp.errorOut <- fmt.Errorf("profiling on %s failed: prof=%s, err=%s",
				targetName, profFile, err.Error())
			resultQueue <- ""
		} else {
			resultQueue <- profFile
		}
	}
}

// Profile a trace file on a given target specified through the profiler config,
// using the companion profiling app specified in profilerBinPath.
func (tp *TraceProfile) runProfile(
	traceFile string, profilerConfig *profile.ProfilerConfigRecord) (string, error) {

	// The bundle is actually a template that we must customize with the proper
	// trace file name and trace dir.
	config, err := tp.customizeProfilerConfig(traceFile, profilerConfig)
	if err != nil {
		return "", err
	}
	defer os.Remove(config)

	arg := fmt.Sprintf("-config=%s", config)
	cmd := exec.Command(tp.profilerBinPath, arg)

	result, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("'profile' failed: err = %s", err)
	}

	// Extract profile file path from profiler output.
	var prof string
	lines := strings.Split(string(result), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "OutProfile=") {
			prof = strings.TrimPrefix(line, "OutProfile=")
			break
		}
	}

	if prof == "" {
		return "", fmt.Errorf("profiling failed; err = %s", result)
	}

	return prof, nil
}

// Customize the profiler config with the trace file and trace-cache dir and
// write it out as json to a temporary file. Return the path to the temp file.
func (tp *TraceProfile) customizeProfilerConfig(
	traceFile string, config *profile.ProfilerConfigRecord) (string, error) {

	dir, filename := path.Split(traceFile)

	// Create a temporary file to received the customized config.
	configFile, err := ioutil.TempFile("", "profiler_config_*.json")
	if err != nil {
		return "", err
	}
	defer configFile.Close()

	// Customize the profiler config with our own trace file and cache dir.
	config.ProfileParams.Traces = []string{filename}
	config.ProfileParams.LocalTraceDir = dir

	// Write config to temp file as json. The format must match the unified-config
	// format so that the Profiler can load it.
	configFile.WriteString("{\"Profile\":")
	encoder := json.NewEncoder(configFile)
	encoder.SetEscapeHTML(false)
	err = encoder.Encode(config)
	configFile.WriteString("}")

	return configFile.Name(), err
}

// Print FPS comparative info for Crostini v.s. Crouton into the output file.
func (tp *TraceProfile) gatherProfileResult(traceName, crostiniFile, croutonFile string) {
	if crostiniFile != "" && croutonFile != "" {
		var err error
		fpsCrostini, e := getFpsFromProfile(crostiniFile)
		if e != nil {
			err = e
		}
		fpsCrouton, e := getFpsFromProfile(croutonFile)
		if e != nil {
			err = e
		}

		percent := math.Inf(1) // Positive infinity
		if fpsCrouton > 0.001 {
			percent = 100.0 * fpsCrostini / fpsCrouton
		}

		tp.fpsData = append(tp.fpsData, FPSRecord{
			FpsError:        err,
			TraceName:       traceName,
			CrostiniFps:     fpsCrostini,
			CroutonFps:      fpsCrouton,
			CrostiniPercent: percent,
		})
	}
}

// Scan the given profile for the FPS info line (starts with Rendered), extract
// the FPS value and return it.
func getFpsFromProfile(profile string) (float64, error) {
	file, err := os.Open(profile)
	if err != nil {
		return 0.0, err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "Rendered") {
			tokens := strings.Split(strings.TrimRight(line, "\r\n"), " ")
			for i, token := range tokens {
				if token == "fps" && i >= 1 {
					fps, _ := strconv.ParseFloat(tokens[i-1], 64)
					return fps, nil
				}
			}
		}
	}

	return 0.0, fmt.Errorf("no FPS info found in profile %s", profile)
}

// Append a line with the trace ID at the end of the profile. Companion tool
// gen_db_result parses this line to extract the trace ID.
func appendTraceIDToProfile(traceID string, profFilepath string) error {
	f, err := os.OpenFile(profFilepath, os.O_APPEND|os.O_WRONLY, os.ModeAppend)
	if err != nil {
		return err
	}
	defer f.Close()

	traceIDLine := fmt.Sprintf("TRACE_ID: %s\n", traceID)
	_, err = f.WriteString(traceIDLine)

	return err
}

// If verbose mode is enabled, print the formatted string.
func (tp *TraceProfile) printIfVerbose(format string, a ...interface{}) {
	if tp.verbose {
		fmt.Printf(format, a...)
	}
}
