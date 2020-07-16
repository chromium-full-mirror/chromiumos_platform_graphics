// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"math"
	"os"
	"sort"

	"trace_profiling/cmd/harvest/config"
	"trace_profiling/cmd/harvest/utils"
	"trace_profiling/cmd/profile/profile"
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

// Cmd-line arguments.
var argVerbose bool
var argOutputFile string
var argConfigFilepath string
var argEnableCompareFps bool
var argSuppressCrostini bool
var argSuppressCrouton bool

var harvestConfig *config.HarvestConfigParser

// Generate the FPS comparative output to the target output file. Note that
// data is always added to the file.
func generateFpsOutput(data []utils.FPSRecord) {
	outFile, err := os.OpenFile(argOutputFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err == nil {
		defer outFile.Close()

		fileInfo, _ := outFile.Stat()
		dataWriter := bufio.NewWriter(outFile)

		sort.SliceStable(data, func(i, j int) bool {
			return data[i].CrostiniPercent < data[j].CrostiniPercent
		})

		// Add header, but only to new files.
		if fileInfo.Size() == 0 {
			dataWriter.WriteString(fmt.Sprintf(
				"%8s   %8s           %%  %s\n", "Crostini", "Crouton", "Trace name"))
		}

		for _, fps := range data {
			if fps.FpsError != nil {
				dataWriter.WriteString(fmt.Sprintf("%s error getting FPS: %s", fps.TraceName, fps.FpsError))
			} else {
				dataWriter.WriteString(fmt.Sprintf("%8.2f,  %8.2f,  ", fps.CrostiniFps, fps.CroutonFps))
				if fps.CrostiniPercent != math.Inf(1) {
					dataWriter.WriteString(fmt.Sprintf("  %6.2f%%  ", fps.CrostiniPercent))
				} else {
					dataWriter.WriteString("     INF!\n")
				}
				dataWriter.WriteString(fps.TraceName)
				dataWriter.WriteString("\n")
			}
		}
		dataWriter.Flush()
	}
}

// Run the traces on Crostini and/or Crouton to gather profile data. If requested,
// also compare the FPS data from both platforms.
func doHarvestProfiles() {

	// Setup profiler config for crostini and crouton. Nil indicates we don't care
	// about that particular platform.
	var crostiniProfilerConfig *profile.ProfilerConfigRecord = nil
	if !argSuppressCrostini {
		crostiniProfilerConfig = harvestConfig.GetCrostiniProfilerConfig()
	}

	var croutonProfilerConfig *profile.ProfilerConfigRecord = nil
	if !argSuppressCrouton {
		croutonProfilerConfig = harvestConfig.GetCroutonProfilerConfig()
	}

	// TraceProfile downloads and prepares the traces and then runs them on each
	// target platform.
	errorFeed := make(chan error)
	traceProfile := utils.NewTraceProfile(crostiniProfilerConfig, croutonProfilerConfig,
		errorFeed, harvestConfig.GetProfilerBinPath(), harvestConfig.ShouldKeepTraceAfterUse(),
		argVerbose)

	go func() {
		traceProfile.RunTraces(harvestConfig.GetTraces(), harvestConfig.GetTraceCacheDir())
		close(errorFeed)
	}()

	for err := range errorFeed {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
	}

	if argEnableCompareFps {
		generateFpsOutput(traceProfile.GetFPSData())
	}
}

// Read and parse the Harvest config json file and leave the result in global
// var harvestConfig.
func readHarvestConfigFromFile(jsonFilepath string) error {
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
	flag.Parse()

	err := readHarvestConfigFromFile(argConfigFilepath)
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

	doHarvestProfiles()
}
