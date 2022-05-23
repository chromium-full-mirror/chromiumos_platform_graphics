// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"encoding/json"
	"fmt"
	"strings"

	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/profile"
)

// TargetDevice encloses all the information needed for identifying a device
// and reaching it through SSH running remote commands on it.
type TargetDevice struct {
	ProfilerConfig *profile.ProfilerConfigRecord
	DeviceConfig   *profile.DeviceConfigRecord
}

// Type harvestConfigRecord encapsulates the configuration parameters for Harvest.
type harvestConfigRecord struct {
	Traces            []string `json:"traces"`
	TraceCacheDir     string   `json:"traceCacheDir"`
	KeepTracesInCache bool     `json:"keepTracesInCache"`
	ProfileBinPath    string   `json:"profileBinPath"`
	Delay             int      `json:"delay"`
}

// HarvestConfigParser provides support for reading and parsing Harvest
// configuration json files. That includes the parameters specific to Harvest as
// well as the profiler configurations that Harvest uses when launching the
// companion tool Profile for running traces on target devices.
type HarvestConfigParser struct {
	jsonParser    *profile.JSONConfigParser
	harvestConfig harvestConfigRecord
	targetDevice1 TargetDevice
	targetDevice2 TargetDevice
}

// Struct parser, and the function ParseJSONData below, provides a concrete
// implementation of the json-config ConfigPropertyHandler interface.
type parser struct {
	handler func(propName, jsonData string) error
}

// ParseJSONData is parser's handler function for interface ConfigPropertyHandler.
func (p *parser) ParseJSONData(propName, jsonData string) error {
	return p.handler(propName, jsonData)
}

// CreateHarvestConfigParser creates and returns a HarvestConfigParser instance.
func CreateHarvestConfigParser() *HarvestConfigParser {
	hc := HarvestConfigParser{
		jsonParser: profile.CreateJSONConfigParser(),
	}

	// Define and add a handler for property "Harvest".
	var harvestParser = parser{
		handler: func(propName, jsonData string) error {
			return hc.parseHarvestParams(jsonData)
		},
	}
	hc.jsonParser.AddHandler("Harvest", &harvestParser)

	// Define and add a handler for property "CrostiniProfilerConfig".
	var legacyCrostiniParser = parser{
		handler: func(propName, jsonData string) error {
			return hc.parseLegacyProfilerConfig(jsonData, "crostini", &hc.targetDevice1)
		},
	}
	hc.jsonParser.AddHandler("CrostiniProfilerConfig", &legacyCrostiniParser)

	// Define and add a handler for property "CroutonProfilerConfig".
	var legacyCroutonParser = parser{
		handler: func(propName, jsonData string) error {
			return hc.parseLegacyProfilerConfig(jsonData, "crouton", &hc.targetDevice2)
		},
	}
	hc.jsonParser.AddHandler("CroutonProfilerConfig", &legacyCroutonParser)

	// Add a handlers for properties "TargetDevice1" and "TargetDevice2".
	var targetDevice1Parser = parser{
		handler: func(propName, jsonData string) error {
			return hc.parseTargetDeviceConfig(propName, jsonData, &hc.targetDevice1)
		},
	}
	hc.jsonParser.AddHandler("TargetDevice1", &targetDevice1Parser)

	var targetDevice2Parser = parser{
		handler: func(propName, jsonData string) error {
			return hc.parseTargetDeviceConfig(propName, jsonData, &hc.targetDevice2)
		},
	}
	hc.jsonParser.AddHandler("TargetDevice2", &targetDevice2Parser)

	return &hc
}

// AddHandler adds handler for the top-level config property with name propName.
func (hc *HarvestConfigParser) AddHandler(
	propName string, handler profile.ConfigPropertyHandler) error {

	return hc.jsonParser.AddHandler(propName, handler)
}

// OpenJSONFile opens a json file and get ready for processing it. If the file
// doesn't contain any properties that this class can parse, and error is
// returned.
func (hc *HarvestConfigParser) OpenJSONFile(jsonFilepath string) error {
	return hc.jsonParser.OpenJSONConfigFile(jsonFilepath)
}

// Process parses the file previously opened with OpenJSONFile.
func (hc *HarvestConfigParser) Process() error {
	return hc.jsonParser.Process()
}

// GetTraces returns the list of traces found in the configuration as an
// array of file names,
func (hc *HarvestConfigParser) GetTraces() []string {
	return hc.harvestConfig.Traces
}

// GetTraceCacheDir returns the path the the directory where traces are cached
// as a string.
func (hc *HarvestConfigParser) GetTraceCacheDir() string {
	return hc.harvestConfig.TraceCacheDir
}

// GetProfilerBinPath returns the full path the the profiler executable as a string.
func (hc *HarvestConfigParser) GetProfilerBinPath() string {
	return hc.harvestConfig.ProfileBinPath
}

// ShouldKeepTraceAfterUse returns whether the options to keep the traces in the
// cache is true.
func (hc *HarvestConfigParser) ShouldKeepTraceAfterUse() bool {
	return hc.harvestConfig.KeepTracesInCache
}

// GetDelay returns the delay between profile runs.
func (hc *HarvestConfigParser) GetDelay() int {
	return hc.harvestConfig.Delay
}

// GetTargetDeviceConfig1 returns the target device for config TargetDevice1.
// May be nil.
func (hc *HarvestConfigParser) GetTargetDeviceConfig1() *TargetDevice {
	if hc.targetDevice1.ProfilerConfig != nil && hc.targetDevice1.DeviceConfig != nil {
		return &hc.targetDevice1
	}
	return nil
}

// GetTargetDeviceConfig2 returns the target device for config TargetDevice2.
// May be nil.
func (hc *HarvestConfigParser) GetTargetDeviceConfig2() *TargetDevice {
	if hc.targetDevice2.ProfilerConfig != nil && hc.targetDevice2.DeviceConfig != nil {
		return &hc.targetDevice2
	}
	return nil
}

// Parse the Harvest config parameters from json data into harvestConfig.
func (hc *HarvestConfigParser) parseHarvestParams(jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &hc.harvestConfig); err != nil {
		return err
	}

	return nil
}

// Parse the config properties for legacy configurations CrostiniProfilerConfig and
// CroutonProfilerConfig. CrostiniProfilerConfig is automatically assigned the
// exec-env value of "crostini" if one is not defined in the config file. Likewise,
// CroutonProfilerConfig is assigned exec env value "crouton".
func (hc *HarvestConfigParser) parseLegacyProfilerConfig(
	jsonData, targetEnv string, device *TargetDevice) error {

	// Create a profile parser to parse this property. Beware, for includes with
	// relatives paths to continue working as expected, it must use the base path
	// from the current parser.
	profileParser := profile.CreateProfilerConfigParser()
	profileParser.SetBasePath(hc.jsonParser.GetBasePath())

	err := profileParser.ParseJSONFromReader(strings.NewReader(jsonData))
	if err != nil {
		return err
	}

	device.ProfilerConfig = profileParser.GetProfilerConfig()
	device.DeviceConfig = profileParser.GetDeviceConfig()

	if device.DeviceConfig == nil {
		device.DeviceConfig = &profile.DeviceConfigRecord{
			Name:    "",
			ExecEnv: targetEnv,
		}
	} else if device.DeviceConfig.ExecEnv == "" {
		device.DeviceConfig.ExecEnv = targetEnv
	}

	return nil
}

// Parse the config properties for one of the target devices (properties
// TargetDevice1 and TargetDevice2).
func (hc *HarvestConfigParser) parseTargetDeviceConfig(
	propName, jsonData string, device *TargetDevice) error {

	// Create a profile parser to parse this property. Beware, for includes with
	// relatives paths to continue working as expected, it must use the base path
	// from the current parser.
	profileParser := profile.CreateProfilerConfigParser()
	profileParser.SetBasePath(hc.jsonParser.GetBasePath())

	err := profileParser.ParseJSONFromReader(strings.NewReader(jsonData))
	if err != nil {
		return fmt.Errorf("could not parse %s: err = %s", propName, err.Error())
	}

	device.ProfilerConfig = profileParser.GetProfilerConfig()
	device.DeviceConfig = profileParser.GetDeviceConfig()

	if device.DeviceConfig == nil {
		return fmt.Errorf("target-device config %s has no device info", propName)
	}
	return nil
}
