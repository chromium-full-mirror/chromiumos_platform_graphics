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
	"path/filepath"
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
var argToolToRun string

// Config data read from JSON.
var harvestConfig *config.HarvestConfigParser
var crostiniProfilerConfig *profile.ProfilerConfigRecord
var croutonProfilerConfig *profile.ProfilerConfigRecord
var deviceInfoConfig *config.DeviceInfoConfigParser

// Tools.
var profileTool *utils.TraceProfile
var deviceInfoTool *utils.DeviceInfoTool

// Setup profiler config for crostini and crouton. Nil indicates we don't care
// about that particular platform.
func setupProfilerConfigs() {
	// Check what configurations are available.
	crostiniProfilerConfig = harvestConfig.GetCrostiniProfilerConfig()
	if crostiniProfilerConfig == nil || crostiniProfilerConfig.ProfileParams == nil {
		argSuppressCrostini = true
		fmt.Fprintf(os.Stderr, "Warning: Crostini profiling is off; no profiler configuration found.\n")
	}
	croutonProfilerConfig = harvestConfig.GetCroutonProfilerConfig()
	if croutonProfilerConfig == nil || croutonProfilerConfig.ProfileParams == nil {
		argSuppressCrouton = true
		fmt.Fprintf(os.Stderr, "Warning: Crouton profiling is off; no profiler configuration found.\n")
	}

	// Take cmd-line options into account.
	if argSuppressCrostini {
		crostiniProfilerConfig = nil
	}
	if argSuppressCrouton {
		croutonProfilerConfig = nil
	}

	// We can only compare fps if we have both crostini and crouton data.
	argEnableCompareFps = argEnableCompareFps && !(argSuppressCrostini || argSuppressCrouton)
}

func setupTools() {
	profileTool = utils.NewTraceProfile(argVerbose)
	deviceInfoTool = utils.NewDeviceInfoTool(argVerbose)
}

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
	printIfVerbose("\nHarvesting profile data with traces:\n")
	printIfVerbose("===================================\n")

	errorFeed := make(chan error)
	profileTool.Setup(crostiniProfilerConfig, croutonProfilerConfig,
		errorFeed, harvestConfig.GetProfilerBinPath(), harvestConfig.ShouldKeepTraceAfterUse())

	go func() {
		profileTool.RunTraces(harvestConfig.GetTraces(), harvestConfig.GetTraceCacheDir())
		close(errorFeed)
	}()

	for err := range errorFeed {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
	}

	if argEnableCompareFps {
		generateFpsOutput(profileTool.GetFPSData())
	}
}

// Run the device-info tool on crostini/crouton, per config.
func doHarvestDeviceInfo() {
	printIfVerbose("\nHarvesting device info:\n======================\n")

	if !argSuppressCrostini {
		doHarvestDeviceInfoOnTarget(deviceInfoConfig.GetCrosvmMachineInfoConfig(),
			crostiniProfilerConfig, "Crosvm")
	}

	if !argSuppressCrouton {
		doHarvestDeviceInfoOnTarget(deviceInfoConfig.GetCroutonMachineInfoConfig(),
			croutonProfilerConfig, "Crouton")
	}
}

// Run the device-info tool on a specific target.
func doHarvestDeviceInfoOnTarget(
	machineConfig *config.MachineInfoConfig,
	profilerConfig *profile.ProfilerConfigRecord,
	machineLabel string) {

	if machineConfig == nil {
		printIfVerbose("Skipping %s device: no applicable property in config file.\n", machineLabel)
	} else if !machineConfig.Enabled {
		printIfVerbose("Skipping %s device: disabled in config.\n", machineLabel)
	} else {
		printIfVerbose("Getting device info from %s device.\n", machineLabel)
		deviceInfoTool.Setup(deviceInfoConfig.GetDeviceInfoBinPath(),
			machineConfig.Name, deviceInfoConfig.GetOwner(),
			profilerConfig.SSHConfig, profilerConfig.TunnelConfig)
		if err := deviceInfoTool.Run(); err != nil {
			fmt.Fprintf(os.Stderr, "Error getting machine-info for %s: %s\n", machineLabel, err.Error())
			return
		}

		// TODO (gwink): upload protobuf to DB if requested.

		err := deviceInfoTool.WriteProtoBufToFile(deviceInfoConfig.GetProtoBufsOutputDir(),
			machineConfig.OutputFileTemplate)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error writing machine-info to protobuf for %s: %s\n",
				machineLabel, err.Error())
		}
	}
}

// Read and parse the Harvest config json file and leave the result in global
// var harvestConfig.
func readHarvestConfigFromFile(jsonFilepath string) error {
	harvestConfigParser := config.CreateHarvestConfigParser()
	deviceInfoConfigParser := config.NewDeviceInfoConfigParser()
	harvestConfigParser.AddHandler("DeviceInfo", deviceInfoConfigParser)

	if err := harvestConfigParser.OpenJSONFile(jsonFilepath); err != nil {
		return err
	}
	if err := harvestConfigParser.Process(); err != nil {
		return err
	}

	harvestConfig = harvestConfigParser
	deviceInfoConfig = deviceInfoConfigParser
	return nil
}

// If verbose mode is enabled, print the formatted string.
func printIfVerbose(format string, a ...interface{}) {
	if argVerbose {
		fmt.Printf(format, a...)
	}
}

func printUsage() {
	appName := filepath.Base(os.Args[0])
	fmt.Fprintf(os.Stderr, "\nUsage: %s -config config-file.json [other options]\n", appName)
	fmt.Fprintf(os.Stderr, "Available options:\n")
	flag.PrintDefaults()
	os.Exit(2)
}

func main() {
	flag.Usage = printUsage
	flag.StringVar(&argConfigFilepath, "config", "", "Filename for JSON config data")
	flag.StringVar(&argOutputFile, "out", "compare_out.prof", "Output file")
	flag.BoolVar(&argVerbose, "verbose", false, "Enable verbose mode")
	flag.BoolVar(&argEnableCompareFps, "compare-fps", false, "Extract FPS from profile data and compare")
	flag.BoolVar(&argSuppressCrostini, "no-crostini", false, "Suppress profiling on crostini")
	flag.BoolVar(&argSuppressCrouton, "no-crouton", false, "Suppress profiling on crouton")
	flag.StringVar(&argToolToRun, "tool", "profile", "Tool to run, one of profile, device-info")
	flag.Parse()

	setupTools()

	err := readHarvestConfigFromFile(argConfigFilepath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return
	}

	setupProfilerConfigs()
	switch argToolToRun {
	case "profile":
		doHarvestProfiles()
	case "device-info":
		doHarvestDeviceInfo()
	}
}
