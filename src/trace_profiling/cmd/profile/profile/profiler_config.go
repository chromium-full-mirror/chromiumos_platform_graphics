// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"encoding/json"
	"trace_profiling/cmd/profile/remote"
)

// All the config data relevant to Profiler in one place, for parsing from json.
type allProfilerConfigs struct {
	TunnelConfig   *remote.TunnelParams `json:"tunnelConfig"`
	SSHConfig      *remote.SSHParams    `json:"sshConfig"`
	ProfilerConfig *ProfileParams       `json:"profilerConfig"`
}

// ProfilerConfig is a helper for parsing Profiler configuration data from json
// config files.
type ProfilerConfig struct {
	jsonConfig *JSONConfig
	configData allProfilerConfigs
}

// CreateProfilerConfig creates and returns a ProfilerConfig instance.
func CreateProfilerConfig() *ProfilerConfig {
	pc := ProfilerConfig{
		jsonConfig: CreateJSONConfig(),
	}

	pc.jsonConfig.AddHandler("Profile", &pc)
	return &pc
}

// ParseJSONFile parse json file with path <jsonFile>. When this function is
// successful, the parsed data may be retrieved with functions GetSSHParams,
// GetTunnelParams and GetProfilerParams.
func (pc *ProfilerConfig) ParseJSONFile(jsonFile string) error {
	if err := pc.jsonConfig.OpenJSONConfigFile(jsonFile); err != nil {
		return err
	}

	return pc.jsonConfig.Process()
}

// GetProfilerParams returns the ProfilerParams parsed from the json file.
// May return nil.
func (pc *ProfilerConfig) GetProfilerParams() *ProfileParams {
	return pc.configData.ProfilerConfig
}

// GetSSHParams returns the SSHParams parsed from the json file. May return nil.
func (pc *ProfilerConfig) GetSSHParams() *remote.SSHParams {
	return pc.configData.SSHConfig
}

// GetTunnelParams returns the TunnelParams parsed from the json file.
// May return nil.
func (pc *ProfilerConfig) GetTunnelParams() *remote.TunnelParams {
	return pc.configData.TunnelConfig
}

// parseJSONData is the handler function for interface ConfigPropertyHandler.
func (pc *ProfilerConfig) parseJSONData(jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &pc.configData); err != nil {
		return err
	}

	return nil
}
