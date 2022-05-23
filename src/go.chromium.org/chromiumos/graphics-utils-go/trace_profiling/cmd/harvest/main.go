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

	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/harvest/config"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/harvest/utils"
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
var argToolToRun string

// Parsers for reading config data from JSON config file.
var harvestConfig *config.HarvestConfigParser
var deviceInfoConfig *config.DeviceInfoConfigParser
var gpuVisConfig *config.GpuVisConfigParser

var targetDevice1 *config.TargetDevice
var targetDevice2 *config.TargetDevice

// Tools.
var profileTool *utils.TraceProfile
var deviceInfoTool *utils.DeviceInfoTool
var gpuVisTool *utils.GpuVisTool

// Setup the two target devices. It's possible for a target device to be nil, in
// which case it is simply ignored.
func setupTargetDevices() {
	targetDevice1 = harvestConfig.GetTargetDeviceConfig1()
	targetDevice2 = harvestConfig.GetTargetDeviceConfig2()
}

// Setup the tools used to carry out the selected action.
func setupTools() {
	profileTool = utils.NewTraceProfile(argVerbose)
	deviceInfoTool = utils.NewDeviceInfoTool(argVerbose)
	gpuVisTool = utils.NewGpuVisTool(argVerbose)
}

// Generate the FPS comparative output to the target output file. Note that
// FPS data is always appended to the file.
func generateFpsOutput(data []utils.FPSRecord) {
	if len(data) == 0 {
		return
	}

	outFile, err := os.OpenFile(argOutputFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err == nil {
		defer outFile.Close()

		fileInfo, _ := outFile.Stat()
		dataWriter := bufio.NewWriter(outFile)

		sort.SliceStable(data, func(i, j int) bool {
			return data[i].TestFpsPercent < data[j].TestFpsPercent
		})

		// Add header, but only to new files.
		if fileInfo.Size() == 0 {
			dataWriter.WriteString(fmt.Sprintf("%8s   %8s           %%  %s\n",
				data[0].TestDeviceEnv, data[0].RefDeviceEnv, "Trace name"))
		}

		for _, fps := range data {
			if fps.FpsError != nil {
				dataWriter.WriteString(fmt.Sprintf("%s error getting FPS: %s", fps.TraceName, fps.FpsError))
			} else {
				dataWriter.WriteString(fmt.Sprintf("%8.2f,  %8.2f,  ", fps.TestFps, fps.ReferenceFps))
				if fps.TestFpsPercent != math.Inf(1) {
					dataWriter.WriteString(fmt.Sprintf("  %6.2f%%  ", fps.TestFpsPercent))
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

// Run the traces on the target devices to gather profile data. If requested,
// also compare the FPS data for all the traces.
func doHarvestProfiles() {
	printIfVerbose("\nHarvesting profile data with traces:\n")
	printIfVerbose("===================================\n")

	errorFeed := make(chan error)
	profileTool.Setup(targetDevice1, targetDevice2, errorFeed,
		harvestConfig.GetProfilerBinPath(), harvestConfig.ShouldKeepTraceAfterUse())

	go func() {
		profileTool.RunTraces(harvestConfig.GetTraces(), harvestConfig.GetTraceCacheDir(), harvestConfig.GetDelay())
		close(errorFeed)
	}()

	for err := range errorFeed {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
	}

	if argEnableCompareFps {
		generateFpsOutput(profileTool.GetFPSData())
	}
}

// Run the device-info tool on the target devices. Depending on the configuration,
// the device-info tool will collect machine information and software information
// from the target devices.
func doHarvestDeviceInfo() {
	printIfVerbose("\nHarvesting device info:\n======================\n")

	const noExecEnv = "Error: Fetching machine info aborted for %s.\n" +
		"Device config does not define ExecEnv.\n"

	toolConfig := deviceInfoConfig.GetDeviceInfoToolConfig()
	if targetDevice1 != nil {
		execEnv := targetDevice1.DeviceConfig.ExecEnv
		if execEnv == "" {
			fmt.Fprintf(os.Stderr, noExecEnv, "TargetDevice1.\n")
		} else {
			doHarvestDeviceInfoOnTarget(toolConfig, targetDevice1)
		}
	}

	if targetDevice2 != nil {
		execEnv := targetDevice2.DeviceConfig.ExecEnv
		if execEnv == "" {
			fmt.Fprintf(os.Stderr, noExecEnv, "TargetDevice2.\n")
		} else {
			doHarvestDeviceInfoOnTarget(toolConfig, targetDevice2)
		}
	}
}

// Run the device-info tool on a single specific target.
func doHarvestDeviceInfoOnTarget(
	toolConfig *config.DeviceInfoToolConfig,
	targetDevice *config.TargetDevice) {

	profilerConfig := targetDevice.ProfilerConfig
	deviceConfig := targetDevice.DeviceConfig
	deviceLabel := deviceConfig.ExecEnv
	printIfVerbose("Getting device info from %s device.\n", deviceLabel)

	deviceInfoTool.Setup(toolConfig, deviceLabel, deviceConfig.Name,
		profilerConfig.SSHConfig, profilerConfig.TunnelConfig)
	if err := deviceInfoTool.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error getting machine-info for %s: %s\n", deviceLabel, err.Error())
	}
}

// Run the GpuVisTool on one target device.
func doHarvestGpuVisData() {
	printIfVerbose("\nHarvesting GpuVis performance data:\n==================================\n")
	if gpuVisConfig == nil {
		fmt.Fprintf(os.Stderr, "Error: no GpuVis parameters in configuration.\n")
		return
	}

	// GpuVisTool can only run on one device. Pick non-nil device as target. If both
	// are non-nil, use targetDevice1 and print a warning.
	targetDevice := targetDevice2
	if targetDevice1 != nil {
		targetDevice = targetDevice1
		if targetDevice2 != nil {
			fmt.Printf("Warning: GpuVis can only run on one device. Running on %s\n",
				targetDevice.DeviceConfig.ExecEnv)
		}
	}

	err := gpuVisTool.Setup(targetDevice, gpuVisConfig.GetGPUPerfConfig(),
		harvestConfig.GetProfilerBinPath(), harvestConfig.GetTraceCacheDir(),
		harvestConfig.GetTraces(), harvestConfig.ShouldKeepTraceAfterUse())
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting GpuVis trace data from %s: %s\n",
			targetDevice.DeviceConfig.ExecEnv, err.Error())
		return
	}

	// Run GpuVisTool asynchronously. The for loop below will loop until channel
	// errorFeed is closed.
	errorFeed := make(chan error)
	go func() {
		gpuVisTool.Run(errorFeed)
		close(errorFeed)
	}()

	for err := range errorFeed {
		fmt.Fprintf(os.Stderr, "Error getting GpuVis trace data from %s: %s\n",
			targetDevice.DeviceConfig.ExecEnv, err.Error())
	}
}

// Read and parse the Harvest config json file and leave the result in global
// var harvestConfig.
func readHarvestConfigFromFile(jsonFilepath string) error {
	harvestConfigParser := config.CreateHarvestConfigParser()
	deviceInfoConfigParser := config.NewDeviceInfoConfigParser()
	gpuVisConfigParser := config.NewGpuVisConfigParser()
	harvestConfigParser.AddHandler("DeviceInfoTool", deviceInfoConfigParser)
	harvestConfigParser.AddHandler("GpuVis", gpuVisConfigParser)

	if err := harvestConfigParser.OpenJSONFile(jsonFilepath); err != nil {
		return err
	}
	if err := harvestConfigParser.Process(); err != nil {
		return err
	}

	harvestConfig = harvestConfigParser
	deviceInfoConfig = deviceInfoConfigParser
	gpuVisConfig = gpuVisConfigParser
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
	flag.StringVar(&argToolToRun, "tool", "profile", "Tool to run, one of profile, device-info")
	flag.Parse()

	setupTools()

	err := readHarvestConfigFromFile(argConfigFilepath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return
	}

	setupTargetDevices()
	switch argToolToRun {
	case "profile":
		doHarvestProfiles()
	case "device-info":
		doHarvestDeviceInfo()
	case "gpuvis":
		doHarvestGpuVisData()
	default:
		fmt.Fprintf(os.Stderr, "Error: unknown tool %s\n", argToolToRun)
	}
}
