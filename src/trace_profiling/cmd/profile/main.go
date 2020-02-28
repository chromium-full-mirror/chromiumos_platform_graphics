// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	"trace_profiling/cmd/profile/remote"
)

func runProfiling(prof *Profiler, target *remote.SSHTarget, tunnel *remote.Tunnel) {
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
	var argTunnelConfigFilepath string
	var argSSHConfigFilepath string
	var argProfileConfigFilepath string
	var argForceInstallTools bool
	var argAlwaysCopyTraces bool
	var argEnableVerbose bool

	flag.StringVar(&argTunnelConfigFilepath, "tunnel-config", "/no-tunnel/",
		"Optional tunnel (port-forwarding) configuration file")
	flag.StringVar(&argSSHConfigFilepath, "ssh-config", "ssh_config.json",
		"SSH configuration file")
	flag.StringVar(&argProfileConfigFilepath, "profile-config", "profile_config.json",
		"Profile configuration file")
	flag.BoolVar(&argForceInstallTools, "reinstall-tools", false,
		"Re-install the profiling tools on the remote device, even if they are already there")
	flag.BoolVar(&argAlwaysCopyTraces, "always-copy-traces", false,
		"Always copy the traces to the remote profiling device, even if they are already there")
	flag.BoolVar(&argEnableVerbose, "verbose", false,
		"Enable verbose mode, to see more info during profiling")
	flag.Parse()

	// Tunneling is optional. If no tunnel parameters are provided, the SHH
	// connection to the target device will be direct.
	var tunnel *remote.Tunnel
	if argTunnelConfigFilepath != "" && argTunnelConfigFilepath != "/no-tunnel/" {
		tunnelConfig, err := remote.ReadTunnelParamsFromJSON(argTunnelConfigFilepath)
		if err != nil {
			fmt.Fprintln(os.Stderr, err.Error())
			return
		}

		tunnel, err = remote.CreateTunnel(tunnelConfig)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Create tunnel %s\n", err.Error())
			return
		}
	}

	sshConfig, err := remote.CreateSSHParamsFromJSON(argSSHConfigFilepath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return
	}

	var target *remote.SSHTarget
	target, err = remote.CreateSSHTargetWithParams(sshConfig)
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return
	}

	profConfig, err := CreateProfileParamsFromJSON(argProfileConfigFilepath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return
	}
	var prof = CreateProfiler(profConfig, target)
	prof.SetEnableVerbose(argEnableVerbose)
	prof.SetAlwaysCopyTraces(argAlwaysCopyTraces)
	prof.SetForceInstallTools(argForceInstallTools)
	runProfiling(prof, target, tunnel)
}
