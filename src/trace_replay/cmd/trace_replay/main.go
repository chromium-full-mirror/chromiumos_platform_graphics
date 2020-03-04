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
)

const (
	defaultRepeatCount = 1
	apitraceAppName    = "apitrace"
	apitraceOutputRE   = `Rendered (\d+) frames in (\d*\.?\d*) secs, average of (\d*\.?\d*) fps`
)

var (
	apitraceArgs     = []string{"replay", "--benchmark"}
	requiredPackages = []string{"apitrace", "zstd"}
)

// ProxyServerInfo is used as a container for a proxy sever information
type ProxyServerInfo struct {
	URL string `json:"url"`
}

// FileInfo is used to specify trace replay test storage artifact info
type FileInfo struct {
	GSURL     string `json:"gsURL"`
	Size      uint64 `json:"size,string"`
	SHA256Sum string `json:"sha256sum"`
	MD5Sum    string `json:"md5sum"`
}

// TestSettings is used to configure trace replay test settings
type TestSettings struct {
	RepeatCount    uint32 `json:"RepeatCount,string"`
	CoolDownIntSec uint32 `json:"CoolDownIntSec,string"`
}

// Config is used to define trace replay config entries
type Config struct {
	Name         string          `json:"Name"`
	ProxyServer  ProxyServerInfo `json:"ProxyServer"`
	StorageFile  FileInfo        `json:"StorageFile"`
	TestSettings TestSettings    `json:"TestSettings"`
}

type replayResult struct {
	TotalFrames       uint32  `json:"TotalFrames,string"`
	AverageFPS        float32 `json:"AverageFPS,string"`
	DurationInSeconds float32 `json:"DurationInSeconds,string"`
}

type testResult struct {
	Result       string         `json:"Result"`
	ErrorMessage string         `json:"ErrorMessage"`
	Values       []replayResult `json:"Values"`
}

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
	if err := decompressCmd.Run(); err != nil {
		return "", wrapError(err, "Unable to decompress <%s>.", fileName)
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

// Downloads a file using gs url [gsURL] via proxy http server [proxyURL]
// and saves it to the specified directory [localPath]
// returns the full name to the result file or error
func downloadFile(localPath, proxyURL, gsURL string) (string, *werror) {
	downloadURL, e := url.Parse(proxyURL)
	if e != nil {
		return "", wrapError(e, "Unable to parse proxy server url <%s>", proxyURL)
	}
	downloadURLParams := url.Values{}
	// Use only one URL argument for now: d={$gsURL}
	downloadURLParams.Add("d", gsURL)
	downloadURL.RawQuery = downloadURLParams.Encode()
	localFileName := path.Join(localPath, path.Base(gsURL))
	err := httpRequestToFile(downloadURL.String(), localFileName)
	if err != nil {
		return "", err
	}
	return localFileName, nil
}

func parseReplayOutput(output string) (*replayResult, *werror) {
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
	return &replayResult{uint32(totalFrames), float32(averageFPS), float32(durationInSeconds)}, nil
}

func replayTrace(traceFileName string) (*replayResult, *werror) {
	cmd := exec.Command(apitraceAppName, append(apitraceArgs, traceFileName)...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return nil, wrapError(err, "Failed to replay trace file [%s]", traceFileName)
	}
	return parseReplayOutput(string(out))
}

func outputResult(replayResults []replayResult, err *werror) {
	if err == nil && len(replayResults) == 0 {
		err = wrapError(nil, "no replay results")
	}

	resultValue := func(err *werror) string {
		if err != nil {
			return "error"
		}
		return "ok"
	}

	errorMessage := func(err *werror) string {
		if err != nil {
			return err.String()
		}
		return ""
	}

	testResult := testResult{
		Result:       resultValue(err),
		ErrorMessage: errorMessage(err),
		Values:       replayResults,
	}

	output, _ := json.Marshal(testResult)
	fmt.Println(string(output))
}

func exitWithError(err *werror) {
	outputResult([]replayResult{}, err)
	os.Exit(0)
}

func checkPackageInstalled(name string) *werror {
	if exitCode, _, stderr := runCommand("dpkg", "-l", name); exitCode != 0 {
		return wrapError(fmt.Errorf("%s", stderr), "dpkg for %s failed with exit code %d!", name, exitCode)
	}
	return nil
}

func main() {
	// Check arguments and unmarshall config json
	if len(os.Args) != 2 {
		exitWithError(wrapError(nil, "Invalid command line arguments count.\nUsage: cros_retrace <config_json>\n"))
	}
	var config Config
	e := json.Unmarshal([]byte(os.Args[1]), &config)
	if e != nil {
		exitWithError(wrapError(nil, "Unable to parse json <%s>: [%s]", os.Args[1], e.Error()))
	}
	// Validate the test config
	if config.ProxyServer.URL == "" {
		exitWithError(wrapError(nil, "Proxy server isn't specified"))
	}

	if config.StorageFile.GSURL == "" {
		exitWithError(wrapError(nil, "GS url for the storage file isn't specified"))
	}

	if config.StorageFile.MD5Sum == "" {
		exitWithError(wrapError(nil, "MD5 checksum for the storage file isn't specified"))
	}

	if config.TestSettings.RepeatCount == 0 {
		config.TestSettings.RepeatCount = defaultRepeatCount
	}

	// Check prerequisites (apitrace, bz2, etc)
	for _, pkgName := range requiredPackages {
		if err := checkPackageInstalled(pkgName); err != nil {
			exitWithError(err)
		}
	}

	// TODO(tutankhamen): check if trace file is already exist in the local cache

	// Download trace file via proxy server
	downloadedFileName, err := downloadFile("/tmp/", config.ProxyServer.URL, config.StorageFile.GSURL)
	if err != nil {
		exitWithError(err)
	}

	// Perform integrity checks on the downloaded file
	fileInfo, e := os.Stat(downloadedFileName)
	if e != nil {
		exitWithError(wrapError(e, "Unable to get stat for %s", downloadedFileName))
	}

	if uint64(fileInfo.Size()) != config.StorageFile.Size {
		exitWithError(wrapError(nil, "Actual file size %db is different from the value in metadata %db", fileInfo.Size(), config.StorageFile.Size))
	}

	downloadedFileMD5Sum, e := getFileMD5Sum(downloadedFileName)
	if e != nil {
		exitWithError(wrapError(e, "Unable to calculate MD5 checksum for %s", downloadedFileName))
	}
	if downloadedFileMD5Sum != config.StorageFile.MD5Sum {
		exitWithError(wrapError(nil, "Actual file MD5 checksum %s is different from the value in metadata %s", downloadedFileMD5Sum, config.StorageFile.MD5Sum))
	}

	traceFileName, err := decompressFile(downloadedFileName, ".trace")
	if err != nil {
		exitWithError(err)
	}

	defer os.Remove(traceFileName)

	// TODO: save the trace file with meta information to the local cache

	// Runs apitrace 'arg_repeat_count' times.
	var replayResults []replayResult
	for it := uint32(0); it < config.TestSettings.RepeatCount; it++ {
		res, err := replayTrace(traceFileName)
		if err != nil {
			exitWithError(err)
		}
		replayResults = append(replayResults, *res)
	}

	outputResult(replayResults, nil)
}
