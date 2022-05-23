// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"encoding/json"
	"fmt"
	"io"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/remote"
)

// List if valid device exec environment. Mirrors ExecutionEnvironment in result.proto.
var validExecEnvNames = []string{"host", "termina", "crostini", "steam", "arc",
	"arcvm", "crouton", "crosvm"}

// ProfilerConfigRecord encapsulates all the parameters needed for profiling.
// Fields are as follows:
//   TunnelConfig: Parameters used for SSH tunneling. Maybe nil, in which case
//       the device should be reachable directly with the SSH parameters below.
//   SSHConfig: SSH parameters used for reaching the device. If the TunnelConfig
//       above is not nil, then these are the SSH parameters for reaching the
//       tunnel server.
//   ProfileParams: Parameters needed for profiling on the device by running
//       traces.
type ProfilerConfigRecord struct {
	TunnelConfig  *remote.TunnelParams `json:"tunnelConfig"`
	SSHConfig     *remote.SSHParams    `json:"sshConfig"`
	ProfileParams *ProfileParams       `json:"profilerConfig"`
}

// DeviceConfigRecord encapsulates information about the device on which profiling
// takes place. Fields are as follows:
//   Name: unique name used to record the machine info in the result DB.
//   ExecEnv: execution environment, one of validExecEnvNames.
type DeviceConfigRecord struct {
	Name    string `json:"name"`
	ExecEnv string `json:"execEnv"`
}

// ProfilerConfigParser helps parse Profiler configuration data from json
// config files and makes the result accessible through accessors.
type ProfilerConfigParser struct {
	jsonParser    *JSONConfigParser
	profileConfig ProfilerConfigRecord
	deviceConfig  DeviceConfigRecord
}

// CreateProfilerConfigParser creates and returns a ProfilerConfigParser instance.
func CreateProfilerConfigParser() *ProfilerConfigParser {
	pc := ProfilerConfigParser{
		jsonParser: CreateJSONConfigParser(),
	}

	pc.jsonParser.AddHandler("Profile", &pc.profileConfig)
	pc.jsonParser.AddHandler("Device", &pc.deviceConfig)
	return &pc
}

// SetBasePath set the base paths for all subsequent includes. This is optional,
// but when used it must be done before opening or including any file.
func (pc *ProfilerConfigParser) SetBasePath(basePath string) {
	pc.jsonParser.SetBasePath(basePath)
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
	return &pc.profileConfig
}

// GetProfileParams returns the ProfileParams parsed from the json file.
// May return nil.
func (pc *ProfilerConfigParser) GetProfileParams() *ProfileParams {
	return pc.profileConfig.ProfileParams
}

// GetDeviceConfig returns a pointer to the device-config record read from the
// JSON configuration.
func (pc *ProfilerConfigParser) GetDeviceConfig() *DeviceConfigRecord {
	return &pc.deviceConfig
}

// GetSSHParams returns the SSHParams parsed from the json file. May return nil.
func (pc *ProfilerConfigParser) GetSSHParams() *remote.SSHParams {
	return pc.profileConfig.SSHConfig
}

// GetTunnelParams returns the TunnelParams parsed from the json file.
// May return nil.
func (pc *ProfilerConfigParser) GetTunnelParams() *remote.TunnelParams {
	return pc.profileConfig.TunnelConfig
}

// ParseJSONData implements interface ConfigPropertyHandler on struct
// ProfilerConfigRecord.
func (r *ProfilerConfigRecord) ParseJSONData(propName, jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), r); err != nil {
		return fmt.Errorf("could not parse %s: err = %s", propName, err.Error())
	}

	return nil
}

// ParseJSONData implements interface ConfigPropertyHandler on struct
// DeviceConfigRecord.
func (d *DeviceConfigRecord) ParseJSONData(propName, jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), d); err != nil {
		return fmt.Errorf("could not parse %s: err = %s", propName, err.Error())
	}

	// Validate exec env.
	for _, validName := range validExecEnvNames {
		if d.ExecEnv == validName {
			return nil
		}
	}

	return fmt.Errorf("invalid ExecEnv value \"%s\" in config for %s", d.ExecEnv, propName)
}
