// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"encoding/json"
	"fmt"
	"html"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
	"time"
)

// GameInfo is a JSON file bundled with the traces that has info about the game
// the trace was generated from. We only use some of the fields here.
type GameInfo struct {
	GameName   string `json:"game_name"`
	GameID     string `json:"gameid"`
	ReportDate string `json:"report_date"`
	Version    string `json:"version"`
}

// TraceInfo is also another JSON file associated with some traces.
type TraceInfo struct {
	TraceFileMD5     string      `json:"trace_file_md5"`
	FileSize         int64       `json:"file_size"`
	FileTime         time.Time   `json:"file_time"`
	ReportVersion    string      `json:"report_version"`
	TraceFileVersion json.Number `json:"trace_file_version"`
	TraceFramesCount json.Number `json:"trace_frames_count"`
}

// TraceRecord encapsulates various data items associated with traces, including
// JSON files with extra info, paths to files and directories for the archives,
// trace files and data extracted from the archives, etc. TraceRecords also
// provides support to derive that data from trace archives.
type TraceRecord struct {
	traceInfo *TraceInfo
	gameInfo  *GameInfo

	traceID           string // ID unique to each trace or game report.
	traceDataFilePath string // File path to trace file or game-report archive.

	workingDir      string // Dir where traces and original archives are stored.
	traceDir        string // Dir where data extracted from archive is stored.
	traceFilename   string // Name of trace file after extraction from archive.
	archiveFilename string // Name of original trace archive.
	isFromLocalFile bool   // Whether the trace data came from a local source.
	isFromArchive   bool   // Whether the trace data was extracted from an archive.

	verbose bool
}

// Trace file names are derived from game names found in the GameInfo file.
// However, the names often have characters that are not suitable for file paths.
// This string replacer is used to sanitize them. (The first column has the
// original characters, the second column is what are replaced with.)
var filenameSanitizer = strings.NewReplacer(
	"&", "And",
	" ", "_",
	";", "-",
	"/", "",
	"\\", "",
	"?", "-",
	"'", "",
	"<", "Lt",
	">", "Gt")

// CreateTraceRecord creates and returns a TraceRecord object. File path
// traceDataFilePath points to either to an archive of type ".tar", ".bz2" or
// ".tar.bz2" or a trace file of type ".trace". Archives may contain related
// trace and game info in JSON files.
func CreateTraceRecord(
	traceID string, traceDataFilePath string, fromLocalFile, verbose bool) *TraceRecord {

	return &TraceRecord{
		traceID:           traceID,
		traceDataFilePath: traceDataFilePath,
		isFromLocalFile:   fromLocalFile,
		isFromArchive:     true,
		verbose:           verbose,
	}
}

// LoadFromTraceData load a trace and related info from the trace-data file
// given to CreateTraceRecord.
func (tb *TraceRecord) LoadFromTraceData() error {
	ext := filepath.Ext(tb.traceDataFilePath)
	if ext == ".bz2" || ext == ".tar" {
		if err := tb.extractTraceDataFromArchive(tb.traceDataFilePath); err != nil {
			return err
		}
	} else if ext == ".trace" {
		tb.workingDir, tb.traceFilename = path.Split(tb.traceDataFilePath)
		tb.archiveFilename = tb.traceFilename
		tb.isFromArchive = false
	} else {
		return fmt.Errorf("trace file does not exist: %s", tb.traceDataFilePath)
	}

	return nil
}

// IsFromLocalFile returns whether the trace data was obtained from a local file.
func (tb *TraceRecord) IsFromLocalFile() bool { return tb.isFromLocalFile }

// IsFromArchive returns whether the trace data was extracted from an archive.
func (tb *TraceRecord) IsFromArchive() bool { return tb.isFromArchive }

// GetTraceID returns the trace ID.
func (tb *TraceRecord) GetTraceID() string {
	return tb.traceID
}

// GetLocalTraceDataPath returns the path to the local file with the original
// trace data.
func (tb *TraceRecord) GetLocalTraceDataPath() string {
	return tb.traceDataFilePath
}

// GetTraceFilePath returns the full path of the trace file.
func (tb *TraceRecord) GetTraceFilePath() string {
	return path.Join(tb.workingDir, tb.traceFilename)
}

// GetTraceInfo returns the TraceInfo record or nil if not available.
func (tb *TraceRecord) GetTraceInfo() *TraceInfo {
	return tb.traceInfo
}

// GetGameInfo returns the GameInfo record or nil if not available.
func (tb *TraceRecord) GetGameInfo() *GameInfo {
	return tb.gameInfo
}

// DeleteArchive deletes the archive file this TraceRecord was derived from.
func (tb *TraceRecord) DeleteArchive() error {
	if tb.archiveFilename != "" {
		filePath := path.Join(tb.workingDir, tb.archiveFilename)
		tb.archiveFilename = ""
		return os.Remove(filePath)
	}

	return nil
}

// DeleteTraceDir deletes the trace directory and the content that was extracted
// from the trace archive.
func (tb *TraceRecord) DeleteTraceDir() error {
	if tb.traceDir != "" {
		err := os.RemoveAll(tb.traceDir)
		tb.traceDir = ""
		return err
	}

	return nil
}

// DeleteTraceFile deletes the trace file.
func (tb *TraceRecord) DeleteTraceFile() error {
	if tb.traceFilename != "" {
		traceFilepath := path.Join(tb.workingDir, tb.traceFilename)
		tb.traceFilename = ""
		return os.Remove(traceFilepath)
	}

	return nil
}

// Clear the TraceRecord instance so that it may be re-used for another trace.
func (tb *TraceRecord) clear() {
	*tb = TraceRecord{
		verbose: tb.verbose,
	}
}

// If verbose mode is enabled, print the formatted string.
func (tb *TraceRecord) printIfVerbose(format string, a ...interface{}) {
	if tb.verbose {
		fmt.Printf(format, a...)
	}
}

// Extract trace data from a trace archive, including the related info contained
// in JSON files if available. The trace data is extracted within a dir with the
// same name as the archive and stored in the working dir, where the trace archive
// is found.
func (tb *TraceRecord) extractTraceDataFromArchive(archive string) error {
	// Make the dir where the archive is stored the working dir.
	tb.workingDir, _ = path.Split(archive)

	// Extract data from the archive.
	tb.printIfVerbose("Extracting archive %s\n", archive)
	extractedDir, err := tb.uncompressArchive(archive)
	if err != nil {
		return err
	}
	tb.traceDir = extractedDir

	// Special case with empty dir means that archive extracted directly to a trace file.
	if extractedDir == "" {
		return nil
	}

	tb.printIfVerbose("Fetching game info from %s\n", tb.traceDir)
	if err := tb.fetchGameInfo(tb.traceDir); err != nil {
		return err
	}
	tb.printIfVerbose("Fetching trace info from %s\n", tb.traceDir)
	if err := tb.fetchTraceInfo(tb.traceDir); err != nil {
		return err
	}

	// Some traces are compressed as zst inside the archive folder. If such is the
	// case, this function will extract the zst archive and leave the file in
	// game.trace.
	traceFile, err := tb.extractTraceFile()
	if err != nil {
		return err
	}

	if err := tb.verifyTraceMD5Hash(traceFile); err != nil {
		return err
	}

	// Finally, copy trace file to it's final path and name.
	tb.traceFilename = tb.inferTraceFileName()
	dstTrace := path.Join(tb.workingDir, tb.traceFilename)
	srcTrace := path.Join(tb.traceDir, traceFile)
	err = os.Rename(srcTrace, dstTrace)
	tb.printIfVerbose("Trace ready as %s\n", dstTrace)

	return err

}

// Extract and archive and return the path to the root folder or file of the
// extracted data. This function uses the file name of the archive, minus the
// extension, as name for the root folder for tar and tar.bz2 archives. In the
// special case where the archive extracts directly to a trace filer, this
// function return an empty dir and no error, with the name of the trace file
// stored in tb.traceFilename.
func (tb *TraceRecord) uncompressArchive(archive string) (string, error) {
	var uncompressCmd *exec.Cmd
	var extractedToTrace = false
	dirPath, fileExt := SplitFileExt(archive)

	switch fileExt {
	case ".tar.bz2":
		os.Mkdir(dirPath, os.ModePerm)
		uncompressCmd = exec.Command("tar", "-xjf", archive, "-C", dirPath)
	case ".tar":
		os.Mkdir(dirPath, os.ModePerm)
		uncompressCmd = exec.Command("tar", "-xf", archive, "-C", dirPath)
	case ".bz2":
		uncompressCmd = exec.Command("bunzip2", "-f", "-k", "-d", archive)
	case ".trace.bz2":
		uncompressCmd = exec.Command("bunzip2", "-f", "-k", "-d", archive)
		extractedToTrace = true
	case ".zst", ".xz":
		uncompressCmd = exec.Command("zstd", "-d", "-f", "-T0", archive)
	default:
		return "", fmt.Errorf("unknown trace type: %s", fileExt)
	}
	if err := uncompressCmd.Run(); err != nil {
		return "", fmt.Errorf("unable to decompress <%s>, err=%s", archive, err.Error())
	}

	var err error
	if extractedToTrace {
		traceFilePath := dirPath + ".trace"
		tb.traceFilename = path.Base(traceFilePath)
		dirPath = ""
		if !FileExists(traceFilePath) {
			err = fmt.Errorf("failed to extract trace from %s", archive)
		}
	} else {
		err = tb.regularizeExtractedDir(dirPath)
	}
	return dirPath, err
}

// Some trace archives extract directly to files within the destination folder,
// while other extract to folder within the folder. This function regularize
// the extracted archives so that all files of interest are directly within
// the destination folder.
func (tb *TraceRecord) regularizeExtractedDir(dirPath string) error {
	fileInfo, err := ioutil.ReadDir(dirPath)
	if err != nil {
		return err
	}

	// If the extracted dir contains a single file and that file is another dir,
	// we need to copy the files within that nested dir one level up.
	if len(fileInfo) == 1 && fileInfo[0].IsDir() {
		nestedDir := path.Join(dirPath, fileInfo[0].Name())
		nestedFiles, _ := ioutil.ReadDir(nestedDir)
		for _, f := range nestedFiles {
			os.Rename(path.Join(nestedDir, f.Name()), path.Join(dirPath, f.Name()))
		}

		// Delete the nested dir.
		os.RemoveAll(nestedDir)
	}

	return nil
}

// Fetch the GameInfo data from game_info.json in the given dirPath.
func (tb *TraceRecord) fetchGameInfo(dirPath string) error {
	tb.gameInfo = nil

	gameInfoFilepath := path.Join(dirPath, "game_info.json")
	jsonData, err := ReadJSONData(gameInfoFilepath)
	if err != nil {
		return err
	}

	var gameInfo GameInfo
	err = json.Unmarshal(jsonData, &gameInfo)
	if err != nil {
		return fmt.Errorf("could not parse JSON file <%s>; error=%w", gameInfoFilepath, err)
	}

	tb.gameInfo = &gameInfo
	return nil
}

// Fetch the TraceInfo data from trace_info.json in the given dirPath.
func (tb *TraceRecord) fetchTraceInfo(dirPath string) error {
	tb.traceInfo = nil

	// Not all game archives have trace info.
	jsonFilepath := path.Join(dirPath, "trace_info.json")
	if !FileExists(jsonFilepath) {
		return nil
	}

	jsonData, err := ReadJSONData(jsonFilepath)
	if err != nil {
		return err
	}

	var traceInfo TraceInfo
	err = json.Unmarshal(jsonData, &traceInfo)
	if err != nil {
		return fmt.Errorf("could not parse JSON file <%s>; error=%w", jsonFilepath, err)
	}

	tb.traceInfo = &traceInfo
	return nil
}

// Extract the trace file from the current trace Dir. This function expects the
// trace to be store either as a trace file with name game.trace or as a
// zst-compressed file with extension .zst". In the later case, the file is
// uncompressed and stored as game.trace. If neither of these files is found,
// the functions returns nil.
func (tb *TraceRecord) extractTraceFile() (string, error) {
	files, err := ioutil.ReadDir(tb.traceDir)
	if err != nil {
		return "", err
	}

	outTrace := path.Join(tb.traceDir, "game.trace")
	for _, file := range files {
		filename := path.Join(tb.traceDir, file.Name())
		if filepath.Ext(filename) == ".zst" {
			tb.printIfVerbose("Extracting %s to game.trace\n", filename)
			cmd := exec.Command("zstd", "-df", filename, "-o", outTrace)
			err = cmd.Run()
			if err != nil {
				return "", err
			}
			return "game.trace", nil
		}
	}

	if FileExists(path.Join(tb.traceDir, "game.trace")) {
		return "game.trace", nil
	}

	return "", fmt.Errorf("no trace file found in %s", tb.traceDir)
}

// Compose and return a file name for the local trace file.
func (tb *TraceRecord) inferTraceFileName() string {
	// If a trace ID is available, use it for file name since it is conveniently unique.
	if tb.traceID != "" {
		return tb.traceID + ".trace"
	}

	// Make up a file name from the game name and game ID, both extracted from
	// game_info.json. The game name is sanitized to remove characters that are
	// not suitable for file names.
	filename := html.UnescapeString(tb.gameInfo.GameName)
	filename = filenameSanitizer.Replace(filename) + "-" + tb.gameInfo.GameID
	return filename + ".trace"
}

// Attempt to the verify the trace file MD5 hash. The reference MD5 hash is read
// from property "trace_file_md5" from file trace_info.json in the game dir.
// Either may not be present, in which case this function will not fail. If the
// reference MD5 hash is found, then it must match the MD5 hash calculated from
// the trace file.
func (tb *TraceRecord) verifyTraceMD5Hash(traceFile string) error {
	if traceFile == "" {
		tb.printIfVerbose("Skip MD5 check for %s: no trace_info.json\n", traceFile)
		return nil
	}

	// Not all trace-info have the MD5 property.
	if tb.traceInfo.TraceFileMD5 == "" {
		tb.printIfVerbose("Skip MD5 check for %s: no MD5 property in trace_info.json\n", traceFile)
		return nil
	}

	tb.printIfVerbose("Verifying MD5 hash for %s\n", traceFile)
	fullPath := path.Join(tb.traceDir, traceFile)
	md5, err := GetFileMD5Hash(fullPath)
	if err != nil {
		return err
	}

	if md5 != tb.traceInfo.TraceFileMD5 {
		return fmt.Errorf("MD5 verification failed for %s", traceFile)
	}

	return nil
}
