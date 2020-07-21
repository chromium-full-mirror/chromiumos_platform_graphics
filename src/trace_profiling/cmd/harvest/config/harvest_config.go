// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"encoding/json"
	"strings"
	"trace_profiling/cmd/profile/profile"
)

// Type harvestConfigRecord encapsulates the configuration parameters for Harvest.
type harvestConfigRecord struct {
	Traces            []string `json:"traces"`
	TraceCacheDir     string   `json:"traceCacheDir"`
	KeepTracesInCache bool     `json:"keepTracesInCache"`
	ProfileBinPath    string   `json:"profileBinPath"`
}

// HarvestConfigParser provides support for reading and parsing Harvest
// configuration json files. That includes the parameters specific to Harvest as
// well as the profiler configurations that Harvest uses when launching the
// companion tool Profile for running traces on Crostini and Crouton devices.
type HarvestConfigParser struct {
	jsonParser             *profile.JSONConfigParser
	harvestConfig          harvestConfigRecord
	crostiniProfilerConfig *profile.ProfilerConfigRecord
	croutonProfilerConfig  *profile.ProfilerConfigRecord
}

// Type parser, and the function ParseJSONData below, provides a concrete
// implementation of the json-config ConfigPropertyHandler interface.
type parser struct {
	handler func(jsonData string) error
}

// ParseJSONData is parser's handler function for interface ConfigPropertyHandler.
func (p *parser) ParseJSONData(jsonData string) error {
	return p.handler(jsonData)
}

// CreateHarvestConfigParser creates and returns a HarvestConfigParser instance.
func CreateHarvestConfigParser() *HarvestConfigParser {
	hc := HarvestConfigParser{
		jsonParser: profile.CreateJSONConfigParser(),
	}

	// Define and add a handler for property "Harvest".
	var harvestParser = parser{
		handler: func(jsonData string) error {
			return hc.parseHarvestParams(jsonData)
		},
	}
	hc.jsonParser.AddHandler("Harvest", &harvestParser)

	// Define and add a handler for property "CrostiniProfilerConfig".
	var crostiniParser = parser{
		handler: func(jsonData string) error {
			var err error
			hc.crostiniProfilerConfig, err = hc.parseProfilerConfig(jsonData)
			return err
		},
	}
	hc.jsonParser.AddHandler("CrostiniProfilerConfig", &crostiniParser)

	// Define and add a handler for property "CroutonProfilerConfig".
	var croutonParser = parser{
		handler: func(jsonData string) error {
			var err error
			hc.croutonProfilerConfig, err = hc.parseProfilerConfig(jsonData)
			return err
		},
	}
	hc.jsonParser.AddHandler("CroutonProfilerConfig", &croutonParser)
	return &hc
}

// AddHandler adds handler for the top-level config property with name fieldName.
func (hc *HarvestConfigParser) AddHandler(
	fieldName string, handler profile.ConfigPropertyHandler) error {

	return hc.jsonParser.AddHandler(fieldName, handler)
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

// GetCrostiniProfilerConfig returns the profiler config for Crostini. May
// return nil if no such configuration is found in the json data.
func (hc *HarvestConfigParser) GetCrostiniProfilerConfig() *profile.ProfilerConfigRecord {
	return hc.crostiniProfilerConfig
}

// GetCroutonProfilerConfig returns the profiler config for Crouton. May return
// nil if no such configuration is found in the json data.
func (hc *HarvestConfigParser) GetCroutonProfilerConfig() *profile.ProfilerConfigRecord {
	return hc.croutonProfilerConfig
}

// Parse the Harvest config parameters from json data into harvestConfig.
func (hc *HarvestConfigParser) parseHarvestParams(jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &hc.harvestConfig); err != nil {
		return err
	}

	return nil
}

// Parse and return profiler configuration parameters from json data.
func (hc *HarvestConfigParser) parseProfilerConfig(
	jsonData string) (*profile.ProfilerConfigRecord, error) {

	profileParser := profile.CreateProfilerConfigParser()
	err := profileParser.ParseJSONFromReader(strings.NewReader(jsonData))
	return profileParser.GetProfilerConfig(), err
}
