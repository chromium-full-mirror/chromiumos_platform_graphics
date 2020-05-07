// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"encoding/json"
	"io"
	"trace_profiling/cmd/profile/remote"
)

// ProfilerConfigRecord encapsulates all the parameters needed for profiling in
// one place, convenient for un-marshalling from JSON.
type ProfilerConfigRecord struct {
	TunnelConfig  *remote.TunnelParams `json:"tunnelConfig"`
	SSHConfig     *remote.SSHParams    `json:"sshConfig"`
	ProfileParams *ProfileParams       `json:"profilerConfig"`
}

// ProfilerConfigParser helps parse Profiler configuration data from json
// config files and makes the result accessible through accessors.
type ProfilerConfigParser struct {
	jsonParser *JSONConfigParser
	configData ProfilerConfigRecord
}

// CreateProfilerConfigParser creates and returns a ProfilerConfigParser instance.
func CreateProfilerConfigParser() *ProfilerConfigParser {
	pc := ProfilerConfigParser{
		jsonParser: CreateJSONConfigParser(),
	}

	pc.jsonParser.AddHandler("Profile", &pc)
	return &pc
}

// ParseJSONFile parse json file with path <jsonFile>. When this function is
// successful, the parsed data may be retrieved with functions GetSSHParams,
// GetTunnelParams and GetProfilerParams.
func (pc *ProfilerConfigParser) ParseJSONFile(jsonFile string) error {
	if err := pc.jsonParser.OpenJSONConfigFile(jsonFile); err != nil {
		return err
	}

	return pc.jsonParser.Process()
}

// ParseJSONFromReader parse json data from the given reader. When this function
// is successful, the parsed data may be retrieved with functions GetSSHParams,
// GetTunnelParams and GetProfilerParams.
func (pc *ProfilerConfigParser) ParseJSONFromReader(jsonReader io.Reader) error {
	if err := pc.jsonParser.OpenJSONFromReader(jsonReader); err != nil {
		return err
	}

	return pc.jsonParser.Process()
}

// GetProfilerConfig returns the profiler config data parsed from json.
func (pc *ProfilerConfigParser) GetProfilerConfig() *ProfilerConfigRecord {
	return &pc.configData
}

// GetProfileParams returns the ProfileParams parsed from the json file.
// May return nil.
func (pc *ProfilerConfigParser) GetProfileParams() *ProfileParams {
	return pc.configData.ProfileParams
}

// GetSSHParams returns the SSHParams parsed from the json file. May return nil.
func (pc *ProfilerConfigParser) GetSSHParams() *remote.SSHParams {
	return pc.configData.SSHConfig
}

// GetTunnelParams returns the TunnelParams parsed from the json file.
// May return nil.
func (pc *ProfilerConfigParser) GetTunnelParams() *remote.TunnelParams {
	return pc.configData.TunnelConfig
}

// ParseJSONData is the handler function for interface ConfigPropertyHandler.
func (pc *ProfilerConfigParser) ParseJSONData(jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &pc.configData); err != nil {
		return err
	}

	return nil
}
