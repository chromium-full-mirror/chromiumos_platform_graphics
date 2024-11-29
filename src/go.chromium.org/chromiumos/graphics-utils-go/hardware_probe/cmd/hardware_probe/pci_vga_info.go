// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"github.com/pkg/errors"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
)

// VGADevice contains the information retrieved from running lspci.
type VGADevice struct {
	BDF      string  // PCI device's BDF information
	Class    string  // PCI device's class name
	Name     string  // PCI device's name, which also contains its vendor's name
	VendorID string  // Vendor ID based on its BDF value. Example: AMD=0x1002, Intel=0x8086, NVIDIA=0x10de.
	DeviceID string  // Device ID based on its BDF value.
	BootVGA  bool    // True if the PCI device boot_vga is true.
	GPUInfo  GPUInfo // GPU Family name.
}

var (
	// Example output
	// 0000:61:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Lexa XT [Radeon PRO WX 3200] (rev 10)
	pciCommand = []string{"lspci", "-D"}
	pciRegex   = regexp.MustCompile(`(\S+) (.*): (.*)`)
)

func readPCIDevice(bdf string, file string) (string, error) {
	filePath := fmt.Sprintf("/sys/bus/pci/devices/%s/%s", bdf, file)
	out, err := os.ReadFile(filePath)
	if err != nil {
		return "", errors.Wrapf(err, "failed to read %v", filePath)
	}
	return strings.TrimSpace(string(out)), nil
}

// GetVGADevices returns the list of vga devices shown when running lspci
func GetVGADevices() ([]VGADevice, error) {
	// With -D, lspci will be force to omit the domain numbers.
	out, err := exec.Command(pciCommand[0], pciCommand[1:]...).Output()
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

		vendorID, err := readPCIDevice(matches[1], "vendor")
		if err != nil {
			return nil, errors.Wrap(err, "failed to read pci device vendor")
		}
		vendorID = strings.ToLower(vendorID)

		// If boot_vga is not exist, consider it false.
		bootVGA := false
		bootVGAStr, err := readPCIDevice(matches[1], "boot_vga")
		if err == nil {
			bootVGA, err = strconv.ParseBool(bootVGAStr)
			if err != nil {
				return nil, errors.Wrapf(err, "failed to parse boot_vga to bool")
			}
		}

		gpuInfo, err := mapPCINameToGPUInfo(matches[3], deviceID)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to map %v to family name", matches[3])
		}
		// Instead of using PCI vendorID as its ID, lets use vendor as its vendorID.
		// deviceID are hex values which has 0x prefix.
		gpuInfo.ID = fmt.Sprintf("%v:%v", gpuInfo.GPUVendor, deviceID[2:])

		vgaDevices = append(vgaDevices, VGADevice{
			BDF:      matches[1],
			Class:    matches[2],
			Name:     matches[3],
			VendorID: vendorID,
			DeviceID: deviceID,
			BootVGA:  bootVGA,
			GPUInfo:  gpuInfo,
		})
	}
	return vgaDevices, nil
}

// mapPCINameToGPUInfo maps the name of the PCI devices with deviceID to a family name, e.g. alderlake, ampere etc.
func mapPCINameToGPUInfo(name, deviceID string) (GPUInfo, error) {
	const (
		amdVGAString    = "Advanced Micro Devices"
		intelVGAString  = "Intel Corporation"
		nvidiaVGAString = "NVIDIA Corporation"
		virtioVGAString = "Virtio"
		vmwareVGAString = "VMWare"
	)

	// We are looking for the REAL GPU first then the VM GPU.
	if strings.Contains(name, amdVGAString) {
		amdMap := getAMDPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := amdMap[deviceID]
		if !ok {
			return GPUInfo{}, fmt.Errorf("no matching device id (%v) in AMD pci id map, please update src/platform/graphics/.../hardware_probe/.../amd_pci_ids.go", deviceID)
		}
		return GPUInfo{Family: gpuName, GPUVendor: vendorAMD}, nil
	} else if strings.Contains(name, intelVGAString) {
		intelMap := getIntelPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := intelMap[deviceID]
		if !ok {
			return GPUInfo{}, fmt.Errorf("no matching device id (%v) in Intel pci id map, please update src/platform/graphics/.../hardware_probe/.../intel_pci_ids.go", deviceID)
		}
		return GPUInfo{Family: gpuName, GPUVendor: vendorIntel}, nil
	} else if strings.Contains(name, nvidiaVGAString) {
		nvidiaMap := getNvidiaPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := nvidiaMap[deviceID]
		if !ok {
			return GPUInfo{}, fmt.Errorf("no matching device id (%v) in Nvidia pci id map, please update src/platform/graphics/.../hardware_probe/.../nvidia_pci_ids.go", deviceID)
		}
		return GPUInfo{Family: gpuName, GPUVendor: vendorNvidia}, nil
	} else if strings.Contains(name, virtioVGAString) {
		virtioMap := getVMPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := virtioMap[deviceID]
		if !ok {
			return GPUInfo{}, fmt.Errorf("no matching device id (%v) in VM pci id map, please update src/platform/graphics/.../hardware_probe/.../vm_pci_ids.go", deviceID)
		}
		return GPUInfo{Family: gpuName, GPUVendor: vendorVirtio}, nil
	} else if strings.Contains(name, vmwareVGAString) {
		virtioMap := getVMPCIIDMap()
		deviceID := strings.ToLower(deviceID)
		gpuName, ok := virtioMap[deviceID]
		if !ok {
			return GPUInfo{}, fmt.Errorf("no matching device id (%v) in VM pci id map, please update src/platform/graphics/.../hardware_probe/.../vm_pci_ids.go", deviceID)
		}
		return GPUInfo{Family: gpuName, GPUVendor: vendorVmware}, nil
	}
	return GPUInfo{}, fmt.Errorf("unrecognized PCI device name: %v", name)
}
