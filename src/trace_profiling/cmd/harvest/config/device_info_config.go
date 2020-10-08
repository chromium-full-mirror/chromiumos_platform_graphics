// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"encoding/json"
	"fmt"
)

// MachineInfoConfig encapsulates the config parameters that affect how
// machine-info is collected from the devices. Fields are as follows:
//  Enabled: whether machine info should be collected.
//  UploadToDb: whether the protobuf should be uploaded to the database, one of
//      "no" or "yes. Default is "no".
type MachineInfoConfig struct {
	Enabled    bool   `json:"enabled"`
	UploadToDb string `json:"uploadToDb"`
}

// SoftwareInfoConfig encapsulates the config parameters that affect how
// software info is collected from the devices. Fields are as follows:
//  Enabled: whether software info should be collected.
//  SkipPackages: when true do not collect software-package info.
//  AlsoRunOnParent: Whether software info should also be collected from the
//      parent device. This is meaningful only when the target device is not
//      the host OS, such as a steam-vm or crouton device.
//  UploadToDb: whether the protobuf should be uploaded to the database, one of
//      "no" or "yes. Default is "no".
type SoftwareInfoConfig struct {
	Enabled         bool   `json:"enabled"`
	SkipPackages    bool   `json:"skipPackages"`
	AlsoRunOnParent bool   `json:"alsoRunOnParent"`
	UploadToDb      string `json:"uploadToDb"`
}

// DeviceInfoToolConfig encapsulates the parameters that the device-info tool
// uses to collect information from the devices. Fields are as follows:
//    GetDeviceInfoBinPath: full path to tool get_device_info. If left empty,
//        Harvest expects get_device_info to be available in PATH on the target
//        device.
//    ProtoBufsOutputDir: Full path to dir where protobufs should be written.
//    UploadScript: path to python script bq_insert_pb.py.
//    Owner: Optional owner string to use in machine info. If left blank, owner
//        is read from USER env on target device. May not be "root".
//    CroutonMachine & CrosvmMachine: MachineInfoConfig specific to each target
//        device. (See above.)
type DeviceInfoToolConfig struct {
	GetDeviceInfoBinPath string              `json:"getDeviceInfoBinPath"`
	ProtoBufsOutputDir   string              `json:"protoBufsOutputDir"`
	UploadScript         string              `json:"uploadScript"`
	Owner                string              `json:"owner"`
	Machine              *MachineInfoConfig  `json:"machine"`
	Software             *SoftwareInfoConfig `json:"software"`
}

// DeviceInfoConfigParser provides support for reading and parsing device-info
// configuration from a Harvest config json file.
type DeviceInfoConfigParser struct {
	toolConfig DeviceInfoToolConfig
}

// Valid upload modes for machine-info and software-config.
const (
	UploadModeAlways = "yes"
	UploadModeNever  = "no"
)

// Returns wether mode is a valid upload-mode string.
func validateOptionUploadToDb(mode string) bool {
	for _, v := range []string{UploadModeAlways, UploadModeNever} {
		if v == mode {
			return true
		}
	}
	return false
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

	// Ensure that upload modes have valid values.
	if dp.toolConfig.Machine != nil && dp.toolConfig.Machine.UploadToDb == "" {
		dp.toolConfig.Machine.UploadToDb = UploadModeNever
	}
	if dp.toolConfig.Software != nil && dp.toolConfig.Software.UploadToDb == "" {
		dp.toolConfig.Software.UploadToDb = UploadModeNever
	}

	if dp.toolConfig.Machine != nil && !validateOptionUploadToDb(dp.toolConfig.Machine.UploadToDb) {
		return fmt.Errorf("Error in json property %s: invalid value \"%s\" for uploadToDb",
			propName, dp.toolConfig.Machine.UploadToDb)
	}
	if dp.toolConfig.Software != nil && !validateOptionUploadToDb(dp.toolConfig.Software.UploadToDb) {
		return fmt.Errorf("Error in json property %s: invalid value \"%s\" for uploadToDb",
			propName, dp.toolConfig.Software.UploadToDb)
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

// GetDeviceInfoToolConfig return the DeviceInfoToolConfig collected by the parser.
func (dp *DeviceInfoConfigParser) GetDeviceInfoToolConfig() *DeviceInfoToolConfig {
	return &dp.toolConfig
}
