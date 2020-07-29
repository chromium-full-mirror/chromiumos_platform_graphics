// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import "encoding/json"

// MachineInfoConfig encapsulates the config parameters that affect how
// machine-info is collected from the devices. Fields are as follows:
//  Enabled: whether machine info should be collected.
//  UploadToDb: TODO (gwink): options to upload protobuf to DB.
//  OutputFileTemplate: File name to which the protobuf should be written to.
//      [[exec-env]] replaced with the execution environment, taken from
//           target-device config, e.g. "host"
//      [[name]] replaced with machine name, taken from target-device config.
//      [[hwid]] is replaced with the machine hwid.
//      [[utc-date]] is replaced with the UTC date, e.g. "20060102-150405"
//      [[utc-time]] is replaced with the UTC time, e.g. "150405"
//      Example: ""machine-info-[[exec-env]]-[[hwid]].json"
type MachineInfoConfig struct {
	Enabled            bool   `json:"enabled"`
	UploadToDb         string `json:"uploadToDb"`
	OutputFileTemplate string `json:"outputFileTemplate"`
}

// DeviceInfoToolConfig encapsulates the parameters that the device-info tool
// uses to collect information from the devices. Fields are as follows:
//    GetDeviceInfoBinPath: full path to tool get_device_info. If left empty,
//        Harvest expects get_device_info to be available in PATH on the target
//        device.
//    ProtoBufsOutputDir: Full path to dir where protobufs should be written.
//    Owner: Optional owner string to use in machine info. If left blank, owner
//        is read from USER env on target device. May not be "root".
//    CroutonMachine & CrosvmMachine: MachineInfoConfig specific to each target
//        device. (See above.)
type deviceInfoToolConfig struct {
	GetDeviceInfoBinPath string             `json:"getDeviceInfoBinPath"`
	ProtoBufsOutputDir   string             `json:"protoBufsOutputDir"`
	Owner                string             `json:"owner"`
	Machine              *MachineInfoConfig `json:"machine"`
}

// DeviceInfoConfigParser provides support for reading and parsing device-info
// configuration from a Harvest config json file.
type DeviceInfoConfigParser struct {
	toolConfig deviceInfoToolConfig
}

// NewDeviceInfoConfigParser creates and returns a new DeviceInfoConfigParser
// object.
func NewDeviceInfoConfigParser() *DeviceInfoConfigParser {
	return &DeviceInfoConfigParser{}
}

// ParseJSONData implements interface profile.ConfigPropertyHandler.
func (dp *DeviceInfoConfigParser) ParseJSONData(propName, jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &dp.toolConfig); err != nil {
		return err
	}
	return nil
}

// GetDeviceInfoBinPath returns the get_device_info bin path read from the
// config file.
func (dp *DeviceInfoConfigParser) GetDeviceInfoBinPath() string {
	return dp.toolConfig.GetDeviceInfoBinPath
}

// GetProtoBufsOutputDir returns the full path to the protobuf output dir read
// from the config file.
func (dp *DeviceInfoConfigParser) GetProtoBufsOutputDir() string {
	return dp.toolConfig.ProtoBufsOutputDir
}

// GetOwner returns the owner string read from the config file.
func (dp *DeviceInfoConfigParser) GetOwner() string {
	return dp.toolConfig.Owner
}

// GetMachineConfig returns the machine parameters read from the config file. May be nil.
func (dp *DeviceInfoConfigParser) GetMachineConfig() *MachineInfoConfig {
	return dp.toolConfig.Machine
}
