// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
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

	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/comm"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/flags"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/labels"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/repo"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/utils"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/pkg/errors"
)

const (
	appDataDir       = "trace_replay.tmp"
	tmpfsDir         = "/tmp"
	minRequiredSpace = 1024 * 1024
	maxRefImageSize  = 3 * 1024 * 1024
	apitraceOutputRE = `Rendered (\d+) frames in (\d*\.?\d*) secs, average of (\d*\.?\d*) fps`
	// Default application timeout in seconds
	defaultTimeout = 60 * 60
	// Maximum allowed replay time for one trace in seonds
	replayMaxTime = 15 * 60
	// Minimum replay timeout for one trace in seconds.
	// Can't be less than 10 due to nested app timeout which is (replayMinTime-10)
	replayMinTime = 30
)

var (
	requiredPackages = []string{"apitrace", "zstd"}
)

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
			stderr = fmt.Sprintf("Error: %s. Stderr: [%s]", err.Error(), stderr)
		}
	} else {
		waitStatus = cmd.ProcessState.Sys().(syscall.WaitStatus)
		exitCode = waitStatus.ExitStatus()
	}
	return
}

func validateFileSize(ctx context.Context, fileName string, expectedSize uint64) (uint64, error) {
	fileInfo, err := os.Stat(fileName)
	if err != nil {
		return 0, errors.Wrap(err, "Unable to get stat for %s", fileName)
	}

	if uint64(fileInfo.Size()) != expectedSize {
		return uint64(fileInfo.Size()), errors.New("File size %db != %db expected (%s)", fileInfo.Size(), expectedSize, path.Base(fileName))
	}
	return uint64(fileInfo.Size()), nil
}

func validateFileMD5(ctx context.Context, fileName string, expectedMD5 string) (string, error) {
	fileMD5, err := utils.GetFileMD5Sum(ctx, fileName)
	if err != nil {
		return fileMD5, errors.Wrap(err, "Unable to calculate MD5 checksum for %s", path.Base(fileName))
	}

	if fileMD5 != expectedMD5 {
		return fileMD5, errors.New("MD5 for %s is wrong (%s, expected: %s)", path.Base(fileName), fileMD5, expectedMD5)
	}
	return fileMD5, nil
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

func getTempDataStorageDir(storageRoot string, requiredSpace uint64) (string, uint64, error) {
	freeSpace, err := utils.GetFreeSpace(storageRoot)
	if err != nil {
		return "", 0, errors.Wrap(err, "Unable to get free space information for %s", storageRoot)
	}

	spaceInfo := fmt.Sprintf("Available space at <%s>: %s bytes, Required space: %s bytes", storageRoot, utils.FormatSize(freeSpace), utils.FormatSize(requiredSpace))
	if freeSpace < requiredSpace {
		return "", freeSpace, errors.New("Not enough space. %s", spaceInfo)
	}

	resultDir := path.Join(storageRoot, appDataDir)
	if _, err := os.Stat(resultDir); os.IsNotExist(err) {
		if err = os.MkdirAll(resultDir, 0777); err != nil {
			return "", freeSpace, errors.Wrap(err, "Unable to create directory %s", resultDir)
		}
	} else {
		if err = utils.ClearDirectory(resultDir); err != nil {
			return "", freeSpace, errors.Wrap(err, "Unable to clear %s directory content", resultDir)
		}
	}
	return resultDir, freeSpace, nil
}

// httpRequestWrapper request the server and return the http.Response. Caller must close the response once finished processing.
func httpRequestWrapper(ctx context.Context, proxyURL string, params url.Values) (*http.Response, error) {
	parsedURL, err := url.Parse(proxyURL)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to parse server URL <%s>", proxyURL)
	}
	parsedURL.RawQuery = params.Encode()

	httpRequest, err := http.NewRequestWithContext(ctx, http.MethodGet, parsedURL.String(), nil)
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
		return nil, errors.New("httpRequestWrapper: HTTP result code: %d %s", httpResponse.StatusCode, http.StatusText(httpResponse.StatusCode))
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

// uploadFile uploads a file to the host's test results folder which will be published
// in Stainless along with test log files and other test artifacts
func uploadFile(ctx context.Context, localFileName, serverURL, remoteFileName string) error {
	logMsg(ctx, serverURL, fmt.Sprintf("Uploading %s to %s...", localFileName, remoteFileName))
	reader, err := os.Open(localFileName)
	if err != nil {
		return errors.Wrap(err, "uploadFile: io.Writer.CreateFromFile() failed. Unable to open [%s]", localFileName)
	}
	defer reader.Close()

	var buffer bytes.Buffer
	var formFileWriter io.Writer
	writer := multipart.NewWriter(&buffer)
	if formFileWriter, err = writer.CreateFormFile("file", remoteFileName); err != nil {
		return errors.Wrap(err, "uploadFile: io.Writer.CreateFormFile() failed")
	}
	if _, err = io.Copy(formFileWriter, reader); err != nil {
		return errors.Wrap(err, "uploadFile: io.Copy() failed")
	}
	writer.Close()

	uploadURL, err := url.Parse(serverURL)
	if err != nil {
		return errors.Wrap(err, "uploadFile: urlParse() failed. Invalid URL: %s", serverURL)
	}
	params := url.Values{}
	params.Add("type", "upload")
	uploadURL.RawQuery = params.Encode()
	request, _ := http.NewRequestWithContext(ctx, http.MethodPost, uploadURL.String(), &buffer)
	request.Header.Set("Content-Type", writer.FormDataContentType())

	httpClient := &http.Client{}
	response, err := httpClient.Do(request)
	if err != nil {
		return errors.Wrap(err, "uploadFile: http.Do(%v) failed", request)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return errors.New("uploadFile: HTTP status code: %d %s", response.StatusCode, http.StatusText(response.StatusCode))
	}
	return nil
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
	storageDir, _, err := getTempDataStorageDir(tmpfsDir, minRequiredSpace)
	if err != nil {
		return nil, err
	}
	traceListFileName := fmt.Sprintf("repo.%d.json", config.Repository.Version)
	fileName, err := downloadFile(ctx, storageDir, config.ProxyServer.URL, traceListFileName)
	if err != nil {
		return nil, err
	}
	defer os.Remove(fileName)

	file, err := os.Open(fileName)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to open downloaded <%s>", fileName)
	}
	defer file.Close()

	bytes, _ := io.ReadAll(file)
	var traceList repo.TraceList
	err = json.Unmarshal(bytes, &traceList)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to parse trace list")
	}

	return &traceList, nil
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
		"frames" + postfix: {
			Unit:      "frame",
			Direction: 0,
			Value:     float32(totalFrames),
		}, "fps" + postfix: {
			Unit:      "fps",
			Direction: +1,
			Value:     float32(averageFPS),
		}, "time" + postfix: {
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
	// Attempt to dpkg -l (for Debian/Ubuntu) and, if that fails, pacman -Q (for Arch).
	if exitCode, _, _ := runCommand(ctx, nil, "dpkg", "-l", name); exitCode != 0 {
		if exitCode, _, stderr := runCommand(ctx, nil, "pacman", "-Q", name); exitCode != 0 {
			return errors.New("dpkg -l and pacman -Q for %s failed with exit code %d! %s", name, exitCode, stderr)
		}
	}
	return nil
}

// TODO(syedfaaiz) Need to either get rid of the timeout on Proton or find out a way to use it in the expected manner.
func replayTrace(ctx context.Context, config flags.ReplayAppConfig, traceFileName string, timeoutInSeconds uint32, proton bool) (map[string]comm.ValueEntry, error) {
	if proton {
		copyD3dBinaries(ctx)
	}
	appArgs := config.Args
	if timeoutInSeconds < replayMinTime {
		return nil, errors.New("The requested timeout is too short to replay a trace file. Requested: %d, wanted >= %d", timeoutInSeconds, replayMinTime)
	}
	if !proton {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, time.Duration(timeoutInSeconds)*time.Second)
		defer cancel()

		// Add nested timeout to glretrace/eglretrace
		appArgs = append(appArgs, fmt.Sprintf("--timeout=%d", timeoutInSeconds-10))
	}
	appArgs = append(appArgs, traceFileName)
	exitCode, stdout, stderr := runCommand(ctx, config.EnvVars, config.AppName, appArgs...)
	if exitCode != 0 {
		return nil, errors.New("Failed to replay trace file [%s]. Exit code: %d. %s", traceFileName, exitCode, stderr)
	}
	return parseReplayOutput(stdout, config.Postfix)
}

// TODO(syedfaaiz) : Remove this copy once paths related issue is resolved.
// The destination directory is a file d3dretrace32/64.exe located on the home directory.
// This work-around prints out the average fps of each trace, which in turn can be then picked up
// by the regex and saved in results-chart.json for analysis.
func copyD3dBinaries(ctx context.Context) error {
	exitCode, _, stderr := runCommand(ctx, nil, "cp", "/opt/win_tools/apitrace/i386/d3dretrace.exe", flags.ApitraceW32)
	if exitCode != 0 {
		return errors.New("failed to copy d3dretrace32. Exit code  %d. %s", exitCode, stderr)
	}
	exitCode, _, stderr = runCommand(ctx, nil, "cp", "/opt/win_tools/apitrace/x86_64/d3dretrace.exe", flags.ApitraceW64)
	if exitCode != 0 {
		return errors.New("failed to copy d3dretrace64. Exit code : %d. %s", exitCode, stderr)
	}
	return nil
}

// contains is case insensitive with regards to the tofind param.
func contains(arr []string, tofind string) bool {
	for _, val := range arr {
		if strings.EqualFold(val, tofind) {
			return true
		}
	}
	return false
}

func runReplayOnce(ctx context.Context, config *comm.TestGroupConfig, traceFileName string, replayTimeout uint32) (map[string]comm.ValueEntry, error) {
	res := make(map[string]comm.ValueEntry)
	if err := notifyInitFinished(ctx, config.ProxyServer.URL); err != nil {
		return res, err
	}

	if len(config.Flags) == 0 {
		logMsg(ctx, config.ProxyServer.URL, "No flag specified, running with TestFlagDefault.")
		config.Flags = append([]string{comm.TestFlagDefault}, config.Flags...)
	}
	// Replay the trace file with custom settings corresponding to an each flag list entry
	for _, flag := range config.Flags {
		replayConfig, err := flags.GetReplayAppConfigs(flag)
		if err != nil {
			logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Warning, Unable to find a trace replay config: %v, Skip running with flag: %v.", err, flag))
			continue
		}
		proton := contains([]string{comm.TestFlagD3DW32, comm.TestFlagD3DW64}, flag)
		// Flush all pending filesistem pending i/o ops
		logMsg(ctx, config.ProxyServer.URL, "Syncing file system")
		exec.Command("sync").Run()
		if proton {
			logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Replaying the trace file with Proton and %v binary", replayConfig.AppName))
		} else {
			logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Replaying the trace file with the default settings and %d seconds timeout...", replayTimeout))
		}
		rr, err := replayTrace(ctx, replayConfig, traceFileName, replayTimeout, proton)
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
	if err := notifyInitFinished(ctx, config.ProxyServer.URL); err != nil {
		return res, err
	}

	flag := comm.TestFlagDefault
	if len(config.Flags) > 0 {
		flag = config.Flags[0]
		msg := fmt.Sprintf("Using only the first of the specified replay flags: <%s>", flag)
		logMsg(ctx, config.ProxyServer.URL, msg)
	}
	traceReplayConfig, err := flags.GetReplayAppConfigs(flag)
	if err != nil {
		return res, errors.Wrap(err, "unable to find a trace replay config: %v", err)
	}
	proton := contains([]string{comm.TestFlagD3DW32, comm.TestFlagD3DW64}, flag)
	if !proton {
		traceReplayConfig.Args = append(traceReplayConfig.Args, "--dump-per-frame-stats=/tmp/per_frame_stats.json")
	}

	timeStart := time.Now()
	timeNow := timeStart
	timeEnd := timeNow.Add(time.Duration(config.ExtendedDuration) * time.Second)
	runCount := 0
	msg := fmt.Sprintf("Extended trace replay session configured to last %0.2f minutes, with <%s> flag", float32(config.ExtendedDuration)/60.0, flag)
	logMsg(ctx, config.ProxyServer.URL, msg)
	for (config.ExtendedDuration > 0 && timeNow.Before(timeEnd)) || (config.RepeatCount > 0 && runCount < int(config.RepeatCount)) {
		timeSinceStr := strings.ReplaceAll(time.Since(timeStart).String(), "µ", "u")
		msg := fmt.Sprintf("Replaying the trace with <%s> flag, #%d at +%s from test start", flag, runCount+1, timeSinceStr)
		logMsg(ctx, config.ProxyServer.URL, msg)
		rr, err := replayTrace(ctx, traceReplayConfig, traceFileName, replayTimeout, proton)
		if err != nil {
			return res, err
		}

		replayDesc := fmt.Sprintf("replay%03d", runCount+1)
		if err := notifyReplayFinished(ctx, config.ProxyServer.URL, replayDesc, float64(timeNow.UnixNano()/1e9)); err != nil {
			return res, err
		}

		// TODO(ryanneph): We need to return results of every trace replay. map is not most convenient for this
		for k, v := range rr {
			res[fmt.Sprintf("%s_%s", replayDesc, k)] = v
		}

		timeNow = time.Now()
		runCount++
	}

	return res, nil
}

// dumpTraceImages captures color buffers of requested frames into PNG files.
// Returns a result as a map of frameId->fileName or an error
func dumpTraceImages(ctx context.Context, config *comm.TestGroupConfig, traceFileName string, traceEntry *repo.TraceListEntry, outDir string) (map[uint32]string, error) {
	res := make(map[uint32]string)
	callsStr := ""
	logMsg(ctx, config.ProxyServer.URL, "Replaying the trace file to dump the reference images...")
	for idx, entry := range traceEntry.ReferenceFrames {
		if idx != 0 {
			callsStr += ","
		}
		callsStr += strconv.FormatUint(uint64(entry.CallID), 10)
	}
	args := []string{"dump-images", "--calls=" + callsStr, "-o", path.Join(outDir, "dmp_"), traceFileName}
	exitCode, _, stderr := runCommand(ctx, []string{"DISPLAY=:0"}, "apitrace", args...)
	if exitCode != 0 {
		return nil, errors.New("Failed to dump images for trace file [%s]. Exit code: %d. %s", traceFileName, exitCode, stderr)
	}
	for _, entry := range traceEntry.ReferenceFrames {
		res[entry.CallID] = path.Join(outDir, fmt.Sprintf("dmp_%010d.png", entry.CallID))
	}
	return res, nil
}

func calcRequiredSpace(traceEntry *repo.TraceListEntry) uint64 {
	// Compressed and decompressed copy of the trace file
	requiredSpace := traceEntry.StorageFile.Size + traceEntry.TraceFile.Size
	// Reference and captured frames
	for _, refFrame := range traceEntry.ReferenceFrames {
		requiredSpace += maxRefImageSize
		if refFrame.FileSize != 0 {
			requiredSpace += refFrame.FileSize
		}
	}
	// Add extra 128 megabytes for logs and unseen circumstances
	requiredSpace += uint64(128 * 1024 * 1024)

	return requiredSpace
}

func runTest(ctx context.Context, config *comm.TestGroupConfig, traceEntry *repo.TraceListEntry) (map[string]comm.ValueEntry, error) {
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Preparing to run %v", *traceEntry))
	requiredSpace := calcRequiredSpace(traceEntry)
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Required space: %s bytes", utils.FormatSize(requiredSpace)))

	// Save traces to storage rather than tmpfs because use of tmpfs appears to worsen trace replay performance consistency (low memory leading to higher swap rate?)
	// Use user's home directory to save downloaded traces. Tast recreates the user account on each run so no need to clean up files afterward.
	userHomeDir, err := os.UserHomeDir()
	if err != nil {
		return nil, errors.Wrap(err, "Unable to get User's home directory")
	}
	storageDir, availableSpace, err := getTempDataStorageDir(userHomeDir, requiredSpace)
	if err != nil {
		return nil, err
	}

	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Using %s to store the data files. Available space: %s bytes", storageDir, utils.FormatSize(availableSpace)))

	// Download trace file via proxy server
	downloadStart := time.Now()
	downloadedFileName, err := downloadFile(ctx, storageDir, config.ProxyServer.URL, traceEntry.StorageFile.Name)
	if err != nil {
		return nil, err
	}
	downloadDuration := time.Since(downloadStart)
	defer os.Remove(downloadedFileName)

	// Perform integrity checks on the downloaded file
	downloadedFileSize, err := validateFileSize(ctx, downloadedFileName, traceEntry.StorageFile.Size)
	if err != nil {
		return nil, err
	}
	sizeInMB := float64(downloadedFileSize) / (1024.0 * 1024.0)
	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("The %.2f MB file was downloaded to %s in %v (%.2f MB/s)", sizeInMB, downloadedFileName, downloadDuration, sizeInMB/downloadDuration.Seconds()))

	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Decompressing %s", downloadedFileName))
	traceFileName, err := decompressFile(ctx, downloadedFileName, ".trace")
	if err != nil {
		return nil, err
	}
	defer os.Remove(traceFileName)

	logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Validating MD5 checksum for %s", traceFileName))
	if _, err := validateFileMD5(ctx, traceFileName, traceEntry.TraceFile.MD5Sum); err != nil {
		return nil, err
	}

	// Cool down and flush all pending filesistem pending i/o ops
	logMsg(ctx, config.ProxyServer.URL, "Syncing file system")
	exec.Command("sync").Run()
	logMsg(ctx, config.ProxyServer.URL, "Syncing has finished")

	// TODO(tutankhamen): save the trace file with meta information to the local cache

	// We can't exceed the test timeout
	var replayTimeout uint32 = replayMaxTime
	if traceEntry.ReplayTimeout != 0 {
		replayTimeout = traceEntry.ReplayTimeout
	}

	if config.ExtendedDuration > 0 && config.RepeatCount > 0 {
		return nil, errors.New("Couldn't specify both ExtendedDuration and RepeatCount at the same time for the test setting")
	}
	// Run the trace replay(s)
	if config.ExtendedDuration > 0 || config.RepeatCount > 0 {
		logMsg(ctx, config.ProxyServer.URL, "Running trace replay in extended mode")
		return runReplayRepeatedly(ctx, config, traceFileName, replayTimeout)
	}
	result, err := runReplayOnce(ctx, config, traceFileName, replayTimeout)
	if err != nil {
		return result, err
	}

	// TODO(tutankhamen): Add test group flag to enable/disable frames capture/validation
	if len(traceEntry.ReferenceFrames) > 0 {
		// Download reference frames (if available) and upload them to the host
		for _, refFrame := range traceEntry.ReferenceFrames {
			if refFrame.FileName != "" {
				refFrameFile, err := downloadFile(ctx, storageDir, config.ProxyServer.URL, refFrame.FileName)
				if err != nil {
					return result, errors.Wrap(err, "Unable to download a reference frame")
				}
				if _, err := validateFileMD5(ctx, refFrameFile, refFrame.FileMD5); err != nil {
					return result, err
				}
				refFrameDstFile := fmt.Sprintf("images/reference/%s/%010d.png", refFrame.Board, refFrame.CallID)
				if err := uploadFile(ctx, refFrameFile, config.ProxyServer.URL, refFrameDstFile); err != nil {
					return result, errors.Wrap(err, "Unable to upload a reference frame")
				}
			}
		}
		// Dump trace images for comparison
		dumped, err := dumpTraceImages(ctx, config, traceFileName, traceEntry, storageDir)
		if err != nil {
			return result, errors.Wrap(err, "dumpFrameImages failed")
		}
		for dmpCallID, dmpImageFile := range dumped {
			var fileSize int64
			fileInfo, err := os.Stat(dmpImageFile)
			if err == nil {
				fileSize = fileInfo.Size()
			} else {
				logMsg(ctx, config.ProxyServer.URL, fmt.Sprintf("Warning: os.Stat() failed for %s", dmpImageFile))
			}
			result[fmt.Sprintf("size_%010d", dmpCallID)] = comm.ValueEntry{
				Unit:      "bytes",
				Direction: 0,
				// TODO(tutankhamen): change type of comm.ValueEntry.Value to float64 to prevent precesion limitation related issues
				Value: float32(fileSize),
			}
			dmpImageDstFile := fmt.Sprintf("images/result/%s/%010d.png", config.Host.Board, dmpCallID)
			if err := uploadFile(ctx, dmpImageFile, config.ProxyServer.URL, dmpImageDstFile); err != nil {
				return result, errors.Wrap(err, "Unable to upload a dumped image")
			}
		}
	}
	return result, nil
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
	// Unmarshal the config argument json
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
	traceEntries, err := labels.GetTraceEntries(traceList, &config.Labels)
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
