// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"bufio"
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/harvest/config"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/remote"
)

const (
	// Useful VT100 escape sequences.
	beginBold     = "\033[1m"
	resetCharAttr = "\033[0m"
	redOnBlack    = "\033[31;40m"
	defaultColors = "\033[39;49m"
)

var (
	// Where to find the GpuVis bash scripts within the GpuVis folder. There are
	// two possibilities, depending on whether the locally built or pre-built version
	// of GpuVis is used.
	gpuVisScriptDirs = []string{"sample", "scripts"}

	// The trace scripts that are used to collect traces on the host device.
	gpuVisTraceScripts = []string{
		"trace-cmd-capture.sh", "trace-cmd-command.sh", "trace-cmd-setup.sh",
		"trace-cmd-start-tracing.sh", "trace-cmd-status.sh", "trace-cmd-stop-tracing.sh"}
)

// GpuVisTool provides support for collecting GpuVis trace data on a target device,
// transfer the data back to the workstation and optionally run GpuVis on this data.
type GpuVisTool struct {
	// Configuration parameters read from json config file.
	gpuVisConfig *config.GpuVisConfig
	targetDevice *config.TargetDevice

	// The profiler is used to run the trace. These are the relevant profiler
	// config parameters from harvest config.
	profilerBinPath   string
	traceCacheDir     string
	keepTracesInCache bool
	traceToRun        string

	// Errors are fed to this channel.
	errorOut chan error

	// GpuVisTool state and parameters.
	targetDeviceSSH  *remote.SSHTarget
	targetWorkingDir string
	firstFrame       int
	lastFrame        int
	frameCount       int

	verbose bool
}

// NewGpuVisTool create and returns a new GpuVisTool instance.
func NewGpuVisTool(verbose bool) *GpuVisTool {
	return &GpuVisTool{verbose: verbose}
}

// Setup is called to setup the GpuVisTool instance prior to running the tool.
func (gv *GpuVisTool) Setup(
	targetDevice *config.TargetDevice,
	gpuVisConfig *config.GpuVisConfig,
	profilerBinPath string,
	traceCacheDir string,
	traces []string,
	keepTracesInCache bool) error {

	if len(traces) == 0 {
		return fmt.Errorf("GpuVisTool setup failed: no trace file provided")
	} else if len(traces) > 1 {
		fmt.Printf("Warning: more than one trace file specified. Will use %s\n",
			traces[0])
	}

	gv.targetDevice = targetDevice
	gv.gpuVisConfig = gpuVisConfig
	gv.profilerBinPath = profilerBinPath
	gv.keepTracesInCache = keepTracesInCache
	gv.traceCacheDir = traceCacheDir
	gv.traceToRun = traces[0]

	if err := gv.setupSSHToHostDevice(); err != nil {
		return err
	}

	return nil
}

// Run runs the GpuVisTool. Errors are fed to the channel errorOut.
func (gv *GpuVisTool) Run(errorOut chan error) {
	gv.errorOut = errorOut

	err := gv.verifyTargetDeviceCompatibility()
	if err != nil {
		gv.errorOut <- err
		return
	}

	// We're about to run several scripts on the host device and need to maintain
	// the shell's state between these scrips. To this end we start a shell session.
	err = gv.targetDeviceSSH.OpenShellSession()
	if err != nil {
		gv.errorOut <- err
		return
	}
	defer gv.targetDeviceSSH.CloseShellSession()

	err = gv.createTargetWorkingDir()
	if err != nil {
		gv.errorOut <- err
		return
	}
	defer gv.targetDeviceSSH.DelFile(gv.targetWorkingDir)

	err = gv.installTraceTools()
	if err != nil {
		gv.errorOut <- err
		return
	}

	// Always run terminateTracing to restore to device to its base state, even
	// if initiateTracing partially fails.
	defer gv.terminateTracing()
	err = gv.initiateTracing()
	if err != nil {
		gv.errorOut <- err
		return
	}

	// We're going to run the profiler asynchronously. Channel profilerExited is
	// used to get notified in case the profiler fails or exists early.
	profilerExited := make(chan bool)
	err = gv.startProfiling(profilerExited)
	if err != nil {
		gv.errorOut <- err
		return
	}

	// Tell the user what's happening and to hit enter at the right time. When
	// that happens, capture the traces.
	if gv.tellUserToHitEnterKeyAndWait(profilerExited) {
		err = gv.captureTrace()
		if err != nil {
			gv.errorOut <- err
			return
		}
	} else {
		gv.errorOut <- fmt.Errorf("exited without capturing trace data")
		return
	}

	// The terminal mode in the shell session interferes with downloading files
	// through stdout. Since we no longer need to preserve shell state, we can close
	// the session. (Yes, defer will close the shell session again and that's ok.)
	gv.targetDeviceSSH.CloseShellSession()

	files, err := gv.downloadTraceFiles()
	if err != nil {
		gv.errorOut <- err
		return
	}

	// Optionally run GpuVis on the trace data.
	if gv.gpuVisConfig.RunGpuVis != "" {
		err = gv.runGpuVis(files)
		if err != nil {
			gv.errorOut <- err
		}
	}
}

// Setup a SSH session to the host OS on the target device. This session is
// used the run the trace-cmd scripts to gather CPU and GPU performance data. Thus
// it is always to the host device and never to any VM or container device.
func (gv *GpuVisTool) setupSSHToHostDevice() error {
	ssh := gv.targetDevice.ProfilerConfig.SSHConfig
	if gv.targetDevice.ProfilerConfig.TunnelConfig != nil {
		ssh = &gv.targetDevice.ProfilerConfig.TunnelConfig.Server
	}

	if ssh == nil {
		return fmt.Errorf("missing SSH info to reach host device")
	}

	var err error
	gv.targetDeviceSSH, err = remote.CreateSSHTargetWithParams(ssh)
	if err != nil {
		return err
	}

	return gv.targetDeviceSSH.Connect()
}

// Verify that a device is compatible with GpuVis. The kernel must be configured
// to generate i915 trace points, ideally low-level i915 trace points. Returns
// an error if the device is not compatible. Prints a warning to stdout if
// low-level trace points are not generated.
func (gv *GpuVisTool) verifyTargetDeviceCompatibility() error {
	gv.printIfVerbose("Checking target device.\n")

	// List the i915 trace points.
	traceDir := "/sys/kernel/debug/tracing/events/i915/"
	files, _ := gv.targetDeviceSSH.ListFiles(traceDir, "i915_request*")

	// Verify that at least some i915 trace points are available. Also check that
	// low-level i915 trace points are available and print a warning if not.
	lowLevelEventCount := 0
	allEventCount := 0
	for _, f := range files {
		if strings.HasPrefix(f, "/sys/") {
			allEventCount++

			// Look for low-level i915_request_[in, out, submit] trace point. They are
			// directories and will have a ':' at the end in the listing.
			fname := strings.TrimPrefix(filepath.Base(f), "i915_request_")
			if fname == "in:" || fname == "out:" || fname == "submit:" {
				lowLevelEventCount++
			}
		}
	}

	if lowLevelEventCount == 0 && allEventCount == 0 {
		fmt.Printf("Error: target device doesn't support i915 tracing events.\n")
		return fmt.Errorf("wrong target device type")
	}

	if lowLevelEventCount < 3 {
		fmt.Printf("Warning: target-device kernel isn't configure to generate low-level i915\n" +
			"tracing events. (See https://github.com/mikesart/gpuvis/wiki/TechDocs-Intel)\n")
	}

	return nil
}

// Create the working dir on the target device. Note that the working dir is a
// temporary dir inside the ChromeOSWorkingDir specified in the config. That makes
// easier to list the generated trace files and cleanup when done.
func (gv *GpuVisTool) createTargetWorkingDir() error {
	gv.printIfVerbose("Creating temp dir in %s\n", gv.gpuVisConfig.ChromeOSWorkingDir)

	if gv.gpuVisConfig.ChromeOSWorkingDir == "" {
		return fmt.Errorf("no device working Dir specified in config")
	}

	err := gv.targetDeviceSSH.Mkdir(gv.gpuVisConfig.ChromeOSWorkingDir)
	if err != nil {
		return err
	}

	// Make sure there's enough space in working dir to receive trace-collection
	// tools and collect traces. A single GPU trace is typically less than 40MB.
	// CPU traces are even smaller and the scripts are just a few KB. 50MB should
	// be more than enough space
	kBytes, err := gv.targetDeviceSSH.GetRemoteFreeSpaceKb(gv.gpuVisConfig.ChromeOSWorkingDir)
	if err != nil {
		return err
	}
	if kBytes < 50*1024 {
		return fmt.Errorf("not enough free space on %s to run GpuVis trace collection tools",
			gv.gpuVisConfig.ChromeOSWorkingDir)
	}

	gv.targetWorkingDir, err = gv.targetDeviceSSH.MkTempDir(gv.gpuVisConfig.ChromeOSWorkingDir)
	gv.printIfVerbose("Using temp dir %s\n", gv.targetWorkingDir)

	return err
}

// Install the trace GpuVis script by copying them from the local GpuVis folder
// to the working dir on the target device.
func (gv *GpuVisTool) installTraceTools() error {
	var err error
	if _, err = os.Stat(gv.gpuVisConfig.LocalGpuVisDir); err != nil {
		return err
	}

	// Find which subdir contains the trace scripts.
	var localScriptDir string
	for _, scriptDir := range gpuVisScriptDirs {
		localScriptDir = filepath.Join(gv.gpuVisConfig.LocalGpuVisDir, scriptDir)
		_, err := os.Stat(localScriptDir)
		if err == nil || !os.IsNotExist(err) {
			break
		}
	}

	if err != nil {
		return fmt.Errorf("could not find GpuVis trace scripts within %s",
			gv.gpuVisConfig.LocalGpuVisDir)
	}

	// Copy each script to the target device.
	for _, script := range gpuVisTraceScripts {
		src := filepath.Join(localScriptDir, script)
		dst := filepath.Join(gv.targetWorkingDir, script)
		if err := gv.targetDeviceSSH.SendFile(src, dst, "0755"); err != nil {
			return err
		}
	}

	return nil
}

// Initiating tracing on the target device. Note that this step only configures
// the target device so that it is ready to capture trace data. The actual data
// capture happens in function captureTrace.
func (gv *GpuVisTool) initiateTracing() error {
	for _, env := range gv.gpuVisConfig.ChromeOSEnvVar {
		cmd := "export " + env
		_, err := gv.targetDeviceSSH.RunCmd(cmd)
		if err != nil {
			return fmt.Errorf("failed: %s, err = %s", cmd, err.Error())
		}
	}

	cmd := "cd " + gv.targetWorkingDir
	_, err := gv.targetDeviceSSH.RunCmd(cmd)
	if err != nil {
		return fmt.Errorf("failed: %s, err = %s", cmd, err.Error())
	}

	cmd = "./trace-cmd-setup.sh"
	output, err := gv.targetDeviceSSH.RunCmd(cmd)
	if err != nil {
		return err
	}
	if strings.Index(output, "ERROR") != -1 {
		return fmt.Errorf("GpuVis trace-cmd-setup failed: %s", output)
	}

	cmd = "./trace-cmd-start-tracing.sh"
	output, err = gv.targetDeviceSSH.RunCmd(cmd)
	if err != nil {
		return err
	}
	if strings.Index(output, "ERROR") != -1 {
		return fmt.Errorf("GpuVis trace-cmd-start-tracing failed: %s", output)
	}

	return nil
}

// Restore the host device to its original non-tracing state.
func (gv *GpuVisTool) terminateTracing() {
	cmd := "cd " + gv.targetWorkingDir
	gv.targetDeviceSSH.RunCmd(cmd)

	cmd = filepath.Join(gv.targetWorkingDir, "trace-cmd-stop-tracing.sh")
	gv.targetDeviceSSH.RunCmd(cmd)
}

// A call to this function triggers capturing of traces on the target device.
// A set of cpu and gpu traces is captured and stored in the target working
// dir with extensions .dat and .i915-dat.
func (gv *GpuVisTool) captureTrace() error {
	cmd := filepath.Join(gv.targetWorkingDir, "trace-cmd-capture.sh")
	_, err := gv.targetDeviceSSH.RunCmd(cmd)
	return err
}

// Download the captured trace files from the target device to the local machine.
// The trace files are stored in LocalGpuVisDir.
func (gv *GpuVisTool) downloadTraceFiles() ([]string, error) {
	files := make([]string, 0, 2)

	// Trace files have extensions .dat and .i915-dat.
	srcFiles, err := gv.targetDeviceSSH.ListFiles(gv.targetWorkingDir, "*.*dat")
	if err != nil {
		return nil, err
	}

	for _, file := range srcFiles {
		filename := gv.targetDevice.DeviceConfig.ExecEnv + "_" + filepath.Base(file)
		dstFile := filepath.Join(gv.gpuVisConfig.LocalGpuVisDir, filename)

		gv.printIfVerbose("Download target-device trace %s to %s\n", file, dstFile)

		err = gv.getRemoteFile(file, dstFile)
		if err != nil {
			return nil, err
		}

		files = append(files, dstFile)
	}

	return files, nil
}

// Run GpuVis with the trace data file captured from the target device.
func (gv *GpuVisTool) runGpuVis(files []string) error {
	if len(files) == 0 {
		return fmt.Errorf("no performance data file to run GpuVis with")
	}

	// Path to GpuVis app.
	gpuVis := filepath.Join(gv.gpuVisConfig.LocalGpuVisDir, gv.gpuVisConfig.RunGpuVis)
	// CPU and GPU trace data files.
	cpuData := ""
	gpuData := ""

	// If there's only one file, it'd better be CPU trace data.
	if len(files) == 1 {
		if !strings.HasSuffix(files[0], ".dat") {
			return fmt.Errorf("only one trace file was captured and it is not CPU data: %s", files[0])
		}

		cpuData = files[0]
	} else {
		// We have at least two trace data files. Let's look for the CPU and GPU trace data.
		for _, f := range files {
			if strings.HasSuffix(f, ".dat") {
				cpuData = f
			} else if strings.HasSuffix(f, ".i915-dat") {
				gpuData = f
			}
		}

		if cpuData == "" {
			return fmt.Errorf("missing CPU trace data: %s, %s", files[0], files[1])
		}
		if gpuData == "" {
			return fmt.Errorf("missing GPU trace data: %s, %s", files[0], files[1])
		}
	}

	// GpuVis is rather picky, the CPU data file must be given before the GPU data file.
	cmd := exec.Command(gpuVis, cpuData, gpuData)
	result, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("exec failed: %s, err = %s", result, err)
	}

	return nil
}

// Download a file from the remote device to local storage.
func (gv *GpuVisTool) getRemoteFile(remoteFilePath, localFilePath string) error {
	outFile, err := os.OpenFile(localFilePath, os.O_RDWR|os.O_CREATE, 0755)
	if err != nil {
		return err
	}
	defer outFile.Close()
	outFile.Truncate(0)

	cmd := fmt.Sprintf("dd if=%s status=none bs=512", remoteFilePath)
	return gv.targetDeviceSSH.RunCmdWithWriter(cmd, outFile)
}

// Run the profiler on the trace asynchronously. The profiler command is modified
// to loop over the frame range given in the GpuVis configuration. The profiler is
// launched in a separate coroutine and this function returns immediately.
func (gv *GpuVisTool) startProfiling(profilerExited chan bool) error {
	var err error
	gv.firstFrame, gv.lastFrame, gv.frameCount, err = parseFrameLoop(gv.gpuVisConfig.FrameLoop)
	if err != nil {
		return err
	}

	cmdTokens := parseProfCmd(gv.targetDevice.ProfilerConfig.ProfileParams.ProfCommand)
	if len(cmdTokens) < 2 {
		return fmt.Errorf("invalid profile command: %s",
			gv.targetDevice.ProfilerConfig.ProfileParams.ProfCommand)
	}

	tmpDir, err := ioutil.TempDir(os.TempDir(), "gpuvis-profile-")
	if err != nil {
		return err
	}

	// We use the profiler configuration from the config file albeit with two
	// modifications. (1) The profile command is altered to include the frame loop.
	// (2) The profile generated by the profiler is downloaded to a temporary folder.
	newCommand := genCustomProfileCmd(cmdTokens, gv.firstFrame, gv.lastFrame, gv.frameCount)
	gv.targetDevice.ProfilerConfig.ProfileParams.ProfCommand = newCommand
	gv.targetDevice.ProfilerConfig.ProfileParams.LocalProfileDir = tmpDir

	profiler := NewTraceProfile(false /* not verbose */)
	profiler.Setup(gv.targetDevice, nil, gv.errorOut, gv.profilerBinPath, gv.keepTracesInCache)

	go func() {
		traces := []string{gv.traceToRun}
		profiler.RunTraces(traces, gv.traceCacheDir, 0)
		os.RemoveAll(tmpDir)
		profilerExited <- true
	}()

	return nil
}

// Print a helpful message asking the user to hit Enter at the right time, then wait
// until that happens. Channel profilerExited is used to get notified when the profiler
// exits, in case the user forgets to hit Enter or the profiler fails.
func (gv *GpuVisTool) tellUserToHitEnterKeyAndWait(profilerExited chan bool) bool {
	fmt.Printf(beginBold+
		"\nThe profiler has been launched to run the trace and will loop %d times on frames %d to %d.\n",
		gv.frameCount, gv.firstFrame, gv.lastFrame)
	fmt.Printf("When the profiler enters the frame loop, hit the " + redOnBlack +
		"Enter" + defaultColors + " key to capture CPU/GPU trace data.\n" + resetCharAttr)

	// Wait for user to hit Enter, but bail out if the profiler exits for any reason.
	stdin := make(chan byte)
	go getStdinInput(stdin)
	select {
	case <-profilerExited:
		fmt.Printf("Profiler exited before capturing trace data: did you forget to hit Enter?\n")
		return false
	case <-stdin:
		return true
	}
}

// Get char input from stdin. This function will not return until the user hits
// Enter. The Enter key is forwarded to channel input.
func getStdinInput(input chan byte) {
	r := bufio.NewReader(os.Stdin)
	b, _ := r.ReadByte()
	input <- b
}

// If verbose mode is enabled, print the formatted string.
func (gv *GpuVisTool) printIfVerbose(format string, a ...interface{}) {
	if gv.verbose {
		fmt.Printf(format, a...)
	}
}

// Parse and return the frame-loop data from the given string. Frame loop data
// has the form <first-frame>-<last-frame>,loop-count and the three values are
// returned as f0, fN, count.
func parseFrameLoop(str string) (f0, fN, count int, err error) {
	f0 = 0
	fN = 0
	count = 0

	// Frame-loop definition should have the form f0-fN,count.
	reFrameLoop := regexp.MustCompile(`^\s*([\d]+)\s*-\s*([\d]+),([\d]+)\s*$`)
	match := reFrameLoop.FindSubmatch([]byte(str))
	if len(match) < 4 {
		err = fmt.Errorf("invalid FrameLoop specification: %s", str)
		return
	}

	f0, err = strconv.Atoi(string(match[1]))
	if err != nil {
		return
	}
	fN, err = strconv.Atoi(string(match[2]))
	if err != nil {
		return
	}
	count, err = strconv.Atoi(string(match[3]))
	if err != nil {
		return
	}

	if f0 >= fN || count <= 0 {
		err = fmt.Errorf("invalid FrameLoop specification: %s", str)
	}
	return
}

// Parse the profile-command definition, from the profiler config, of the form
// [env] bin-path [options] "[[trace-file]] > [[prof-file]]". Options have
// the form -c, --name or --name=val. The bin path and each option are returned
// as a separate token. The last component between quotes is returned as a
// single token.
func parseProfCmd(str string) (tokens []string) {
	var withinString = false
	var prevChar, c rune = 0, 0
	var command bytes.Buffer
	var i int

	// Scan the cmd line up to the first - (option) or [ (file parameters).
	// Beware of - and [ embedded in strings.
	for i, c = range str {
		if !withinString && ((c == '-' && prevChar == ' ') || c == '[') {
			break
		}

		if !withinString && c == '"' {
			withinString = true
		} else if withinString && c == '"' && prevChar != '\\' {
			withinString = false
		}

		command.WriteRune(c)
		prevChar = c
	}

	// All the char accumulated so far form the first token.
	tokens = append(tokens, command.String())

	// Scan the options, each to one token. Each option starts with a '-' and options
	// are separated by spaces. Again, beware of strings.
	str = str[i:]
	if c == '-' {
		var option bytes.Buffer
		for i, c = range str {
			if option.Len() == 0 && c != '-' && c != ' ' {
				break
			}

			if !withinString && c == ' ' {
				if option.Len() > 0 {
					tokens = append(tokens, option.String())
					option.Reset()
				}

				prevChar = c
				continue
			}

			if !withinString && c == '"' {
				withinString = true
			} else if withinString && c == '"' && prevChar != '\\' {
				withinString = false
			}

			option.WriteRune(c)
			prevChar = c
		}
	}

	// What's left of the cmd string forms the last token.
	str = str[i:]
	if len(str) > 0 {
		tokens = append(tokens, str)
	}
	return
}

// Generate a new profiler command that includes the frame-loop options. If the
// existing profiler-cmd token already include frame-loop options, they are
// discarded. The tokens are those generated by function parseProfCmd above.
func genCustomProfileCmd(cmdTokens []string, startFrame, endFrame, loopCount int) string {
	var newCmd bytes.Buffer

	newCmd.WriteString(cmdTokens[0])

	// Accumulate existing options, skipping frame-loop options.
	for _, opt := range cmdTokens[1 : len(cmdTokens)-1] {
		if !strings.HasPrefix("--loop", opt) {
			newCmd.WriteString(" " + opt)
		}
	}

	// Add our own frame-loop options.
	newCmd.WriteString(fmt.Sprintf(" --loop-begin=%d", startFrame))
	newCmd.WriteString(fmt.Sprintf(" --loop-end=%d", endFrame))
	newCmd.WriteString(fmt.Sprintf(" --loop-repeat-cnt=%d", loopCount))

	newCmd.WriteString(" " + cmdTokens[len(cmdTokens)-1])

	return newCmd.String()
}
