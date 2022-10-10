// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"github.com/pkg/errors"
	"io/ioutil"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
)

// VGADevice contains the information retrieved from running lspci.
type VGADevice struct {
	BDF       string    // PCI device's BDF information
	Class     string    // PCI device's class name
	Name      string    // PCI device's name, which also contains its vendor's name
	DeviceID  string    // Device ID based on its BDF value.
	BootVGA   bool      // True if the PCI device boot_vga is true.
	GPUFamily GPUFamily // GPU Family name.
}

var pciRegex = regexp.MustCompile(`(\S+) (.*): (.*)`)

func readPCIDevice(bdf string, file string) (string, error) {
	filePath := fmt.Sprintf("/sys/bus/pci/devices/0000:%s/%s", bdf, file)
	out, err := ioutil.ReadFile(filePath)
	if err != nil {
		return "", errors.Wrapf(err, "failed to read %v", filePath)
	}
	return strings.TrimSpace(string(out)), nil
}

// GetVGADevices returns the list of vga devices shown when running lspci
func GetVGADevices() ([]VGADevice, error) {
	out, err := exec.Command("lspci").Output()
	if err != nil {
		return nil, errors.Wrap(err, "failed to run lspci")
	}
	vgaDevices := []VGADevice{}
	for _, line := range strings.Split(string(out), "\n") {
		if !strings.Contains(line, "VGA compatible controller") && !strings.Contains(line, "3D controller") {
			continue
		}
		matches := pciRegex.FindStringSubmatch(line)
		if matches == nil {
			continue
		}

		deviceID, err := readPCIDevice(matches[1], "device")
		if err != nil {
			return nil, errors.Wrap(err, "failed to read pci device")
		}
		deviceID = strings.ToLower(deviceID)

		// If boot_vga is not exist, consider it false.
		bootVGA := false
		bootVGAStr, err := readPCIDevice(matches[1], "boot_vga")
		if err == nil {
			bootVGA, err = strconv.ParseBool(bootVGAStr)
			if err != nil {
				return nil, errors.Wrapf(err, "failed to parse boot_vga to bool")
			}
		}

		gpuFamily, err := MapGPUFamilyName(matches[3], deviceID)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to map %v to family name", matches[3])
		}

		vgaDevices = append(vgaDevices, VGADevice{
			BDF:       matches[1],
			Class:     matches[2],
			Name:      matches[3],
			DeviceID:  deviceID,
			BootVGA:   bootVGA,
			GPUFamily: gpuFamily,
		})
	}
	return vgaDevices, nil
}

// MapGPUFamilyName maps the name of the PCI devices with deviceID to a family name, e.g. alderlake, ampere etc.
func MapGPUFamilyName(name, deviceID string) (GPUFamily, error) {
	const (
		amdVGAString    = "Advanced Micro Devices"
		intelVGAString  = "Intel Corporation"
		nvidiaVGAString = "NVIDIA Corporation"
	)
	if strings.Contains(name, amdVGAString) {
		amdMap := getAMDPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := amdMap[deviceID]
		if !ok {
			return "", fmt.Errorf("no matching device id (%v) in AMD pci id map, please update src/platform/graphics/.../hardware_probe/.../amd_pci_ids.go", deviceID)
		}
		return gpuName, nil
	} else if strings.Contains(name, intelVGAString) {
		intelMap := getIntelPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := intelMap[deviceID]
		if !ok {
			return "", fmt.Errorf("no matching device id (%v) in Intel pci id map", deviceID)
		}
		return gpuName, nil
	} else if strings.Contains(name, nvidiaVGAString) {
		nvidiaMap := getNvidiaPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := nvidiaMap[deviceID]
		if !ok {
			return "", fmt.Errorf("no matching device id (%v) in Nvidia pci id map, please update src/platform/graphics/.../hardware_probe/.../nvidia_pci_ids.go", deviceID)
		}
		return gpuName, nil
	}
	return "", fmt.Errorf("Unrecognized PCI device name: %v", name)
}
