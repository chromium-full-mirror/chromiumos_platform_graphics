// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package flags is used to show how to execute the binary basd on comm.Flags.
package flags

import (
	"bufio"
	"os"
	"strings"

	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/comm"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/pkg/errors"
)

// guestType indicates the environment trace_replay is running on.
type guestType int

const (
	// Supported guest types
	guestTypeUnknown guestType = iota
	guestTypeBorealis
	guestTypeCrostini
)

// ReplayAppConfig contains argument to construct the command line.
type ReplayAppConfig struct {
	AppName string   // Path to excuable binary to run the trace.
	Args    []string // Arguements to the executable binary
	EnvVars []string // Environmental variables to set before running the binary.
	Postfix string   // Postfix to use when reporting the metrics.
}

const (
	apitraceW32 = "apitrace-10.0-win32/bin/d3dretrace.exe"
	apitraceW64 = "apitrace-10.0-win64/bin/d3dretrace.exe"
	steamDir    = "/home/chronos/home/chronos/.steam/steam/steamapps/common/"
	slr         = steamDir + "SteamLinuxRuntime_soldier/"
	proton      = steamDir + "Proton 7.0/"
)

var configs = map[guestType]map[string]ReplayAppConfig{
	guestTypeBorealis: {
		comm.TestFlagDefault: {
			AppName: "glretrace",
			Args:    []string{"--benchmark", "--watchdog"},
			EnvVars: []string{"DISPLAY=:0"},
			Postfix: "",
		},
		comm.TestFlagSurfaceless: {
			AppName: "eglretrace",
			Args:    []string{"--benchmark", "--watchdog"},
			EnvVars: []string{"WAFFLE_PLATFORM=sl", "LD_PRELOAD=libEGL.so.1"},
			Postfix: "_surfaceless",
		},
		comm.TestFlagD3DW32: {
			AppName: "/opt/win_tools/bin/exerun.py",
			Args:    []string{"--slr", slr, "--proton", proton, apitraceW32},
			EnvVars: []string{"DISPLAY=:0"},
			Postfix: "_d3d32",
		},
		comm.TestFlagD3DW64: {
			AppName: "/opt/win_tools/bin/exerun.py",
			Args:    []string{"--slr", slr, "--proton", proton, apitraceW64},
			EnvVars: []string{"DISPLAY=:0"},
			Postfix: "_d3d64",
		},
	},
	guestTypeCrostini: {
		comm.TestFlagDefault: {
			AppName: "glretrace",
			Args:    []string{"--benchmark"},
			EnvVars: []string{"DISPLAY=:0"},
			Postfix: "",
		},
		comm.TestFlagSurfaceless: {
			AppName: "eglretrace",
			Args:    []string{"--benchmark"},
			EnvVars: []string{"WAFFLE_PLATFORM=sl", "LD_PRELOAD=libEGL.so.1"},
			Postfix: "_surfaceless",
		},
	},
}

// GetReplayAppConfigs returns the config to run based on the flag.
func GetReplayAppConfigs(flag string) (ReplayAppConfig, error) {
	guestType, err := getGuestType()
	if err != nil {
		return ReplayAppConfig{}, errors.Wrap(err, "failed to determine the guest type")
	}

	config, ok := configs[guestType]
	if !ok {
		return ReplayAppConfig{}, errors.New("unsupported guest type: %v", guestType)
	}

	replayConfig, ok := config[flag]
	if !ok {
		return ReplayAppConfig{}, errors.New("unsupported flag: %v on guest type: %v", flag, guestType)
	}
	return replayConfig, nil
}

func getGuestType() (guestType, error) {
	// Check for Borealis
	if lsbFile, err := os.Open("/etc/lsb-release"); err == nil {
		defer lsbFile.Close()
		scanner := bufio.NewScanner(lsbFile)
		for scanner.Scan() {
			if strings.Contains(scanner.Text(), "BOREALIS_STAGE=") {
				return guestTypeBorealis, nil
			}
		}
	}

	// Check for Crostini
	hostName, err := os.Hostname()
	if err != nil {
		return guestTypeUnknown, errors.Wrap(err, "unable to get hostname")
	}
	if hostName == "penguin" {
		return guestTypeCrostini, nil
	}
	return guestTypeUnknown, errors.New("unable to detetermine guest type")
}
