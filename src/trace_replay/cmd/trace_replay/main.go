// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"time"

	"trace_replay/cmd/trace_replay/comm"
	"trace_replay/cmd/trace_replay/repo"
	"trace_replay/cmd/trace_replay/utils"
	"trace_replay/pkg/errors"
)

const (
	tempFolder        = "/tmp"
	glRetraceAppName  = "glretrace"
	eglRetraceAppName = "eglretrace"
	apitraceOutputRE  = `Rendered (\d+) frames in (\d*\.?\d*) secs, average of (\d*\.?\d*) fps`
	// Default application timeout in seconds
	defaultTimeout = 60 * 60
	// Maximum allowed replay time for one trace in seonds
	replayMaxTime = 15 * 60
	// Cooling down time before each trace replay in seconds
	replayCoolDownTime = 30
)

var (
	retraceArgs      = []string{"--benchmark"}
	requiredPackages = []string{"apitrace", "zstd"}
)

type replayAppConfig struct {
	AppName string
	Args    []string
	EnvVars []string
	Postfix string
}

var traceReplayConfigs = map[string]replayAppConfig{
	comm.TestFlagDefault: replayAppConfig{
		AppName: "glretrace",
		Args:    retraceArgs,
		EnvVars: []string{"DISPLAY=:0"},
		Postfix: "",
	},
	comm.TestFlagSurfaceless: replayAppConfig{
		AppName: "eglretrace",
		Args:    retraceArgs,
		EnvVars: []string{"WAFFLE_PLATFORM=sl", "LD_PRELOAD=libEGL.so.1"},
		Postfix: "_surfaceless",
	},
}

func runCommand(ctx context.Context, env []string, appName string, args ...string) (exitCode int, stdout string, stderr string) {
	appPathName, err := exec.LookPath(appName)
	if err != nil {
		exitCode = -1
		stderr = err.Error()
		return
	}

	var outbuf, errbuf bytes.Buffer
	var waitStatus syscall.WaitStatus
	cmd := exec.CommandContext(ctx, appPathName, args...)
	cmd.Stdout = &outbuf
	cmd.Stderr = &errbuf
	cmd.Env = append(os.Environ(), env...)

	err = cmd.Run()
	stdout = outbuf.String()
	stderr = errbuf.String()

	if ctx.Err() == context.DeadlineExceeded {
		// In case of timeout the err is always "signal: killed", so, it's better to replace it
		// with more informative DeadlineExceeded error
		err = ctx.Err()
	}

	if err != nil {
		if exitError, ok := err.(*exec.ExitError); ok {
			waitStatus = exitError.Sys().(syscall.WaitStatus)
			exitCode = waitStatus.ExitStatus()
		} else {
			exitCode = -1
			if stderr == "" {
				stderr = err.Error()
			}
		}
	} else {
		waitStatus = cmd.ProcessState.Sys().(syscall.WaitStatus)
		exitCode = waitStatus.ExitStatus()
	}
	return
}

func decompressFile(ctx context.Context, fileName string, expectedExt string) (string, error) {
	var appName string
	var appArgs []string
	fileExt := filepath.Ext(fileName)

	switch fileExt {
	case expectedExt:
		return fileName, nil
	case ".bz2":
		appName = "bunzip2"
		appArgs = []string{"-f", fileName}
	case ".zst", ".xz":
		appName = "zstd"
		appArgs = []string{"-d", "-f", "--rm", "-T0", fileName}
	default:
		return "", errors.New("Unknown compressed file  extension: %s", fileExt)
	}

	exitCode, _, stderr := runCommand(ctx, nil, appName, appArgs...)
	if exitCode != 0 {
		return "", errors.New("Unable to decompress <%s>. Exit code: %d. %s", fileName, exitCode, stderr)
	}
	return strings.TrimSuffix(fileName, filepath.Ext(fileName)), nil
}

// httpRequestWrapper request the server and return the http.Response. Caller must close the response once finished processing.
func httpRequestWrapper(ctx context.Context, proxyURL string, params url.Values) (*http.Response, error) {
	parsedURL, err := url.Parse(proxyURL)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to parse server URL <%s>", proxyURL)
	}
	parsedURL.RawQuery = params.Encode()

	httpRequest, err := http.NewRequestWithContext(ctx, "GET", parsedURL.String(), nil)
	if err != nil {
		return nil, errors.Wrap(err, "http.NewRequestWithContext(%s) failed", parsedURL)
	}
	httpClient := &http.Client{}

	httpResponse, err := httpClient.Do(httpRequest)
	if err != nil {
		return nil, errors.Wrap(err, "http.Do(%v) failed", httpRequest)
	}
	// We decide to let the caller process to close the body.
	// defer httpResponse.Body.Close()
	if httpResponse.StatusCode != http.StatusOK {
		return nil, errors.New("http status code isn't OK: %d", httpResponse.StatusCode)
	}
	return httpResponse, nil
}

// downloadFile downloads a file using relative file path [filePath] via proxy http server
// [proxyURL] and saves it to the specified directory [localPath]
// returns the full name to the local result file or error
func downloadFile(ctx context.Context, localPath, proxyURL, filePath string) (string, error) {
	// Send http GET download=filePath request to the server
	params := url.Values{}
	params.Add("type", "download")
	params.Add("filePath", filePath)
	httpResponse, err := httpRequestWrapper(ctx, proxyURL, params)
	if err != nil {
		return "", errors.Wrap(err, "failed to download file: %v", filePath)
	}
	defer httpResponse.Body.Close()

	outFile := path.Join(localPath, path.Base(filePath))
	localFile, err := os.Create(outFile)
	if err != nil {
		return "", errors.Wrap(err, "os.Create(%s) failed", outFile)
	}
	defer localFile.Close()
	err = utils.CopyWithContext(ctx, localFile, httpResponse.Body)
	if err != nil {
		return "", errors.Wrap(err, "io.Copy() failed")
	}
	return outFile, nil
}

// logMsg sends log the message to the host via proxy.
func logMsg(ctx context.Context, proxyURL, message string) error {
	// Send http Get log=message request to the server
	params := url.Values{}
	params.Add("type", "log")
	params.Add("message", message)
	httpResponse, err := httpRequestWrapper(ctx, proxyURL, params)
	if err != nil {
		return errors.Wrap(err, "failed to log message: %v", message)
	}
	defer httpResponse.Body.Close()
	return nil
}

// notifyInitFinished sends an event notifying of finished initialization
func notifyInitFinished(ctx context.Context, proxyURL string) error {
	params := url.Values{}
	params.Add("type", "notifyInitFinished")
	httpResponse, err := httpRequestWrapper(ctx, proxyURL, params)
	if err != nil {
		return errors.Wrap(err, "failed to send initFinished notification")
	}
	defer httpResponse.Body.Close()
	return nil
}

// notifyInitFinished sends an event notifying of a single finished replay
func notifyReplayFinished(ctx context.Context, proxyURL string, replayDesc string, replayStartTime float64) error {
	params := url.Values{}
	params.Add("type", "notifyReplayFinished")
	params.Add("replayDescription", replayDesc)
	params.Add("replayStartTime", strconv.FormatFloat(replayStartTime, 'e', -1, 64))
	httpResponse, err := httpRequestWrapper(ctx, proxyURL, params)
	if err != nil {
		return errors.Wrap(err, "failed to send replayFinished notification")
	}
	defer httpResponse.Body.Close()
	return nil
}

// getTraceList function retreives the list of all traces for the repository specified
// in the TestGroupConfig
func getTraceList(ctx context.Context, config *comm.TestGroupConfig) (*repo.TraceList, error) {
	traceListFileName := fmt.Sprintf("repo.%d.json", config.Repository.Version)
	fileName, err := downloadFile(ctx, tempFolder, config.ProxyServer.URL, traceListFileName)
	if err != nil {
		return nil, err
	}
	defer os.Remove(fileName)

	file, err := os.Open(fileName)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to open downloaded <%s>", fileName)
	}
	defer file.Close()

	bytes, _ := ioutil.ReadAll(file)
	var traceList repo.TraceList
	err = json.Unmarshal(bytes, &traceList)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to parse trace list")
	}

	return &traceList, nil
}

// checks if a set of labels |a| is a subset of labels |b|
func matchLabels(a *[]string, b *[]string) bool {
	if len(*a) == 0 || len(*b) == 0 {
		return false
	}

	for _, aval := range *a {
		bFound := false
		for _, bval := range *b {
			if strings.EqualFold(aval, bval) {
				bFound = true
				break
			}
		}
		if bFound == false {
			return false
		}
	}
	return true
}

// getTraceEntries function selects the trace entries for the specified labels
func getTraceEntries(traceList *repo.TraceList, queryLabels *[]string) ([]repo.TraceListEntry, error) {
	var result []repo.TraceListEntry
	for _, entry := range traceList.Entries {
		if matchLabels(queryLabels, &entry.Labels) == true {
			result = append(result, entry)
		}
	}
	return result, nil
}

func parseReplayOutput(output string, postfix string) (map[string]comm.ValueEntry, error) {
	re := regexp.MustCompile(apitraceOutputRE)
	match := re.FindStringSubmatch(output)
	if match == nil {
		return nil, errors.New("Unable to parse apitrace output <%s>", output)
	}
	totalFrames, err := strconv.ParseUint(match[1], 10, 32)
	if err != nil {
		return nil, errors.Wrap(err, "failed to parse frames %q", match[1])
	}
	durationInSeconds, err := strconv.ParseFloat(match[2], 32)
	if err != nil {
		return nil, errors.Wrap(err, "failed to parse duration %q", match[2])
	}
	averageFPS, err := strconv.ParseFloat(match[3], 32)
	if err != nil {
		return nil, errors.Wrap(err, "failed to parse fps %q", match[3])
	}
	return map[string]comm.ValueEntry{
		"frames" + postfix: comm.ValueEntry{
			Unit:      "frame",
			Direction: 0,
			Value:     float32(totalFrames),
		}, "fps" + postfix: comm.ValueEntry{
			Unit:      "fps",
			Direction: +1,
			Value:     float32(averageFPS),
		}, "time" + postfix: comm.ValueEntry{
			Unit:      "sec",
			Direction: -1,
			Value:     float32(durationInSeconds),
		},
	}, nil
}

func outputResult(result comm.TestGroupResult) {
	output, _ := json.Marshal(result)
	fmt.Println(string(output))
}

func exitWithError(err error) {
	formatMessage := func(err error) string {
		if err != nil {
			return err.Error()
		}
		return "Unknown error"
	}

	result := comm.TestGroupResult{
		Result:  comm.TestResultFailure,
		Message: formatMessage(err),
	}
	outputResult(result)
	os.Exit(0)
}

func checkPackageInstalled(ctx context.Context, name string) error {
	if exitCode, _, stderr := runCommand(ctx, nil, "dpkg", "-l", name); exitCode != 0 {
		return errors.New("dpkg for %s failed with exit code %d! %s", name, exitCode, stderr)
	}
	return nil
}

func replayTrace(ctx context.Context, config replayAppConfig, traceFileName string, timeoutInSeconds uint32) (map[string]comm.ValueEntry, error) {
	ctx, cancel := context.WithTimeout(ctx, time.Duration(timeoutInSeconds)*time.Second)
	defer cancel()

	exitCode, stdout, stderr := runCommand(ctx, config.EnvVars, config.AppName, append(config.Args, traceFileName)...)
	if exitCode != 0 {
		return nil, errors.New("Failed to replay trace file [%s]. Exit code: %d. %s", traceFileName, exitCode, stderr)
	}
	return parseReplayOutput(stdout, config.Postfix)
}

func listFiles(path string) (map[string]uint64, error) {
	result := make(map[string]uint64)
	files, err := ioutil.ReadDir(path)
	if err != nil {
		return nil, err
	}

	for _, file := range files {
		if !file.IsDir() {
			result[file.Name()] = uint64(file.Size())
		}
	}
	return result, nil
}

func runReplayOnce(ctx context.Context, config *comm.TestGroupConfig, traceFileName string, replayTimeout uint32) (map[string]comm.ValueEntry, error) {
	res := make(map[string]comm.ValueEntry)

	if err := notifyInitFinished(ctx, config.ProxyServer.URL); err != nil {
		return res, err
	}

	logMsg(ctx, config.ProxyServer.URL, "Replaying the trace with with the default settings.")
	res, err := replayTrace(ctx, traceReplayConfigs[comm.TestFlagDefault], traceFileName, replayTimeout)
	if err != nil {
		return res, err
	}

	// Replay the trace file with custom settings corresponding to an each flag list entry
	for _, flag := range config.Flags {
		if _, ok := traceReplayConfigs[flag]; !ok {
			logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Warning: Unable to find a trace replay config for <%s> flag! Skipping the test.", flag))
			continue
		}
		// Cool down and flush all pending filesistem pending i/o ops
		time.Sleep(time.Duration(replayCoolDownTime) * time.Second)
		exec.Command("sync").Run()

		logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Replaying the trace with <%s> flag.", flag))
		rr, err := replayTrace(ctx, traceReplayConfigs[flag], traceFileName, replayTimeout)
		if err != nil {
			return rr, err
		}

		if err := notifyReplayFinished(ctx, config.ProxyServer.URL, "Replay_"+flag, float64(time.Now().UnixNano()/1e9)); err != nil {
			return res, err
		}

		for k, v := range rr {
			res[k] = v
		}
	}

	return res, nil
}

func runReplayRepeatedly(ctx context.Context, config *comm.TestGroupConfig, traceFileName string, replayTimeout uint32) (map[string]comm.ValueEntry, error) {
	res := make(map[string]comm.ValueEntry)

	exec.Command("sync").Run()
	if err := notifyInitFinished(ctx, config.ProxyServer.URL); err != nil {
		return res, err
	}

	flag := comm.TestFlagDefault
	if len(config.Flags) > 0 {
		flag = config.Flags[0]
		msg := fmt.Sprintf("Using only the first of the specified replay flags: <%s>", flag)
		logMsg(ctx, config.ProxyServer.URL, msg)
	}
	traceReplayConfig := traceReplayConfigs[flag]
	traceReplayConfig.Args = append(traceReplayConfigs[flag].Args, "--dump-per-frame-stats=/tmp/per_frame_stats.json")

	time_start := time.Now()
	time_now := time_start
	time_end := time_now.Add(time.Duration(config.ExtendedDuration) * time.Second)
	run_count := 0
	msg := fmt.Sprintf("Extended trace replay session configured to last %0.2f minutes, with <%s> flag", float32(config.ExtendedDuration)/60.0, flag)
	logMsg(ctx, config.ProxyServer.URL, msg)
	for time_now.Before(time_end) {
		msg := fmt.Sprintf("Replaying the trace with <%s> flag, #%d at +%v from test start", flag, run_count+1, time.Since(time_start))
		logMsg(ctx, config.ProxyServer.URL, msg)
		rr, err := replayTrace(ctx, traceReplayConfig, traceFileName, replayTimeout)
		if err != nil {
			return res, err
		}

		replayDesc := fmt.Sprintf("replay%03d", run_count+1)
		if err := notifyReplayFinished(ctx, config.ProxyServer.URL, replayDesc, float64(time_now.UnixNano()/1e9)); err != nil {
			return res, err
		}

		// TODO(ryanneph): We need to return results of every trace replay. map is not most convenient for this
		for k, v := range rr {
			res[fmt.Sprintf("%s_%s", replayDesc, k)] = v
		}

		time_now = time.Now()
		run_count++
	}

	return res, nil
}

func runTest(ctx context.Context, config *comm.TestGroupConfig, traceEntry *repo.TraceListEntry) (map[string]comm.ValueEntry, error) {
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Preparing to run %v", *traceEntry))
	// check is it enough space to run the test (container file size + trace file size + 16MB)
	requiredSpace := traceEntry.StorageFile.Size + traceEntry.TraceFile.Size + uint64(16*1204*1024)
	freeSpace, err := utils.GetFreeSpace(tempFolder)
	if err != nil {
		logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Unable to get free space information: %s", err.Error()))
	} else {
		space_info := fmt.Sprintf("Available space at <%s>: %s bytes, Required space: %s bytes", tempFolder, utils.FormatSize(freeSpace), utils.FormatSize(requiredSpace))
		logMsg(ctx, config.ProxyServer.URL, space_info)
		if freeSpace < requiredSpace {
			// Dump the content of tempFolder
			files, err := listFiles(tempFolder)
			if err != nil {
				logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Unable to read the content of %s: %s",
					tempFolder, err.Error()))
			} else {
				logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("The content of %s: %v", tempFolder, files))
			}
			return nil, errors.New("not enough space to run %s test. %s", traceEntry.Name, space_info)
		}
	}

	// Download trace file via proxy server
	downloadStart := time.Now()
	downloadedFileName, err := downloadFile(ctx, tempFolder, config.ProxyServer.URL, traceEntry.StorageFile.Name)
	if err != nil {
		return nil, err
	}
	downloadDuration := time.Since(downloadStart)
	defer os.Remove(downloadedFileName)

	// Perform integrity checks on the downloaded file
	fileInfo, err := os.Stat(downloadedFileName)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to get stat for %s", downloadedFileName)
	}
	sizeInMB := float64(fileInfo.Size()) / (1024.0 * 1024.0)
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("The %.2f MB file was downloaded to %s in %v (%.2f MB/s)", sizeInMB, downloadedFileName, downloadDuration, sizeInMB/downloadDuration.Seconds()))

	if uint64(fileInfo.Size()) != traceEntry.StorageFile.Size {
		return nil, errors.New("Actual file size of %s is different from the value in metadata. Actual: %db, expected: %db", downloadedFileName, fileInfo.Size(), traceEntry.StorageFile.Size)
	}

	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Decompressing %s", downloadedFileName))
	traceFileName, err := decompressFile(ctx, downloadedFileName, ".trace")
	if err != nil {
		return nil, err
	}
	defer os.Remove(traceFileName)

	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Validating MD5 checksum for %s", traceFileName))
	traceFileMD5Sum, err := utils.GetFileMD5Sum(ctx, traceFileName)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to calculate MD5 checksum for %s", traceFileName)
	}

	if traceFileMD5Sum != traceEntry.TraceFile.MD5Sum {
		return nil, errors.New("Actual file MD5 checksum for %s is different from the value in metadata. Actual: %s, expected: %s", downloadedFileName, traceFileMD5Sum, traceEntry.TraceFile.MD5Sum)
	}

	// Cool down and flush all pending filesistem pending i/o ops
	time.Sleep(time.Duration(replayCoolDownTime) * time.Second)
	exec.Command("sync").Run()

	// TODO(tutankhamen): save the trace file with meta information to the local cache

	// We can't exceed the test timeout
	var replayTimeout uint32 = replayMaxTime
	if traceEntry.ReplayTimeout != 0 {
		replayTimeout = traceEntry.ReplayTimeout
	}

	// Run the trace replay(s)
	if config.ExtendedDuration > 0 {
		return runReplayRepeatedly(ctx, config, traceFileName, replayTimeout)
	} else {
		return runReplayOnce(ctx, config, traceFileName, replayTimeout)
	}
}

func main() {
	startTime := time.Now()
	// Check arguments and unmarshall config json
	if len(os.Args) != 2 {
		exitWithError(errors.New("invalid command line arguments count.\nUsage: cros_retrace <config_json | --version>"))
	}
	if os.Args[1] == "--version" || os.Args[1] == "-v" {
		versionInfo := comm.VersionInfo{
			ProtocolVersion: comm.ProtocolVersion,
		}
		versionInfoJSON, _ := json.Marshal(versionInfo)
		fmt.Println(string(versionInfoJSON))
		os.Exit(0)
	}
	// Unmarshal the  config argument json
	var config comm.TestGroupConfig
	err := json.Unmarshal([]byte(os.Args[1]), &config)
	if err != nil {
		exitWithError(errors.New("Unable to parse config <%s>: [%s]", os.Args[1], err.Error()))
	}
	// Validate the test config
	if config.ProxyServer.URL == "" {
		exitWithError(errors.New("Proxy server isn't specified"))
	}

	if config.Repository.RootURL == "" {
		exitWithError(errors.New("Storage repository url isn't specified"))
	}

	ctx := context.Background()
	runTimeout := defaultTimeout
	if config.Timeout != 0 {
		runTimeout = int(config.Timeout)
	}
	// Run long enough to complete extended test, if it has been requested
	if config.ExtendedDuration > 0 {
		runTimeout = utils.MaxOfInt(defaultTimeout, int(config.Timeout), 60*10+int(config.ExtendedDuration))
	}
	ctx, cancel := context.WithTimeout(ctx, time.Duration(runTimeout)*time.Second)
	defer cancel()

	// TODO(tutankhamen): System environment in Borealis doesn't include PATH
	// variable for some reason
	if _, set := os.LookupEnv("PATH"); !set {
		os.Setenv("PATH", "/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin")
	}

	// fetch the trace list from the repository
	traceList, err := getTraceList(ctx, &config)
	if err != nil {
		exitWithError(err)
	}

	// Check prerequisites (apitrace, bz2, etc)
	for _, pkgName := range requiredPackages {
		if err := checkPackageInstalled(ctx, pkgName); err != nil {
			exitWithError(err)
		}
	}

	// TODO(tutankhamen): check if trace file is already exist in the local cache
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Filter test entries based on label: %v", config.Labels))
	traceEntries, err := getTraceEntries(traceList, &config.Labels)
	if err != nil {
		exitWithError(err)
	}
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Number of filtered entries: %v", len(traceEntries)))

	if len(traceEntries) == 0 {
		exitWithError(errors.New("No trace entries found to match the selection attributes %vs. TraceList: %v", config.Labels, *traceList))
	}

	var result comm.TestGroupResult
	succeededCount := 0
	for _, entry := range traceEntries {
		entryResult := comm.TestEntryResult{Name: entry.Name}
		replayValues, err := runTest(ctx, &config, &entry)
		if err != nil {
			entryResult.Result = comm.TestResultFailure
			entryResult.Message = err.Error()
		} else {
			entryResult.Result = comm.TestResultSuccess
			entryResult.Values = replayValues
			succeededCount++
		}
		result.Entries = append(result.Entries, entryResult)
		// Cancel all the susbsequent tests due to the main context is expired
		if ctx.Err() != nil {
			break
		}
	}

	if len(traceEntries) == succeededCount {
		result.Result = comm.TestResultSuccess
		result.Message = fmt.Sprintf("Finished successfully in %v", time.Since(startTime))
	} else {
		result.Result = comm.TestResultFailure
		if ctx.Err() != nil {
			result.Message = fmt.Sprintf("Failed with timeout. %v. ", ctx.Err())
		} else {
			if len(result.Entries) == 1 {
				result.Message = result.Entries[0].Message
			} else {
				result.Message = "Failed. Not all tests succeeded"
			}
		}
		result.Message += fmt.Sprintf(". Total/Finished/Succeeded %d/%d/%d tests in %v.", len(traceEntries), len(result.Entries), succeededCount, time.Since(startTime))
	}

	outputResult(result)
}
