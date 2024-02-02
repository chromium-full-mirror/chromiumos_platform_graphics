// Copyright 2022 The ChromiumOS Authors
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
	// ApitraceW32 keeps track of the 32 bit binary for d3dretrace.
	ApitraceW32 = "d3dretrace32.exe"
	// ApitraceW64 keeps track of the 64 bit binary for d3dretrace.
	ApitraceW64 = "d3dretrace64.exe"
	slr         = "SteamLinuxRuntime_sniper/"
	proton      = "Proton 8.0/"
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
		comm.TestFlagZink: {
			AppName: "glretrace",
			Args:    []string{"--benchmark", "--watchdog"},
			EnvVars: []string{"DISPLAY=:0", "MESA_LOADER_DRIVER_OVERRIDE=zink",
					  "GALLIUM_DRIVER=zink"},
			Postfix: "_zink",
		},
		comm.TestFlagZinkSurfaceless: {
			AppName: "eglretrace",
			Args:    []string{"--benchmark", "--watchdog"},
			EnvVars: []string{"WAFFLE_PLATFORM=sl", "LD_PRELOAD=libEGL.so.1",
					  "MESA_LOADER_DRIVER_OVERRIDE=zink","GALLIUM_DRIVER=zink"},
			Postfix: "_zink_surfaceless",
		},
		comm.TestFlagD3DW32: {
			AppName: "/opt/win_tools/bin/exerun.py",
			Args:    []string{"--slr", slr, "--proton", proton, ApitraceW32},
			EnvVars: []string{"DISPLAY=:0"},
			Postfix: "",
		},
		comm.TestFlagD3DW64: {
			AppName: "/opt/win_tools/bin/exerun.py",
			Args:    []string{"--slr", slr, "--proton", proton, ApitraceW64},
			EnvVars: []string{"DISPLAY=:0"},
			Postfix: "",
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
			if strings.Contains(scanner.Text(), "CHROMEOS_RELEASE_BOARD=borealis") {
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
