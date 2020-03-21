// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"trace_replay/cmd/trace_replay/comm"
	"trace_replay/cmd/trace_replay/repo"
)

const (
	tempFolder       = "/tmp"
	apitraceAppName  = "apitrace"
	apitraceOutputRE = `Rendered (\d+) frames in (\d*\.?\d*) secs, average of (\d*\.?\d*) fps`
)

var (
	apitraceArgs     = []string{"replay", "--benchmark"}
	requiredPackages = []string{"apitrace", "zstd"}
)

type werror struct {
	err  error
	msg  string
	file string
	line int
}

func wrapError(err error, format string, args ...interface{}) *werror {
	_, file, line, ok := runtime.Caller(1)
	if !ok {
		file = "unknown"
	}
	msg := fmt.Sprintf(format, args...)
	return &werror{err, msg, path.Base(file), line}
}

func (e *werror) String() string {
	errMsg := "nil"
	if e.err != nil {
		errMsg = e.err.Error()
	}
	return fmt.Sprintf("ERROR: %s (%s)! [%s:%d]", e.msg, errMsg, e.file, e.line)
}

func getFileMD5Sum(fileName string) (string, error) {
	file, err := os.Open(fileName)
	if err != nil {
		return "", err
	}
	defer file.Close()
	hash := md5.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", err
	}
	hashInBytes := hash.Sum(nil)[:16]
	return hex.EncodeToString(hashInBytes), nil
}

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

func decompressFile(fileName, expectedExt string) (string, *werror) {
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
		return "", wrapError(nil, "Unknown trace extension: %s", fileExt)
	}
	if out, err := decompressCmd.CombinedOutput(); err != nil {
		return "", wrapError(err, "Unable to decompress <%s>. Combined output: %s", fileName, string(out))
	}
	return strings.TrimSuffix(fileName, filepath.Ext(fileName)), nil
}

func httpRequestToFile(request, outFile string) *werror {
	httpResponse, err := http.Get(request)
	if err != nil {
		return wrapError(err, "http.Get(%s) failed", request)
	}
	defer httpResponse.Body.Close()

	if httpResponse.StatusCode == http.StatusOK {
		localFile, err := os.Create(outFile)
		if err != nil {
			return wrapError(err, "os.Create(%s) failed", outFile)
		}
		defer localFile.Close()
		_, err = io.Copy(localFile, httpResponse.Body)
		if err != nil {
			return wrapError(err, "io.Copy() failed 2")
		}
		return nil
	}
	return wrapError(nil, "HTTP status code isn't OK: %d!", httpResponse.StatusCode)
}

// Downloads a file using relative file path [filePath] via proxy http server
// [proxyURL] and saves it to the specified directory [localPath]
// returns the full name to the local result file or error
func downloadFile(localPath, proxyURL, filePath string) (string, *werror) {
	downloadURL, e := url.Parse(proxyURL)
	if e != nil {
		return "", wrapError(e, "Unable to parse proxy server URL <%s>", proxyURL)
	}
	downloadURLParams := url.Values{}
	// Use only one URL argument for now: d=filePath
	downloadURLParams.Add("d", filePath)
	downloadURL.RawQuery = downloadURLParams.Encode()
	localFileName := path.Join(localPath, path.Base(filePath))
	err := httpRequestToFile(downloadURL.String(), localFileName)
	if err != nil {
		return "", err
	}
	return localFileName, nil
}

// getTraceList function retreives the list of all traces for the repository specified
// in the TestGroupConfig
func getTraceList(config *comm.TestGroupConfig) (*repo.TraceList, *werror) {
	traceListFileName := fmt.Sprintf("repo.%d.json", config.Repository.Version)
	fileName, err := downloadFile(tempFolder, config.ProxyServer.URL, traceListFileName)
	if err != nil {
		return nil, err
	}
	defer os.Remove(fileName)

	file, e := os.Open(fileName)
	if e != nil {
		return nil, wrapError(e, "Unable to open downloaded <%s>", fileName)
	}
	defer file.Close()

	bytes, _ := ioutil.ReadAll(file)
	var traceList repo.TraceList
	e = json.Unmarshal(bytes, &traceList)
	if e != nil {
		return nil, wrapError(e, "Unable to parse trace list")
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
func getTraceEntries(traceList *repo.TraceList, queryLabels *[]string) ([]repo.TraceListEntry, *werror) {
	var result []repo.TraceListEntry
	for _, entry := range traceList.Entries {
		if matchLabels(queryLabels, &entry.Labels) == true {
			result = append(result, entry)
		}
	}
	return result, nil
}

func parseReplayOutput(output string) (*comm.ReplayResult, *werror) {
	re := regexp.MustCompile(apitraceOutputRE)
	match := re.FindStringSubmatch(output)
	if match == nil {
		return nil, wrapError(nil, "Unable to parse apitrace output <%s>", output)
	}
	totalFrames, err := strconv.ParseUint(match[1], 10, 32)
	if err != nil {
		return nil, wrapError(err, "failed to parse frames %q", match[1])
	}
	durationInSeconds, err := strconv.ParseFloat(match[2], 32)
	if err != nil {
		return nil, wrapError(err, "failed to parse duration %q", match[2])
	}
	averageFPS, err := strconv.ParseFloat(match[3], 32)
	if err != nil {
		return nil, wrapError(err, "failed to parse fps %q", match[3])
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

func exitWithError(err *werror) {
	formatMessage := func(err *werror) string {
		if err != nil {
			return err.String()
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

func checkPackageInstalled(name string) *werror {
	if exitCode, _, stderr := runCommand("dpkg", "-l", name); exitCode != 0 {
		return wrapError(fmt.Errorf("%s", stderr), "dpkg for %s failed with exit code %d!", name, exitCode)
	}
	return nil
}

func replayTrace(traceFileName string) (*comm.ReplayResult, *werror) {
	cmd := exec.Command(apitraceAppName, append(apitraceArgs, traceFileName)...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return nil, wrapError(err, "Failed to replay trace file [%s]", traceFileName)
	}
	return parseReplayOutput(string(out))
}

func runTest(config *comm.TestGroupConfig, traceEntry *repo.TraceListEntry) (*[]comm.ReplayResult, *werror) {
	//TODO(tutankhamen): Check for free space (container file size + trace file size + some extra?)

	// Download trace file via proxy server
	downloadedFileName, err := downloadFile(tempFolder, config.ProxyServer.URL, traceEntry.StorageFile.Name)
	if err != nil {
		return nil, err
	}

	// Perform integrity checks on the downloaded file
	fileInfo, e := os.Stat(downloadedFileName)
	if e != nil {
		return nil, wrapError(e, "Unable to get stat for %s", downloadedFileName)
	}

	if uint64(fileInfo.Size()) != traceEntry.StorageFile.Size {
		return nil, wrapError(nil, "Actual file size of %s is different from the value in metadata. Actual: %db, expected: %db", downloadedFileName, fileInfo.Size(), traceEntry.StorageFile.Size)
	}

	traceFileName, err := decompressFile(downloadedFileName, ".trace")
	if err != nil {
		return nil, err
	}

	traceFileMD5Sum, e := getFileMD5Sum(traceFileName)
	if e != nil {
		return nil, wrapError(e, "Unable to calculate MD5 checksum for %s", traceFileName)
	}

	if traceFileMD5Sum != traceEntry.TraceFile.MD5Sum {
		return nil, wrapError(nil, "Actual file MD5 checksum for %s is different from the value in metadata. Actual: %s, expected: %s", downloadedFileName, traceFileMD5Sum, traceEntry.TraceFile.MD5Sum)
	}

	defer os.Remove(traceFileName)

	// TODO(tutankhamen): save the trace file with meta information to the local cache

	var replayResults []comm.ReplayResult
	result, err := replayTrace(traceFileName)
	if err != nil {
		return nil, err
	}
	replayResults = append(replayResults, *result)

	return &replayResults, nil
}

func main() {
	// Check arguments and unmarshall config json
	if len(os.Args) != 2 {
		exitWithError(wrapError(nil, "Invalid command line arguments count.\nUsage: cros_retrace <config_json>\n"))
	}
	var config comm.TestGroupConfig
	e := json.Unmarshal([]byte(os.Args[1]), &config)
	if e != nil {
		exitWithError(wrapError(nil, "Unable to parse config <%s>: [%s]", os.Args[1], e.Error()))
	}
	// Validate the test config
	if config.ProxyServer.URL == "" {
		exitWithError(wrapError(nil, "Proxy server isn't specified"))
	}

	if config.Repository.RootURL == "" {
		exitWithError(wrapError(nil, "Storage repository url isn't specified"))
	}

	// fetch the trace list from the repository
	traceList, err := getTraceList(&config)
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
		exitWithError(wrapError(nil, "No trace entries found to match the selection attributes %vs. TraceList: %v", config.Labels, *traceList))
	}

	var result comm.TestGroupResult
	passedCount := 0
	for _, entry := range traceEntries {
		entryResult := comm.TestEntryResult{Name: entry.Name}
		replayValues, err := runTest(&config, &entry)
		if err != nil {
			entryResult.Result = comm.TestResultFailure
			entryResult.Message = err.String()
		} else {
			entryResult.Result = comm.TestResultSuccess
			entryResult.Values = *replayValues
			passedCount++
		}
		result.Entries = append(result.Entries, entryResult)
	}

	if len(traceEntries) == passedCount {
		result.Result = comm.TestResultSuccess
	} else {
		result.Result = comm.TestResultFailure
		result.Message = fmt.Sprintf("%d/%d tests passed", passedCount, len(traceEntries))
	}

	outputResult(result)
}
