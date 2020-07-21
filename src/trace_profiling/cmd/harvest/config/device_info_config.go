// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import "encoding/json"

// MachineInfoConfig encapsulates the machine-configuration parameters read from
// the JSON config file. Fields are as follows:
//  Enabled: whether machine info should be collected.
//  Name: Optional name to ID this machine by. A UUID is assigned if left blank.
//  UploadToDb: TODO (gwink): options to upload protobuf to DB.
//  OutputFileTemplate: File name to which the protobuf should be written to.
//      [[hwid]] is replaced with the machine hwid.
//      [[utc-date]] is replaced with the UTC date, e.g. "20060102-150405"
//      [[utc-time]] is replaced with the UTC time, e.g. "150405"
//      Example: ""machine-info-crosvm-[[hwid]].json"
type MachineInfoConfig struct {
	Enabled            bool   `json:"enabled"`
	Name               string `json:"name"`
	UploadToDb         string `json:"uploadToDb"`
	OutputFileTemplate string `json:"outputFileTemplate"`
}

// DeviceInfoConfig encapsulates the device-info configuration parameters read from
// the JSON config file. Fields are as follows:
//    GetDeviceInfoBinPath: full path to tool get_device_info. If left empty,
//        Harvest expects get_device_info to be available in PATH on the target
//        device.
//    ProtoBufsOutputDir: Full path to dir where protobufs should be written.
//    Owner: Optional owner string to use in machine info. If left blank, owner
//        is read from USER env on target device. May not be "root".
//    CroutonMachine & CrosvmMachine: MachineInfoConfig specific to each target
//        device. (See above.)
type deviceInfoConfig struct {
	GetDeviceInfoBinPath string             `json:"getDeviceInfoBinPath"`
	ProtoBufsOutputDir   string             `json:"protoBufsOutputDir"`
	Owner                string             `json:"owner"`
	CroutonMachine       *MachineInfoConfig `json:"croutonMachine"`
	CrosvmMachine        *MachineInfoConfig `json:"crosvmMachine"`
}

// DeviceInfoConfigParser provides support for reading and parsing device-info
// configuration from a Harvest config json file.
type DeviceInfoConfigParser struct {
	config deviceInfoConfig
}

// NewDeviceInfoConfigParser creates and returns a new DeviceInfoConfigParser
// object.
func NewDeviceInfoConfigParser() *DeviceInfoConfigParser {
	return &DeviceInfoConfigParser{}
}

// ParseJSONData is a handler function that implements interface
// profile.ConfigPropertyHandler.
func (dp *DeviceInfoConfigParser) ParseJSONData(jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &dp.config); err != nil {
		return err
	}
	return nil
}

// GetDeviceInfoBinPath returns the get_device_info bin path read from the
// config file.
func (dp *DeviceInfoConfigParser) GetDeviceInfoBinPath() string {
	return dp.config.GetDeviceInfoBinPath
}

// GetProtoBufsOutputDir returns the full path to the protobuf output dir read
// from the config file.
func (dp *DeviceInfoConfigParser) GetProtoBufsOutputDir() string {
	return dp.config.ProtoBufsOutputDir
}

// GetOwner returns the owner string read from the config file.
func (dp *DeviceInfoConfigParser) GetOwner() string {
	return dp.config.Owner
}

// GetCrosvmMachineInfoConfig returns the Crosvm machine-info config options read
// from the config file. May be nil.
func (dp *DeviceInfoConfigParser) GetCrosvmMachineInfoConfig() *MachineInfoConfig {
	return dp.config.CrosvmMachine
}

// GetCroutonMachineInfoConfig returns the Crouton machine-info config options read
// from the config file. May be nil.
func (dp *DeviceInfoConfigParser) GetCroutonMachineInfoConfig() *MachineInfoConfig {
	return dp.config.CroutonMachine
}
