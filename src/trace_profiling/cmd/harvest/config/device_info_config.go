// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import "encoding/json"

// MachineInfoConfig encapsulates the config parameters that affect how
// machine-info is collected from the devices. Fields are as follows:
//  Enabled: whether machine info should be collected.
type MachineInfoConfig struct {
	Enabled bool `json:"enabled"`
}

// SoftwareInfoConfig encapsulates the config parameters that affect how
// software info is collected from the devices. Fields are as follows:
//  Enabled: whether software info should be collected.
//  SkipPackages: when true do not collect software-package info.
//  AlsoRunOnParent: Whether software info should also be collected from the
//      parent device. This is meaningful only when the target device is not
//      the host OS, such as a steam-vm or crouton device.
type SoftwareInfoConfig struct {
	Enabled         bool `json:"enabled"`
	SkipPackages    bool `json:"skipPackages"`
	AlsoRunOnParent bool `json:"alsoRunOnParent"`
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
type DeviceInfoToolConfig struct {
	GetDeviceInfoBinPath string              `json:"getDeviceInfoBinPath"`
	ProtoBufsOutputDir   string              `json:"protoBufsOutputDir"`
	Owner                string              `json:"owner"`
	Machine              *MachineInfoConfig  `json:"machine"`
	Software             *SoftwareInfoConfig `json:"software"`
}

// DeviceInfoConfigParser provides support for reading and parsing device-info
// configuration from a Harvest config json file.
type DeviceInfoConfigParser struct {
	toolConfig DeviceInfoToolConfig
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

func (dp *DeviceInfoConfigParser) GetDeficeInfoToolConfig() *DeviceInfoToolConfig {
	return &dp.toolConfig
}
