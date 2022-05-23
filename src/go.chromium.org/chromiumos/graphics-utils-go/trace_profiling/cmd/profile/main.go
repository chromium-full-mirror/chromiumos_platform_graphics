// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/profile"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/remote"
)

func runProfiling(prof *profile.Profiler, target *remote.SSHTarget, tunnel *remote.Tunnel) {
	tunnelReady := make(chan bool, 1)
	tunnelError := make(chan error, 1)
	if tunnel != nil {
		go func(errChan chan error) {
			errChan <- tunnel.BeginTunneling(tunnelReady)
		}(tunnelError)
	} else {
		tunnelReady <- true // No tunnel implies it's ready to go as is.
	}

	defer func() {
		if tunnel != nil {
			tunnel.Close()
			<-tunnelReady // Wait for tunnel to be closed.
		}
	}()

	// Wait until tunneling is ready to go or failed.
	select {
	case err := <-tunnelError:
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
		return
	case <-tunnelReady:
		{
			err := target.Connect()
			if err != nil {
				fmt.Fprintf(os.Stderr, "SSH error: %s\n", err.Error())
				return
			}
			defer target.Disconnect()

			err = prof.GatherProfiles()
			if err != nil {
				fmt.Fprintln(os.Stderr, err.Error())
			}
		}
	}
}

func main() {
	var argConfigFilePath string
	var argForceInstallTools bool
	var argAlwaysCopyTraces bool
	var argEnableVerbose bool

	flag.StringVar(&argConfigFilePath, "config", "",
		"Path to json config file with the SSH, tunneling and profiling parameters.")
	flag.BoolVar(&argForceInstallTools, "reinstall-tools", false,
		"Re-install the profiling tools on the remote device, even if they are already there.")
	flag.BoolVar(&argAlwaysCopyTraces, "always-copy-traces", false,
		"Always copy the traces to the remote profiling device, even if they are already there.")
	flag.BoolVar(&argEnableVerbose, "verbose", false,
		"Enable verbose mode, to see more info during profiling.")
	flag.Parse()

	var sshParams *remote.SSHParams
	var tunnelParams *remote.TunnelParams
	var profParams *profile.ProfileParams
	var profilerConfig *profile.ProfilerConfigParser
	var err error
	if argConfigFilePath != "" {
		profilerConfig = profile.CreateProfilerConfigParser()
		if err = profilerConfig.ParseJSONFile(argConfigFilePath); err != nil {
			fmt.Fprintln(os.Stderr, err.Error())
			return
		}
	}

	if profilerConfig != nil {
		sshParams = profilerConfig.GetSSHParams()
		tunnelParams = profilerConfig.GetTunnelParams()
		profParams = profilerConfig.GetProfileParams()

		if sshParams == nil {
			fmt.Fprintf(os.Stderr, "No SSH configuration in %s\n", argConfigFilePath)
			return
		}
		if profParams == nil {
			fmt.Fprintf(os.Stderr, "No Profiler configuration in %s\n", argConfigFilePath)
			return
		}
	}

	// Tunneling is optional. If no tunnel parameters are provided, the SHH
	// connection to the target device will be direct.
	var tunnel *remote.Tunnel
	if tunnelParams != nil {
		tunnel, err = remote.CreateTunnel(tunnelParams)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error creating tunnel %s\n", err.Error())
			return
		}
	}

	var target *remote.SSHTarget
	target, err = remote.CreateSSHTargetWithParams(sshParams)
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return
	}

	var prof = profile.CreateProfiler(profParams, target)
	prof.SetEnableVerbose(argEnableVerbose)
	prof.SetAlwaysCopyTraces(argAlwaysCopyTraces)
	prof.SetForceInstallTools(argForceInstallTools)
	runProfiling(prof, target, tunnel)
}
