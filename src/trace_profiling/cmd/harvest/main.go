// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"math"
	"os"
	"os/exec"
	"path"
	"sort"
	"strconv"
	"strings"
	"time"

	"trace_profiling/cmd/harvest/config"
	"trace_profiling/cmd/harvest/utils"
	"trace_profiling/cmd/profile/profile"
	"trace_profiling/cmd/profile/remote"
)

const (
	defaultTraceCacheDir  = "/tmp/traces"
	defaultProfileBinPath = "/usr/local/graphics/profile"
	defaultCrostiniBundle = "crostini-bundle-template.json"
	defaultCroutonBundle  = "crouton-bundle-template.json"
)

// PerfConfig is used to read the performance configuration parameters from
// a JSON file.
type PerfConfig struct {
	Traces            []string `json:"traces"`
	TraceCacheDir     string   `json:"traceCacheDir"`
	KeepTracesInCache bool     `json:"keepTracesInCache"`
	ProfileBinPath    string   `json:"profileBinPath"`
	CrostiniBundle    string   `json:"crostiniBundleTemplate"`
	CroutonBundle     string   `json:"croutonBundleTemplate"`
}

// FPS output for performance comparison.
type fpsRecord struct {
	fpsError        error
	traceName       string
	crostiniFps     float64
	croutonFps      float64
	crostiniPercent float64
}

// Cmd-line arguments.
var argVerbose bool
var argOutputFile string
var argConfigFilepath string
var argEnableCompareFps bool
var argSuppressCrostini bool
var argSuppressCrouton bool
var argDeleteArchiveCrumbs bool

var fpsData []fpsRecord

var harvestConfig *config.HarvestConfigParser

// Fetch a trace archive from Google storage.
func fetchTraceFromStorage(cacheDir, trace string) (string, error) {
	_, filename := path.Split(trace)
	filepath := path.Join(cacheDir, filename)
	if !utils.FileExists(filepath) {
		_, err := remote.FetchFromGS(filepath, trace)
		if err != nil {
			return "", err
		}
	}

	return filepath, nil
}

// Customize the profiler config with the trace file and trace-cache dir and
// write it out as json to a temporary file. Return the path to the temp file.
func customizeProfilerConfig(
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

// Profile a trace file on a given target specified through the profiler config,
// using the companion profiling app specified in profilerBinPath.
func runProfile(profilerBinPath, traceFile string,
	profilerConfig *profile.ProfilerConfigRecord) (string, error) {

	// The bundle is actually a template that we must customize with the proper
	// trace file name and trace dir.
	config, err := customizeProfilerConfig(traceFile, profilerConfig)
	if err != nil {
		return "", err
	}
	defer os.Remove(config)

	arg := fmt.Sprintf("-config=%s", config)
	cmd := exec.Command(profilerBinPath, arg)

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

// Print FPS comparative info for Crostini v.s. Crouton into the output file.
func gatherProfileResult(traceName, crostiniFile, croutonFile string) {
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
		fpsData = append(fpsData, fpsRecord{
			fpsError:        err,
			traceName:       traceName,
			crostiniFps:     fpsCrostini,
			croutonFps:      fpsCrouton,
			crostiniPercent: percent,
		})
	}
}

// Generate the FPS comparative output to the target output file. Note that
// data is always added to the file.
func generateOutput(data []fpsRecord) {
	outFile, err := os.OpenFile(argOutputFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err == nil {
		defer outFile.Close()

		fileInfo, _ := outFile.Stat()
		dataWriter := bufio.NewWriter(outFile)

		sort.SliceStable(fpsData, func(i, j int) bool {
			return fpsData[i].crostiniPercent < fpsData[j].crostiniPercent
		})

		// Add header, but only to new files.
		if fileInfo.Size() == 0 {
			dataWriter.WriteString(fmt.Sprintf(
				"%32s  %8s  %8s      %%\n", "Trace name", "Crostini", "Crouton"))
		}

		for _, fps := range fpsData {
			if fps.fpsError != nil {
				dataWriter.WriteString(fmt.Sprintf("%32s error getting FPS: %s", fps.traceName, fps.fpsError))
			} else {
				dataWriter.WriteString(fmt.Sprintf("%32s,  %8.2f,  %8.2f,", fps.traceName, fps.crostiniFps, fps.croutonFps))
				if fps.crostiniPercent != math.Inf(1) {
					dataWriter.WriteString(fmt.Sprintf("  %8.2f%%\n", fps.crostiniPercent))
				} else {
					dataWriter.WriteString("     INF!\n")
				}
			}
		}
		dataWriter.Flush()
	}
}

// Profile traces coming in queue feedQueue onto the target specified in
// targetBundle and feed the resulting profile file paths to resultQueue.
func profileTracesOnTarget(
	feedQueue chan string,
	targetName string,
	profileAppBinPath string,
	profilerConfig *profile.ProfilerConfigRecord,
	resultQueue chan string) {
	for trace := range feedQueue {
		printIfVerbose("Tracing on %s with %s\n", targetName, trace)
		profFile, err := runProfile(profileAppBinPath, trace, profilerConfig)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error profiling on %s: prof=%s, err=%s\n",
				targetName, profFile, err.Error())
			resultQueue <- ""
		} else {
			resultQueue <- profFile
		}
	}
}

// Run the performance comparison with the given configuration.
func runPerfComparison() error {
	// Create array that will receive FPS data.
	fpsData = make([]fpsRecord, 0, len(harvestConfig.GetTraces()))

	// Launch goroutine to download traces for G-Storage. Once a trace file is
	// ready it is queued into traceQueue. Trace files available locally are
	// simply queued as-is.
	var traceQueue = make(chan string)
	go func() {
		for _, trace := range harvestConfig.GetTraces() {
			if remote.IsGoogleStorageURI(trace) {
				localTrace, err := fetchTraceFromStorage(harvestConfig.GetTraceCacheDir(), trace)
				if err != nil {
					// Print an error and keep going with the next trace.
					fmt.Fprintf(os.Stderr, "Error downloading trace <%s>:\n  err=%s\n", trace, err.Error())
				} else {
					traceQueue <- localTrace
				}
			} else {
				traceQueue <- trace
			}
		}
		close(traceQueue)
	}()

	// Feed queues are used to feed trace-file data to profilers for crostini
	// and crouton, which run in parallel. Conversely, result queues are used
	// to receive data from these profilers.
	var crostFeedQueue = make(chan string)
	var croutFeedQueue = make(chan string)
	var crostResultQueue = make(chan string)
	var croutResultQueue = make(chan string)

	// Run goroutines to profile on Crostini and Crouton in parallel.
	go profileTracesOnTarget(crostFeedQueue, "Crostini", harvestConfig.GetProfilerBinPath(),
		harvestConfig.GetCrostiniProfilerConfig(), crostResultQueue)
	go profileTracesOnTarget(croutFeedQueue, "Crouton", harvestConfig.GetProfilerBinPath(),
		harvestConfig.GetCroutonProfilerConfig(), croutResultQueue)

	// Grab trace files as they become available and feed them to the profile
	// goroutines above.
	var traceData = utils.CreateTraceRecord(argVerbose)
	for trace := range traceQueue {
		if err := traceData.LoadFromFile(trace); err != nil {
			fmt.Fprintf(os.Stderr, "Error extracting trace from archive %s, err=%s\n",
				trace, err.Error())
			continue
		}

		if !argSuppressCrostini {
			crostFeedQueue <- traceData.GetTraceFilePath()
		}
		if !argSuppressCrouton {
			croutFeedQueue <- traceData.GetTraceFilePath()
		}

		// A short pause gives the profile goroutines a chance to grab the traces
		// traces and print their verbose output before we print "Waiting...".It
		// just looks better.
		time.Sleep(5 * time.Millisecond)
		printIfVerbose("Waiting for profiling to complete... \n")

		// Wait for result from profilers.
		var crostProfile = ""
		var croutProfile = ""
		if !argSuppressCrostini {
			crostProfile = <-crostResultQueue
		}
		if !argSuppressCrouton {
			croutProfile = <-croutResultQueue
		}

		if argEnableCompareFps {
			_, traceName := path.Split(traceData.GetTraceFilePath())
			gatherProfileResult(traceName, crostProfile, croutProfile)
		} else {
			if crostProfile != "" {
				printIfVerbose("Crostini profile ready in: %s\n", crostProfile)
			}
			if croutProfile != "" {
				printIfVerbose("Crouton profile ready in: %s\n", croutProfile)
			}
		}
	}

	close(crostFeedQueue)
	close(croutFeedQueue)

	if argEnableCompareFps {
		generateOutput(fpsData)
	}

	return nil
}

// Read and parse the Harvest config json file and leave the result in global
// var harvestConfig.
func readConfigFromFile(jsonFilepath string) error {
	configParser := config.CreateHarvestConfigParser()
	if err := configParser.OpenJSONFile(jsonFilepath); err != nil {
		return err
	}
	if err := configParser.Process(); err != nil {
		return err
	}

	harvestConfig = configParser
	return nil
}

// If verbose mode is enabled, print the formatted string.
func printIfVerbose(format string, a ...interface{}) {
	if argVerbose {
		fmt.Printf(format, a...)
	}
}

func main() {
	flag.StringVar(&argConfigFilepath, "config", "", "Filename for JSON config data")
	flag.StringVar(&argOutputFile, "out", "compare_out.prof", "Output file")
	flag.BoolVar(&argVerbose, "verbose", false, "Enable verbose mode")
	flag.BoolVar(&argEnableCompareFps, "compare-fps", false, "Extract FPS from profile data and compare")
	flag.BoolVar(&argSuppressCrostini, "no-crostini", false, "Suppress profiling on crostini")
	flag.BoolVar(&argSuppressCrouton, "no-crouton", false, "Suppress profiling on crouton")
	flag.BoolVar(&argDeleteArchiveCrumbs, "del-archive-crumbs", false, "Delete files and folders left after unarchiving game data")
	flag.Parse()

	err := readConfigFromFile(argConfigFilepath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return
	}

	// Verify that we have profiler configuration for Crostini and Crouton.
	config := harvestConfig.GetCrostiniProfilerConfig()
	if config == nil || config.ProfileParams == nil {
		argSuppressCrostini = true
		fmt.Fprintf(os.Stderr, "Warning: Crostini profiling is off; no profiler configuration found.\n")
	}
	config = harvestConfig.GetCroutonProfilerConfig()
	if config == nil || config.ProfileParams == nil {
		argSuppressCrouton = true
		fmt.Fprintf(os.Stderr, "Warning: Crouton profiling is off; no profiler configuration found.\n")
	}

	// We can only compare fps if we have both crostini and crouton data.
	argEnableCompareFps = argEnableCompareFps && !(argSuppressCrostini || argSuppressCrouton)

	err = runPerfComparison()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
	}
}
