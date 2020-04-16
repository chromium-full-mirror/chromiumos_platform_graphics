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
	tempFolder       = "/tmp"
	apitraceAppName  = "glretrace"
	apitraceOutputRE = `Rendered (\d+) frames in (\d*\.?\d*) secs, average of (\d*\.?\d*) fps`
	// Default application timeout in seconds
	defaultTimeout = 60 * 60
	// Maximum allowed replay time for one trace in seonds
	replayMaxTime = 15 * 60
	// Cooling down time before each trace replay in seconds
	replayCoolDownTime = 30
)

var (
	apitraceArgs     = []string{"--benchmark"}
	requiredPackages = []string{"apitrace", "zstd"}
)

func runCommand(name string, args ...string) (exitCode int, stdout string, stderr string) {
	var outbuf, errbuf bytes.Buffer
	var waitStatus syscall.WaitStatus
	cmd := exec.Command(name, args...)
	cmd.Stdout = &outbuf
	cmd.Stderr = &errbuf

	err := cmd.Run()
	stdout = outbuf.String()
	stderr = errbuf.String()

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
	var decompressCmd *exec.Cmd
	fileExt := filepath.Ext(fileName)

	switch fileExt {
	case expectedExt:
		return fileName, nil
	case ".bz2":
		decompressCmd = exec.Command("bunzip2", "-f", fileName)
	case ".zst", ".xz":
		decompressCmd = exec.Command("zstd", "-d", "-f", "--rm", "-T0", fileName)
	default:
		return "", errors.New("Unknown trace extension: %s", fileExt)
	}
	if out, err := decompressCmd.CombinedOutput(); err != nil {
		return "", errors.Wrap(err, "Unable to decompress <%s>. Combined output: %s", fileName, string(out))
	}
	return strings.TrimSuffix(fileName, filepath.Ext(fileName)), nil
}

func httpRequestToFile(ctx context.Context, url, outFile string) error {
	httpRequest, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return errors.Wrap(err, "http.NewRequestWithContext(%s) failed", url)
	}
	httpClient := &http.Client{}

	httpResponse, err := httpClient.Do(httpRequest)
	if err != nil {
		return errors.Wrap(err, "http.Do(%v) failed", httpRequest)
	}
	defer httpResponse.Body.Close()

	if httpResponse.StatusCode == http.StatusOK {
		localFile, err := os.Create(outFile)
		if err != nil {
			return errors.Wrap(err, "os.Create(%s) failed", outFile)
		}
		defer localFile.Close()
		err = utils.CopyWithContext(ctx, localFile, httpResponse.Body)
		if err != nil {
			return errors.Wrap(err, "io.Copy() failed")
		}
		return nil
	}
	return errors.New("http status code isn't OK: %d", httpResponse.StatusCode)
}

// Downloads a file using relative file path [filePath] via proxy http server
// [proxyURL] and saves it to the specified directory [localPath]
// returns the full name to the local result file or error
func downloadFile(ctx context.Context, localPath, proxyURL, filePath string) (string, error) {
	downloadURL, err := url.Parse(proxyURL)
	if err != nil {
		return "", errors.Wrap(err, "Unable to parse proxy server URL <%s>", proxyURL)
	}
	downloadURLParams := url.Values{}
	// Use only one URL argument for now: d=filePath
	downloadURLParams.Add("d", filePath)
	downloadURL.RawQuery = downloadURLParams.Encode()
	localFileName := path.Join(localPath, path.Base(filePath))
	if err := httpRequestToFile(ctx, downloadURL.String(), localFileName); err != nil {
		return "", err
	}
	return localFileName, nil
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

func parseReplayOutput(output string) (*comm.ReplayResult, error) {
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
	return &comm.ReplayResult{
		TotalFrames:       uint32(totalFrames),
		AverageFPS:        float32(averageFPS),
		DurationInSeconds: float32(durationInSeconds),
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

func checkPackageInstalled(name string) error {
	if exitCode, _, stderr := runCommand("dpkg", "-l", name); exitCode != 0 {
		return errors.Wrap(fmt.Errorf("%s", stderr), "dpkg for %s failed with exit code %d!", name, exitCode)
	}
	return nil
}

func replayTrace(ctx context.Context, traceFileName string) (*comm.ReplayResult, error) {
	cmd := exec.CommandContext(ctx, apitraceAppName, append(apitraceArgs, traceFileName)...)
	out, err := cmd.CombinedOutput()

	if ctx.Err() == context.DeadlineExceeded {
		// In case of timeout the err is always "signal: killed", so, it's better to replace it
		// with more informative DeadlineExceeded error
		err = ctx.Err()
	}

	if err != nil {
		return nil, errors.Wrap(err, "Failed to replay trace file [%s]", traceFileName)
	}
	return parseReplayOutput(string(out))
}

func runTest(ctx context.Context, config *comm.TestGroupConfig, traceEntry *repo.TraceListEntry) (*[]comm.ReplayResult, error) {
	//TODO(tutankhamen): Check for free space (container file size + trace file size + some extra?)

	// Download trace file via proxy server
	downloadedFileName, err := downloadFile(ctx, tempFolder, config.ProxyServer.URL, traceEntry.StorageFile.Name)
	if err != nil {
		return nil, err
	}
	defer os.Remove(downloadedFileName)

	// Perform integrity checks on the downloaded file
	fileInfo, err := os.Stat(downloadedFileName)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to get stat for %s", downloadedFileName)
	}

	if uint64(fileInfo.Size()) != traceEntry.StorageFile.Size {
		return nil, errors.New("Actual file size of %s is different from the value in metadata. Actual: %db, expected: %db", downloadedFileName, fileInfo.Size(), traceEntry.StorageFile.Size)
	}

	traceFileName, err := decompressFile(ctx, downloadedFileName, ".trace")
	if err != nil {
		return nil, err
	}
	defer os.Remove(traceFileName)

	traceFileMD5Sum, err := utils.GetFileMD5Sum(ctx, traceFileName)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to calculate MD5 checksum for %s", traceFileName)
	}

	if traceFileMD5Sum != traceEntry.TraceFile.MD5Sum {
		return nil, errors.New("Actual file MD5 checksum for %s is different from the value in metadata. Actual: %s, expected: %s", downloadedFileName, traceFileMD5Sum, traceEntry.TraceFile.MD5Sum)
	}

	// Cooling down
	time.Sleep(time.Duration(replayCoolDownTime) * time.Second)

	// Execute all pending file system reads and writes
	exec.Command("sync").Run()

	// TODO(tutankhamen): save the trace file with meta information to the local cache

	// We can't exceed replay timeout
	var replayTimeout uint32 = replayMaxTime
	if traceEntry.ReplayTimeout != 0 {
		replayTimeout = traceEntry.ReplayTimeout
	}
	ctx, cancel := context.WithTimeout(ctx, time.Duration(replayTimeout)*time.Second)
	defer cancel()
	var replayResults []comm.ReplayResult
	result, err := replayTrace(ctx, traceFileName)
	if err != nil {
		return nil, err
	}
	replayResults = append(replayResults, *result)

	return &replayResults, nil
}

func main() {
	startTime := time.Now()
	// Check arguments and unmarshall config json
	if len(os.Args) != 2 {
		exitWithError(errors.New("invalid command line arguments count.\nUsage: cros_retrace <config_json>"))
	}
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
	ctx, cancel := context.WithTimeout(ctx, time.Duration(runTimeout)*time.Second)
	defer cancel()

	// fetch the trace list from the repository
	traceList, err := getTraceList(ctx, &config)
	if err != nil {
		exitWithError(err)
	}

	// Check prerequisites (apitrace, bz2, etc)
	for _, pkgName := range requiredPackages {
		if err := checkPackageInstalled(pkgName); err != nil {
			exitWithError(err)
		}
	}

	// TODO(tutankhamen): check if trace file is already exist in the local cache

	traceEntries, err := getTraceEntries(traceList, &config.Labels)
	if err != nil {
		exitWithError(err)
	}

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
			entryResult.Values = *replayValues
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
			result.Message = "Failed. Not all tests succeeded. "
		}
		result.Message += fmt.Sprintf("Total/Finished/Succeeded %d/%d/%d tests in %v.", len(traceEntries), len(result.Entries), succeededCount, time.Since(startTime))
	}

	outputResult(result)
}
