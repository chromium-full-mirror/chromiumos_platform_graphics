// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"github.com/pkg/errors"
	"io/ioutil"
	"os/exec"
	"regexp"
	"strings"
)

// VGADevice contains the information retrieved from running lspci.
type VGADevice struct {
	BDF      string // PCI device's BDF information
	Class    string // PCI device's class name
	Name     string // PCI device's name, which also contains its vendor's name
	DeviceID string // Device ID based on its BDF value.
}

var pciRegex = regexp.MustCompile(`(\S+) (.*): (.*)`)

// GetVGADevices returns the list of vga devices shown when running lspci
func GetVGADevices() ([]VGADevice, error) {
	out, err := exec.Command("lspci").Output()
	if err != nil {
		return nil, errors.Wrap(err, "failed to run lspci")
	}
	vgaDevices := []VGADevice{}
	for _, line := range strings.Split(string(out), "\n") {
		if !strings.Contains(line, "VGA") {
			continue
		}
		matches := pciRegex.FindStringSubmatch(line)
		if matches == nil {
			continue
		}
		pciPath := fmt.Sprintf("/sys/bus/pci/devices/0000:%s/device", matches[1])
		out, err := ioutil.ReadFile(pciPath)
		if err != nil {
			return nil, errors.Wrap(err, fmt.Sprintf("failed to read %v", pciPath))
		}
		deviceID := strings.ToLower(strings.TrimSpace(string(out)))
		vgaDevices = append(vgaDevices, VGADevice{
			BDF:      matches[1],
			Class:    matches[2],
			Name:     matches[3],
			DeviceID: deviceID,
		})
	}
	return vgaDevices, nil
}
