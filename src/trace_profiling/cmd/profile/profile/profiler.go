// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"strings"

	"trace_profiling/cmd/profile/remote"
)

// ProfileParams bundles all the parameters needed to profile traces on a
// target device.
type ProfileParams struct {
	LocalTraceDir     string   `json:"localTraceDir"`
	TargetTraceDir    string   `json:"targetTraceDir"`
	Traces            []string `json:"traces"`
	KeepTraceOnTarget bool     `json:"keepTraceOnTarget"`
	LocalProfileDir   string   `json:"localProfileDir"`
	ProfileNameSuffix string   `json:"profileNameSuffix"`
	LocalProfAppPath  string   `json:"localProfAppPath"`
	TargetProfAppPath string   `json:"targetProfAppPath"`
	ProfCommand       string   `json:"profCommand"`
	TargetDisplay     string   `json:"targetDisplay"`
}

// Profiler provides support for gathering trace-based profiles on a target
// device reached through SSH.
type Profiler struct {
	params            *ProfileParams
	target            *remote.SSHTarget
	verbose           bool
	alwaysCopyTrace   bool
	forceInstallTools bool
}

// CreateProfiler creates and returns a Profiler instance.
func CreateProfiler(params *ProfileParams, target *remote.SSHTarget) *Profiler {
	return &Profiler{params, target, false, false, false}
}

// CreateProfileParamsFromJSON is a convenience function that reads profile
// parameters from a json file and returns them as a ProfileParams object.
func CreateProfileParamsFromJSON(jsonFilepath string) (*ProfileParams, error) {
	file, err := os.Open(jsonFilepath)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot open Profile config file <%s>; error=%w",
			jsonFilepath, err)
	}
	defer file.Close()

	jsonData, err := ioutil.ReadAll(file)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot read Profile config file <%s>; error=%w",
			jsonFilepath, err)
	}

	var config ProfileParams
	json.Unmarshal(jsonData, &config)
	return &config, nil
}

// SetEnableVerbose is used to enable or disable verbose mode. (Disabled by default).
func (p *Profiler) SetEnableVerbose(enable bool) {
	p.verbose = enable
}

// SetAlwaysCopyTraces is used to set whether traces should always be copied to
// the device. By default, traces are not copied to the remote device if they are
// already there. Setting this option to true will cause traces to always be copied.
func (p *Profiler) SetAlwaysCopyTraces(always bool) {
	p.alwaysCopyTrace = always
}

// SetForceInstallTools is used to set whether profiling tools should always be
// installed to the device. By default, the tools are not copied to the remote
// device if they are already there. Setting this option to true will cause
// tools to always be copied.
func (p *Profiler) SetForceInstallTools(force bool) {
	p.forceInstallTools = force
}

// GatherProfiles runs the profiling tools on the remote target with the provided
// traces to gather the profiles.
func (p *Profiler) GatherProfiles() error {
	err := p.installTools()
	if err != nil {
		return err
	}

	for _, trace := range p.params.Traces {
		srcTrace := path.Join(p.params.LocalTraceDir, trace)
		dstTrace := path.Join(p.params.TargetTraceDir, trace)
		err = p.installTrace(srcTrace, p.params.TargetTraceDir)
		if err != nil {
			return err
		}

		profName := trace + p.params.ProfileNameSuffix
		localProf := path.Join(p.params.LocalProfileDir, profName)
		err = p.profileTrace(p.params.ProfCommand, dstTrace, localProf)
		if err != nil {
			return err
		}

		// Other tools, such as Harvest, rely on this output to locate the profile output.
		fmt.Printf("OutProfile=%s\n", localProf)

		if !p.params.KeepTraceOnTarget {
			p.target.DelFile(dstTrace)
		}
	}

	return nil
}

// Install a trace on the target device. By default, if the trace already exists
// on the target device it is not installed. Set force to true to override that
// behavior.
func (p *Profiler) installTrace(srcTracePath, destDirPath string) (err error) {
	traceName := path.Base(srcTracePath)
	dstTracePath := path.Join(destDirPath, traceName)

	p.printIfVerbose("Install trace %s on target: ", traceName)

	err = nil
	if ok, _ := p.target.CheckFileExists(dstTracePath); !ok || p.alwaysCopyTrace {
		p.target.Mkdir(destDirPath)
		permissions := getLocalFilePermissions(srcTracePath)
		err = p.target.SendFile(srcTracePath, dstTracePath, permissions)
		p.printIfVerbose("%s\n", doneOrError(err == nil))
	} else {
		p.printIfVerbose("skipped (already exist)\n")
	}
	return
}

// Install the profiling tools on the target device. By default, if the tools
// already exists on the target device they are not installed. Set force to true
// to override that behavior.
func (p *Profiler) installTools() error {
	p.printIfVerbose("Install tool(s) %s on target: ", p.params.LocalProfAppPath)

	err := p.copyFilesFromLocal(
		p.params.LocalProfAppPath, p.params.TargetProfAppPath, p.forceInstallTools)
	p.printIfVerbose("%s\n", doneOrError(err == nil))
	return err
}

// Profile a single trace on the target device and produce the profile data
// in the local file <localProfPath>.
func (p *Profiler) profileTrace(binCmd, tracePath, localProfPath string) error {
	defer func() {
		if !p.params.KeepTraceOnTarget {
			p.target.DelFile(tracePath)
		}
	}()

	outFile, err := os.OpenFile(localProfPath, os.O_RDWR|os.O_CREATE, 0755)
	if err != nil {
		return err
	}
	defer outFile.Close()

	tmpFilename, err := p.target.MkTempFileName()
	if err != nil {
		return err
	}
	defer func() {
		p.target.DelFile(tmpFilename)
	}()

	p.printIfVerbose("Profiling to temp file %s ", tmpFilename)

	cmd := strings.Replace(binCmd, "[[trace-file]]", tracePath, 1)
	cmd = strings.Replace(cmd, "[[prof-file]]", tmpFilename, 1)
	cmd = fmt.Sprintf("DISPLAY=:%s %s", p.params.TargetDisplay, cmd)
	output, err := p.target.RunCmd(cmd)
	if err != nil {
		p.printIfVerbose(" error, stdout=\"%s\"\n", output)
		return err
	}

	p.printIfVerbose("\nCopy profile to local file %s: ", localProfPath)

	cmd = fmt.Sprintf("cat %s", tmpFilename)
	outFile.Truncate(0)
	err = p.target.RunCmdWithWriter(cmd, outFile)
	p.printIfVerbose("%s\n", doneOrError(err == nil))
	return err
}

// Copy a single file or directory from the local host to the target device.
// If <srcFilePath> is a file, that single file is copied. If it is a directory,
// the entire directory content is copied, but not recursively. The directories
// in destFilePath are created on the target device if they do not already exist.
// If <srcFileZPath> is a directory, but <destFileZPath> is a file, only the
// directory portion of <destFilePath> is used.
func (p *Profiler) copyFilesFromLocal(srcFilePath, destFilePath string, force bool) (err error) {
	destFileDir := path.Dir(destFilePath)

	p.target.Mkdir(destFileDir)

	err = nil
	if isLocalDirectory(srcFilePath) {
		files, _ := ioutil.ReadDir(srcFilePath)
		for _, file := range files {
			srcPath := path.Join(srcFilePath, file.Name())
			if !isLocalDirectory(srcPath) {
				dstPath := path.Join(destFileDir, file.Name())
				permissions := getLocalFilePermissions(srcPath)
				if ok, _ := p.target.CheckFileExists(dstPath); !ok || force {
					err = p.target.SendFile(srcPath, dstPath, permissions)
					if err != nil {
						return
					}
				}
			}
		}
	} else {
		if ok, _ := p.target.CheckFileExists(destFilePath); !ok || force {
			permissions := getLocalFilePermissions(srcFilePath)
			err = p.target.SendFile(srcFilePath, destFilePath, permissions)
		}
	}
	return
}

// Return the permissions of a local file as a OOOO number, e.g. 0755.
func getLocalFilePermissions(filePath string) string {
	fileInfo, err := os.Stat(filePath)
	if err != nil {
		return ""
	}
	return fmt.Sprintf("%04o", fileInfo.Mode().Perm())
}

// Return whether filePath is an existing local directory.
func isLocalDirectory(filePath string) bool {
	fileInfo, err := os.Stat(filePath)
	return err == nil && fileInfo.IsDir()
}

// Return "done" if done is true and "error" otherwise.
func doneOrError(done bool) string {
	if done {
		return "done"
	}
	return "error"
}

// If verbose mode is enabled, print the formatted string.
func (p *Profiler) printIfVerbose(format string, a ...interface{}) {
	if p.verbose {
		fmt.Printf(format, a...)
	}
}
