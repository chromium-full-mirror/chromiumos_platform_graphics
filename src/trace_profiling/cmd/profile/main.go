// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	"trace_profiling/cmd/profile/profile"
	"trace_profiling/cmd/profile/remote"
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

// Retrieve the configuration parameters from a single Bundle JSON file.
func getParamsFromBundle(bundleFilePath string) (
	*remote.SSHParams, *remote.TunnelParams, *profile.ProfileParams, error) {
	sshParams, tunnelParams, profileParams, err := profile.ReadConfigBundle(bundleFilePath)
	return sshParams, tunnelParams, profileParams, err
}

// Retrieve the configuration parameters from individual JSON files for
// ssh, tunnel and profile configurations.
func getParamsFromFiles(sshConfigFile, tunnelConfigFile, profileConfigFile string) (
	*remote.SSHParams, *remote.TunnelParams, *profile.ProfileParams, error) {

	var tunnelConfig *remote.TunnelParams
	var err error
	if tunnelConfigFile != "" && tunnelConfigFile != "/no-tunnel/" {
		tunnelConfig, err = remote.ReadTunnelParamsFromJSON(tunnelConfigFile)
		if err != nil {
			return nil, nil, nil, err
		}
	}

	sshConfig, err := remote.CreateSSHParamsFromJSON(sshConfigFile)
	if err != nil {
		return nil, nil, nil, err
	}

	profConfig, err := profile.CreateProfileParamsFromJSON(profileConfigFile)
	if err != nil {
		return nil, nil, nil, err
	}

	return sshConfig, tunnelConfig, profConfig, nil
}

func main() {
	var argUnifiedConfigFilePath string
	var argTunnelConfigFilepath string
	var argSSHConfigFilepath string
	var argBundleConfigFilePath string
	var argProfileConfigFilepath string
	var argForceInstallTools bool
	var argAlwaysCopyTraces bool
	var argEnableVerbose bool

	flag.StringVar(&argUnifiedConfigFilePath, "config", "",
		"Specify the SSH, tunneling and profiling parameters in a unified config json file.\n"+
			"(Other config files are ignored when this is specified.)")
	flag.StringVar(&argTunnelConfigFilepath, "tunnel-config", "/no-tunnel/",
		"Optional tunnel (port-forwarding) configuration file")
	flag.StringVar(&argSSHConfigFilepath, "ssh-config", "ssh_config.json",
		"SSH configuration file")
	flag.StringVar(&argProfileConfigFilepath, "profile-config", "profile_config.json",
		"Profile configuration file")
	flag.StringVar(&argBundleConfigFilePath, "config-bundle", "",
		"Configuration-bundle file (deprecated, use -config with new json format instead)")
	flag.BoolVar(&argForceInstallTools, "reinstall-tools", false,
		"Re-install the profiling tools on the remote device, even if they are already there")
	flag.BoolVar(&argAlwaysCopyTraces, "always-copy-traces", false,
		"Always copy the traces to the remote profiling device, even if they are already there")
	flag.BoolVar(&argEnableVerbose, "verbose", false,
		"Enable verbose mode, to see more info during profiling")
	flag.Parse()

	// Unified configuration takes precedence, then a config bundle if available.
	// Otherwise, we look for individual SSH, tunnel and profile config files.
	var sshParams *remote.SSHParams
	var tunnelParams *remote.TunnelParams
	var profParams *profile.ProfileParams
	var profilerConfig *profile.ProfilerConfigParser
	var err error
	if argUnifiedConfigFilePath != "" {
		profilerConfig = profile.CreateProfilerConfigParser()
		err = profilerConfig.ParseJSONFile(argUnifiedConfigFilePath)
	} else if argBundleConfigFilePath != "" {
		sshParams, tunnelParams, profParams, err = getParamsFromBundle(argBundleConfigFilePath)
	} else {
		sshParams, tunnelParams, profParams, err = getParamsFromFiles(
			argSSHConfigFilepath, argTunnelConfigFilepath, argProfileConfigFilepath)
	}

	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return
	}

	if profilerConfig != nil {
		sshParams = profilerConfig.GetSSHParams()
		tunnelParams = profilerConfig.GetTunnelParams()
		profParams = profilerConfig.GetProfileParams()

		if sshParams == nil {
			fmt.Fprintf(os.Stderr, "No SSH configuration in %s\n", argUnifiedConfigFilePath)
			return
		}
		if profParams == nil {
			fmt.Fprintf(os.Stderr, "No Profiler configuration in %s\n", argUnifiedConfigFilePath)
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
