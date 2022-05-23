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
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/harvest/config"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/profile"
)

// FPSRecord records frame-per-second performance data for comparisons between
// a reference device, usually a crouton device, and a test device.
type FPSRecord struct {
	FpsError       error   // Non-nil indicates an error occurred for this trace.
	TraceName      string  // Name of trace that yielded this fps data.
	RefDeviceName  string  // The name of the reference device.
	RefDeviceEnv   string  // The exec env of the reference device, one of crostini, crouton, etc.
	TestDeviceName string  // The name of the test device.
	TestDeviceEnv  string  // The exec env for the test device.
	TestFps        float64 // Measured frame-per-second for test device.
	ReferenceFps   float64 // Measured frame-per-second for reference device.
	TestFpsPercent float64 // Percent fps for test device v.s. reference device.
}

// TraceProfile takes profiler configuration for up to two target devices and
// is run traces on these devices to collect profile data.
type TraceProfile struct {
	targetDevice1     *config.TargetDevice // First target device. May be nil.
	targetDevice2     *config.TargetDevice // Second target device. May be nil.
	profilerBinPath   string               // Local path to Profiler tool.
	fpsData           []FPSRecord          // FPS comparison between the two devices, one per trace.
	errorOut          chan error           // This channel receives errors during profiling.
	keepTracesInCache bool                 // Whether to keep trace files in the cache.
	verbose           bool                 //  Whether to be verbose during profiling.
}

// NewTraceProfile creates and returns a new TraceProfile object.
func NewTraceProfile(verbose bool) *TraceProfile {

	tp := TraceProfile{verbose: verbose}

	return &tp
}

// Setup must be invoked before calling RunTraces to configure the TraceProfile
// instance.  Either device may be set to nil to run traces on only one device.
// If both target devices are nil, this will be boring but will run very fast.
func (tp *TraceProfile) Setup(
	targetDevice1 *config.TargetDevice,
	targetDevice2 *config.TargetDevice,
	errorOut chan error,
	profilerBinPath string,
	keepTracesInCache bool) {

	tp.targetDevice1 = targetDevice1
	tp.targetDevice2 = targetDevice2
	tp.errorOut = errorOut
	tp.profilerBinPath = profilerBinPath
	tp.keepTracesInCache = keepTracesInCache
}

// GetFPSData returns the FPS data gathered during profiling. Call this function
// only after TraceProfile is done running all the traces.
func (tp *TraceProfile) GetFPSData() []FPSRecord {
	return tp.fpsData
}

// RunTraces run the profiler on the given traces found in cacheDir.
func (tp *TraceProfile) RunTraces(traces []string, cacheDir string, delay int) {
	tp.fpsData = make([]FPSRecord, 0, len(traces))

	traceQueue := make(chan *TraceRecord)
	fetcher := NewGSFetcher(cacheDir, traceQueue, tp.errorOut, tp.verbose)
	go fetcher.FetchTraceData(traces)

	// Feed queues are used to feed trace-file data to profilers for both devices,
	// which run in parallel. Conversely, result queues are used to receive data
	// data from these profilers.
	var feedQueue1 = make(chan string)
	var feedQueue2 = make(chan string)
	var resultQueue1 = make(chan string)
	var resultQueue2 = make(chan string)

	// Run goroutines to profile on both devices in parallel.
	go tp.profileTracesOnTarget(feedQueue1, tp.targetDevice1, delay, resultQueue1)
	go tp.profileTracesOnTarget(feedQueue2, tp.targetDevice2, delay, resultQueue2)

	for traceRecord := range traceQueue {
		if traceRecord == nil {
			break
		}

		if tp.targetDevice1 != nil {
			feedQueue1 <- traceRecord.GetTraceFilePath()
		}
		if tp.targetDevice2 != nil {
			feedQueue2 <- traceRecord.GetTraceFilePath()
		}

		// A short pause gives the profile goroutines a chance to grab the traces
		// and print their verbose output before we print "Waiting...". It just
		// looks better.
		time.Sleep(5 * time.Millisecond)
		tp.printIfVerbose("Waiting for profiling to complete... \n")

		// Wait for result from profilers.
		var profile1 = ""
		var profile2 = ""
		if tp.targetDevice1 != nil {
			profile1 = <-resultQueue1
			if profile1 != "" {
				appendExtraInfoToProfile(traceRecord.GetTraceID(),
					tp.targetDevice1.DeviceConfig.ExecEnv, tp.targetDevice1.DeviceConfig.Name,
					delay,
					tp.targetDevice1.ProfilerConfig.ProfileParams.Timeout, profile1)
			}
		}
		if tp.targetDevice2 != nil {
			profile2 = <-resultQueue2
			if profile2 != "" {
				appendExtraInfoToProfile(traceRecord.GetTraceID(),
					tp.targetDevice2.DeviceConfig.ExecEnv, tp.targetDevice2.DeviceConfig.Name,
					delay,
					tp.targetDevice2.ProfilerConfig.ProfileParams.Timeout, profile2)
			}
		}

		if tp.targetDevice1 != nil && tp.targetDevice2 != nil {
			_, traceName := path.Split(traceRecord.GetTraceFilePath())
			tp.gatherProfileResult(traceName, tp.targetDevice1.DeviceConfig, profile1,
				tp.targetDevice2.DeviceConfig, profile2)
		} else {
			if profile1 != "" {
				tp.printIfVerbose("Profile for %s ready in: %s\n",
					tp.targetDevice1.DeviceConfig.ExecEnv, profile1)
			}
			if profile2 != "" {
				tp.printIfVerbose("Profile for %s ready in: %s\n",
					tp.targetDevice2.DeviceConfig.ExecEnv, profile2)
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
	close(feedQueue1)
	close(feedQueue2)
}

// Profile traces coming in queue feedQueue onto the target specified in
// targetBundle and feed the resulting profile file paths to resultQueue.
func (tp *TraceProfile) profileTracesOnTarget(
	feedQueue chan string,
	targetDevice *config.TargetDevice,
	delay int,
	resultQueue chan string) {

	targetName := "undefined"
	if targetDevice != nil {
		targetName = targetDevice.DeviceConfig.ExecEnv
	}

	// Only delay after the first trace.
	var needsDelay = false
	for trace := range feedQueue {
		if needsDelay && delay != 0 {
			tp.printIfVerbose("Delaying for %d seconds prior to tracing %s\n", delay, trace)
			time.Sleep(time.Duration(delay) * time.Second)
		}
		tp.printIfVerbose("\nTracing on %s with %s\n", targetName, trace)
		profFile, err := tp.runProfile(trace, targetDevice.ProfilerConfig)
		if err != nil {
			tp.errorOut <- fmt.Errorf("profiling on %s failed: prof=%s, err=%s",
				targetName, profFile, err.Error())
			resultQueue <- ""
		} else {
			resultQueue <- profFile
		}

		needsDelay = true
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

// Print FPS comparative info for profile results from the two devices into the
// output file. If a single device is a crouton device, then it is chosen as the
// reference device and the other device is the test device. Otherwise, the
// comparison order is arbitrary.
func (tp *TraceProfile) gatherProfileResult(
	traceName string,
	device1 *profile.DeviceConfigRecord,
	profileData1 string,
	device2 *profile.DeviceConfigRecord,
	profileData2 string) {

	if profileData1 != "" && profileData2 != "" {
		// Pick the crouton device, if one is available, as reference device.
		referenceData, testData := profileData1, profileData2
		referenceDevice, testDevice := device1, device2
		if device2.ExecEnv == "crouton" {
			referenceData, testData = profileData2, profileData1
			referenceDevice, testDevice = device2, device1
		}

		var err error
		fpsFromTestDevice, e := getFpsFromProfile(testData)
		if e != nil {
			err = e
		}
		fpsFromReferenceDevice, e := getFpsFromProfile(referenceData)
		if e != nil {
			err = e
		}

		percent := math.Inf(1) // Positive infinity
		if fpsFromReferenceDevice > 0.001 {
			percent = 100.0 * fpsFromTestDevice / fpsFromReferenceDevice
		}

		tp.fpsData = append(tp.fpsData, FPSRecord{
			FpsError:       err,
			TraceName:      traceName,
			RefDeviceName:  referenceDevice.Name,
			RefDeviceEnv:   referenceDevice.ExecEnv,
			TestDeviceName: testDevice.Name,
			TestDeviceEnv:  testDevice.ExecEnv,
			TestFps:        fpsFromTestDevice,
			ReferenceFps:   fpsFromReferenceDevice,
			TestFpsPercent: percent,
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

// Append extra information about the device and trace at the end of the profile.
// Companion tool gen_db_result parses these lines to extract the info.
func appendExtraInfoToProfile(
	traceID, machineExecEnv, machineName string, delay int, timeout int, profFilepath string) error {
	f, err := os.OpenFile(profFilepath, os.O_APPEND|os.O_WRONLY, os.ModeAppend)
	if err != nil {
		return err
	}
	defer f.Close()

	if _, err := f.WriteString(fmt.Sprintf("TRACE_ID: %s\n", traceID)); err != nil {
		return err
	}
	if _, err := f.WriteString(fmt.Sprintf("EXEC_ENV: %s\n", machineExecEnv)); err != nil {
		return err
	}
	if _, err := f.WriteString(fmt.Sprintf("MACHINE_NAME: %s\n", machineName)); err != nil {
		return err
	}
	if _, err := f.WriteString(fmt.Sprintf("DELAY: %d\n", delay)); err != nil {
		return err
	}
	if _, err := f.WriteString(fmt.Sprintf("TIMEOUT: %d\n", timeout)); err != nil {
		return err
	}

	return nil
}

// If verbose mode is enabled, print the formatted string.
func (tp *TraceProfile) printIfVerbose(format string, a ...interface{}) {
	if tp.verbose {
		fmt.Printf(format, a...)
	}
}
