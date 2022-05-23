// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"encoding/json"
	"fmt"
)

// GpuVisConfig contains the configuration parameters for GpuVisTool. They are:
//   ChromeOSWorkingDir: path to dir where scripts and data files should be stored
//       on target Chrome-OS device.
//   ChromeOSEnvVar: List of env var, in format var=value, to set on target device
//       before running the capture tools.
//   LocalGpuVisDir: Path to dir on local machine where the GpuVis binaries and
//       scripts are found.
//   FrameLoop: Apitrace frame-looping specification in the form frame1-frameN,count.
//       Apitrace will loop frame1 to frameN count times.
//   RunGpuVis: Path to GpuVis executable relative to LocalGpuVisDir. Set this to the
//       empty string to not run GpuVis automatically.
type GpuVisConfig struct {
	ChromeOSWorkingDir string   `json:"chromeOSWorkingDir"`
	ChromeOSEnvVar     []string `json:"chromeOSEnvVar"`
	LocalGpuVisDir     string   `json:"localGpuVisDir"`
	FrameLoop          string   `json:"frameLoop"`
	RunGpuVis          string   `json:"runGpuVis"`
}

// GpuVisConfigParser provides support for parsing GpuVisConfig parameters
// through the profile.ConfigPropertyHandler interface.
type GpuVisConfigParser struct {
	config GpuVisConfig
}

// NewGpuVisConfigParser creates and returns a new GpuVisConfigParser object.
func NewGpuVisConfigParser() *GpuVisConfigParser {
	return &GpuVisConfigParser{}
}

// ParseJSONData is the handler function for interface profile.ConfigPropertyHandler.
func (gp *GpuVisConfigParser) ParseJSONData(propName, jsonData string) error {
	if err := json.Unmarshal([]byte(jsonData), &gp.config); err != nil {
		return fmt.Errorf("failed to parse property %s: err = %s", propName, err.Error())
	}
	return nil
}

// GetGPUPerfConfig returns a pointer to the GpuVisConfig parameters.
func (gp *GpuVisConfigParser) GetGPUPerfConfig() *GpuVisConfig {
	return &gp.config
}
