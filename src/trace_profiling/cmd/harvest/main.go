// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"html"
	"io"
	"io/ioutil"
	"math"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"trace_profiling/cmd/profile/remote"
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

// GameInfo is a JSON file bundled with the traces that has info about the game
// the trace was generated from. We only use two of the fields here.
type GameInfo struct {
	GameName string `json:"game_name"`
	GameID   string `json:"gameid"`
}

// TraceInfo is also a JSON associated with some traces. We care about the MD5
// field for the uncompressed trace file.
type TraceInfo struct {
	TraceFileMD5 string `json:"trace_file_md5"`
}

// FPS output for performance comparison.
type fpsRecord struct {
	fpsError        error
	traceName       string
	crostiniFps     float64
	croutonFps      float64
	crostiniPercent float64
}

// Cmd-line arguments.
var argVerbose bool
var argOutputFile string
var argConfigFilepath string
var argEnableCompareFps bool
var argSupressCrostini bool
var argSupressCrouton bool
var argDeleteArchiveCrumbs bool

var fpsData []fpsRecord

// Trace names often have characters that are not suitable for file paths. This
// string replacer is used to sanitize them.
var filenameSanitizer = strings.NewReplacer(
	"&", "And", " ", "_", ";", "-", "/", "", "\\", "", "?", "-", "'", "", "<", "Lt", ">", "Gt")

// Return whether file with path filepath exists.
func fileExists(filename string) bool {
	info, err := os.Stat(filename)
	if err != nil {
		return false
	}

	return !info.IsDir()
}

// Fetch a trace archive from Google storage.
func fetchTraceFromStorage(cacheDir, trace string) (string, error) {
	_, filename := path.Split(trace)
	filepath := path.Join(cacheDir, filename)
	if !fileExists(filepath) {
		_, err := remote.FetchFromGS(filepath, trace)
		if err != nil {
			return "", err
		}
	}

	return filepath, nil
}

// Read JSON data from a file and return as a byte array.
func readJSONData(jsonFilepath string) ([]byte, error) {
	file, err := os.Open(jsonFilepath)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot open JSON file <%s>; error=%w",
			jsonFilepath, err)
	}
	defer file.Close()

	jsonData, err := ioutil.ReadAll(file)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot read JSON file <%s>; error=%w",
			jsonFilepath, err)
	}

	return jsonData, nil
}

// Split a filename from all its extensions and returns as pair (name, extensions).
// E.g. filename "blahblah.txt.tar" produces "filename" and ".txt.tar".
func splitFileExt(filename string) (string, string) {
	ext := ""
	for {
		e := filepath.Ext(filename)
		if e == "" {
			break
		}
		ext = e + ext
		filename = strings.TrimSuffix(filename, e)
	}

	return filename, ext
}

// Extract and archive and return the path to the root folder or file of the
// extracted data. This function uses the file name, minus the extension, of
// the archive as name for the root folder for tar and tar.bz2 archives.
func extractArchive(fileName string) (string, error) {
	var decompressCmd *exec.Cmd
	dirPath, fileExt := splitFileExt(fileName)

	switch fileExt {
	case ".tar.bz2":
		os.Mkdir(dirPath, os.ModePerm)
		decompressCmd = exec.Command("tar", "-xjf", fileName, "-C", dirPath)
	case ".tar":
		os.Mkdir(dirPath, os.ModePerm)
		decompressCmd = exec.Command("tar", "-xf", fileName, "-C", dirPath)
	case ".bz2":
		decompressCmd = exec.Command("bunzip2", "-f", "-k", "-d", fileName)
	case ".zst", ".xz":
		decompressCmd = exec.Command("zstd", "-d", "-f", "--rm", "-T0", fileName)
	default:
		return "", fmt.Errorf("Error: unknown trace extension: %s", fileExt)
	}
	if err := decompressCmd.Run(); err != nil {
		return "", fmt.Errorf("Error: unable to decompress <%s>, err=%s", fileName, err.Error())
	}

	return dirPath, nil
}

// Look for a file with zst extension in the given directory. If found, uncompress
// the zst archive into file "game.trace" within the same directory. Returns
// file name "game.trace" when successful. If there is no zst archive, this
// function doesn't fail, but return the empty string "" instead.
func extractGameTraceFromZst(dirPath string) (string, error) {
	files, err := ioutil.ReadDir(dirPath)
	if err != nil {
		return "", nil
	}

	outTrace := path.Join(dirPath, "game.trace")
	for _, file := range files {
		filename := path.Join(dirPath, file.Name())
		if filepath.Ext(filename) == ".zst" {
			printIfVerbose("Extracting %s to game.trace\n", filename)
			cmd := exec.Command("zstd", "-df", filename, "-o", outTrace)
			err = cmd.Run()
			if err != nil {
				return "", err
			}
			return "game.trace", nil
		}
	}

	return "", nil
}

// Some trace archives extract directly to files within the destination folder,
// while other extract to folder within the folder. This function regularize
// the extracted archives so that all files of interest are directly within
// the root folder.
func regularizeTraceDir(dirPath string) error {
	fileInfo, err := ioutil.ReadDir(dirPath)
	if err != nil {
		return err
	}

	if len(fileInfo) == 1 && fileInfo[0].IsDir() {
		nestedDir := path.Join(dirPath, fileInfo[0].Name())
		nestedFiles, _ := ioutil.ReadDir(nestedDir)
		for _, f := range nestedFiles {
			os.Rename(path.Join(nestedDir, f.Name()), path.Join(dirPath, f.Name()))
		}
	}

	return nil
}

// Fetch the GameInfo data from game_info.json in the given dirPath.
func fetchGameInfoFromDir(dirPath string) (GameInfo, error) {
	jsonFilepath := path.Join(dirPath, "game_info.json")
	jsonData, err := readJSONData(jsonFilepath)
	if err != nil {
		return GameInfo{}, err
	}

	var gameInfo GameInfo
	err = json.Unmarshal(jsonData, &gameInfo)
	if err != nil {
		return GameInfo{}, fmt.Errorf("Error: unable to parse JSON file <%s>; error=%w",
			jsonFilepath, err)
	}

	return gameInfo, nil
}

// Fetch the TraceInfo data from trace_info.json in the given dirPath.
func fetchTraceInfoFromDir(dirPath string) (TraceInfo, error) {
	// Not all game archives have trace info.
	jsonFilepath := path.Join(dirPath, "trace_info.json")
	if !fileExists(jsonFilepath) {
		return TraceInfo{}, fmt.Errorf("No trace info")
	}

	jsonData, err := readJSONData(jsonFilepath)
	if err != nil {
		return TraceInfo{}, err
	}

	var traceInfo TraceInfo
	err = json.Unmarshal(jsonData, &traceInfo)
	if err != nil {
		return TraceInfo{}, fmt.Errorf("Error: unable to parse JSON file <%s>; error=%w",
			jsonFilepath, err)
	}

	return traceInfo, nil
}

// Return the MD5 hash from file <filename>.
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

// Attempt to the verify the trace file MD5 hash. The reference MD5 hash is read
// from property "trace_file_md5" from file trace_info.json in the game dir.
// Either may not be present, in which case this function will not fail. If the
// reference MD5 hash is found, then it must match the MD5 hash calculated from
// the trace file.
func verifyTraceMD5(gameDirPath, traceFilename string) error {
	traceInfo, err := fetchTraceInfoFromDir(gameDirPath)
	if err != nil {
		if err.Error() == "No trace info" {
			printIfVerbose("Skip MD5 check for %s: no trace_info.json\n", traceFilename)
			return nil
		}
		return err
	}

	// Not all trace-info have the MD5 property.
	if traceInfo.TraceFileMD5 == "" {
		printIfVerbose("Skip MD5 check for %s: no MD5 property in trace_info.json\n", traceFilename)
		return nil
	}

	printIfVerbose("Verifying MD5 hash for %s\n", traceFilename)
	traceFile := path.Join(gameDirPath, traceFilename)
	md5, err := getFileMD5Sum(traceFile)
	if err != nil {
		return err
	}

	if md5 != traceInfo.TraceFileMD5 {
		return fmt.Errorf("md5 verification failed for %s", traceFilename)
	}

	return nil
}

// Given a directory path to a folder containing data extracted from a game
// archive, construct and return a trace filename. The filename is derived from
// the game name, itself extracted from game_info.josn, sanitized to remove
// characters that are not suitable for file names.
func getTraceFileNameFromGameDir(gameDir string) (string, error) {
	gameInfo, err := fetchGameInfoFromDir(gameDir)
	if err != nil {
		return "", err
	}

	filename := html.UnescapeString(gameInfo.GameName)
	filename = filenameSanitizer.Replace(filename) + "-" + gameInfo.GameID

	return filename, nil
}

// Extract a trace, as a file, from the archive with the given file path.
// Return the file path to the trace as a string.
func extractTraceFromArchive(archivePath string) (string, error) {
	// Dir where traces and game archives are stored.
	cacheDir, _ := path.Split(archivePath)

	// Extract data from the archive.
	printIfVerbose("Extracting archive %s\n", archivePath)
	gameDirPath, err := extractArchive(archivePath)
	if err != nil {
		return "", err
	}

	err = regularizeTraceDir(gameDirPath)
	if err != nil {
		return "", err
	}

	// Get a suitable name for the trace file.
	traceFilename, err := getTraceFileNameFromGameDir(gameDirPath)
	if err != nil {
		return "", err
	}

	// Some traces are compressed as zst inside the archive folder. If such is the
	// case, this function will extract the zst archive and leave the file in
	// game.trace.
	traceName, err := extractGameTraceFromZst(gameDirPath)
	if err != nil {
		return "", err
	}
	if traceName == "" {
		traceName = "game.trace"
	}

	err = verifyTraceMD5(gameDirPath, traceName)
	if err != nil {
		return "", err
	}

	// Finally, copy trace file to it's final path and name.
	dstTrace := path.Join(cacheDir, traceFilename) + ".trace"
	srcTrace := path.Join(gameDirPath, traceName)
	err = os.Rename(srcTrace, dstTrace)
	printIfVerbose("Trace ready as %s\n", dstTrace)

	if argDeleteArchiveCrumbs {
		os.RemoveAll(gameDirPath)
	}

	return dstTrace, err
}

// Retrieve and return, as a path, the trace file from a game archive.
func getTraceFileFromArchive(compressedFilepath string) (string, error) {
	fileExt := filepath.Ext(compressedFilepath)
	switch fileExt {
	case ".bz2", ".tar":
		return extractTraceFromArchive(compressedFilepath)
	default:
		return "", fmt.Errorf("Error: unknown compressed file: %s", fileExt)
	}
}

// Customize a profiler bundle with the given trace file path.
func customizeBundle(bundle, traceFile string) (string, error) {
	dir, filename := path.Split(traceFile)

	customBundle, err := ioutil.TempFile("", "bundle")
	if err != nil {
		return "", err
	}

	bundleFile, err := os.Open(bundle)
	if err != nil {
		os.Remove(customBundle.Name())
		return "", err
	}
	defer bundleFile.Close()

	datawriter := bufio.NewWriter(customBundle)
	scanner := bufio.NewScanner(bundleFile)
	for scanner.Scan() {
		str := strings.Replace(scanner.Text(), "<<trace-file>>", filename, 1)
		str = strings.Replace(str, "<<trace-dir>>", dir, 1)
		datawriter.WriteString(str + "\n")
	}

	datawriter.Flush()
	customBundle.Close()

	return customBundle.Name(), nil
}

// Profile a trace file on a given traget, specified as a bundle, using the
// companion proifling app specified in profilerBinPath.
func runProfile(profilerBinPath, bundleFile, traceFile string) (string, error) {
	// The bundle is actually a template that we must customize with the proper
	// trace file name.
	newBundle, err := customizeBundle(bundleFile, traceFile)
	if err != nil {
		return "", err
	}
	defer os.Remove(newBundle)

	arg := fmt.Sprintf("-config-bundle=%s", newBundle)
	cmd := exec.Command(profilerBinPath, arg)
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
		return "", fmt.Errorf("profling failed; err = %s", result)
	}

	return prof, nil
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

// Print FPS comparative info for Crostini v.s. Crouton into the output file.
func gatherProfileResult(traceName, crostiniFile, croutonFile string) {
	if crostiniFile != "" && croutonFile != "" {
		var err error
		fpsCrostini, e := getFpsFromProfile(crostiniFile)
		if e != nil {
			err = e
		}
		fpsCrouton, e := getFpsFromProfile(croutonFile)
		if e != nil {
			err = e
		}

		percent := math.Inf(1) // Positive infinity
		if fpsCrouton > 0.001 {
			percent = 100.0 * fpsCrostini / fpsCrouton
		}
		fpsData = append(fpsData, fpsRecord{
			fpsError:        err,
			traceName:       traceName,
			crostiniFps:     fpsCrostini,
			croutonFps:      fpsCrouton,
			crostiniPercent: percent,
		})
	}
}

// Generate the FPS comparative output to the target output file. Note that
// data is always added to the file.
func generateOutput(data []fpsRecord) {
	outFile, err := os.OpenFile(argOutputFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err == nil {
		defer outFile.Close()

		fileInfo, _ := outFile.Stat()
		dataWriter := bufio.NewWriter(outFile)

		sort.SliceStable(fpsData, func(i, j int) bool {
			return fpsData[i].crostiniPercent < fpsData[j].crostiniPercent
		})

		// Add header, but only to new files.
		if fileInfo.Size() == 0 {
			dataWriter.WriteString(fmt.Sprintf(
				"%32s  %8s  %8s      %%\n", "Trace name", "Crostini", "Crouton"))
		}

		for _, fps := range fpsData {
			if fps.fpsError != nil {
				dataWriter.WriteString(fmt.Sprintf("%32s error getting FPS: %s", fps.traceName, fps.fpsError))
			} else {
				dataWriter.WriteString(fmt.Sprintf("%32s,  %8.2f,  %8.2f,", fps.traceName, fps.crostiniFps, fps.croutonFps))
				if fps.crostiniPercent != math.Inf(1) {
					dataWriter.WriteString(fmt.Sprintf("  %8.2f%%\n", fps.crostiniPercent))
				} else {
					dataWriter.WriteString("     INF!\n")
				}
			}
		}
		dataWriter.Flush()
	}
}

// Profile traces coming in queue feedQueue onto the target specified in
// targetBundle and feed the resulting profile file paths to resultQueue.
func profileTracesOnTarget(
	feedQueue chan string,
	targetName string,
	profileAppBinPath, targetBundle string,
	resultQueue chan string) {
	for trace := range feedQueue {
		printIfVerbose("Tracing on %s with %s\n", targetName, trace)
		profFile, err := runProfile(profileAppBinPath, targetBundle, trace)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error profiling on %s: %s\n", targetName, err.Error())
			resultQueue <- ""
		} else {
			resultQueue <- profFile
		}
	}
}

// Run the performance comparison with the given configuration.
func runPerfComparison(config *PerfConfig) error {
	// Create array that will receive FPS data.
	fpsData = make([]fpsRecord, 0, len(config.Traces))

	// Launch goroutine to download traces for G-Storage. Once a trace file is
	// ready it is queued into traceQueue. Trace files available locally are
	// simply queued as-is.
	var traceQueue = make(chan string)
	go func() {
		for _, trace := range config.Traces {
			if remote.IsGoogleStorageURI(trace) {
				localTrace, err := fetchTraceFromStorage(config.TraceCacheDir, trace)
				if err != nil {
					// Print an error and keep going with the next trace.
					fmt.Fprintf(os.Stderr, "Error downloading trace <%s>:\n  err=%s\n", trace, err.Error())
				} else {
					traceQueue <- localTrace
				}
			} else {
				traceQueue <- trace
			}
		}
		close(traceQueue)
	}()

	// Feed queues are used to feed trace-file data to profilers for crostini
	// and crouton, which run in parallel. Conversely, result queues are used
	// to receive data from these profilers.
	var crostFeedQueue = make(chan string)
	var croutFeedQueue = make(chan string)
	var crostResultQueue = make(chan string)
	var croutResultQueue = make(chan string)

	// Run goroutines to profile on Crostini and Crouton in parallel.
	go profileTracesOnTarget(crostFeedQueue, "Crostini", config.ProfileBinPath,
		config.CrostiniBundle, crostResultQueue)
	go profileTracesOnTarget(croutFeedQueue, "Crouton", config.ProfileBinPath,
		config.CroutonBundle, croutResultQueue)

	// Grab trace files as they become available and feed them to the profile
	// goroutines above.
	for trace := range traceQueue {
		ext := filepath.Ext(trace)
		if ext == ".bz2" || ext == ".tar" {
			traceFile, err := getTraceFileFromArchive(trace)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error extracting trace from archive %s, err=%s\n",
					trace, err.Error())
				continue
			}

			trace = traceFile
		}

		if !argSupressCrostini {
			crostFeedQueue <- trace
		}
		if !argSupressCrouton {
			croutFeedQueue <- trace
		}

		// Short (hacky) pause to give the profile goroutines a chance to grab the
		// traces and print their verbose output.
		time.Sleep(5 * time.Millisecond)
		printIfVerbose("Waiting for profiling to complete... \n")

		// Wait for result from profilers.
		var crostProfile = ""
		var croutProfile = ""
		if !argSupressCrostini {
			crostProfile = <-crostResultQueue
		}
		if !argSupressCrouton {
			croutProfile = <-croutResultQueue
		}

		if argEnableCompareFps {
			_, traceName := path.Split(trace)
			gatherProfileResult(traceName, crostProfile, croutProfile)
		} else {
			if crostProfile != "" {
				printIfVerbose("Crostini profile ready in: %s\n", crostProfile)
			}
			if croutProfile != "" {
				printIfVerbose("Crouton profile ready in: %s\n", croutProfile)
			}
		}
	}

	close(crostFeedQueue)
	close(croutFeedQueue)

	if argEnableCompareFps {
		generateOutput(fpsData)
	}

	return nil
}

// Read and return the perf configuration from a JSON file.
func readPerfConfigFromFile(jsonFilepath string) (*PerfConfig, error) {
	jsonData, err := readJSONData(jsonFilepath)
	if err != nil {
		return nil, err
	}

	var config = PerfConfig{
		TraceCacheDir:     defaultTraceCacheDir,
		KeepTracesInCache: false,
		ProfileBinPath:    defaultProfileBinPath,
		CrostiniBundle:    defaultCrostiniBundle,
		CroutonBundle:     defaultCroutonBundle}
	err = json.Unmarshal(jsonData, &config)
	if err != nil {
		return nil, fmt.Errorf("unable to parse JSON file <%s>; error=%w",
			jsonFilepath, err)
	}

	return &config, err
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
	flag.BoolVar(&argSupressCrostini, "no-crostini", false, "Supress profiling on crostini")
	flag.BoolVar(&argSupressCrouton, "no-crouton", false, "Supress profiling on crouton")
	flag.BoolVar(&argDeleteArchiveCrumbs, "del-archive-crumbs", false, "Delete files and folders left after unarchiving game data")
	flag.Parse()

	// We can only compare fps if we have both crostini and crouton data.
	argEnableCompareFps = argEnableCompareFps && !(argSupressCrostini || argSupressCrouton)

	config, err := readPerfConfigFromFile(argConfigFilepath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return
	}

	err = runPerfComparison(config)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
	}
}
