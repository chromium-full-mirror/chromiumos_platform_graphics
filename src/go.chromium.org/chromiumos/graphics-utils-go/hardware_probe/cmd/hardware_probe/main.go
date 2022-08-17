// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"github.com/pkg/errors"
	"io/ioutil"
	"os"
	"os/exec"
	"regexp"
	"strings"
)

// CPUArch is type of CPU architecture.
type CPUArch int

const (
	archUnknown CPUArch = iota
	archArm
	archX64
	archX86
)

// CPUSOCFamily is type of CPU SOC family.
type CPUSOCFamily int

const (
	socUnknown CPUSOCFamily = iota
	socAMD
	socX64
	socX86
	socQualcomm
	socMT8173
)

// GPUFamily is type of GPU family.
type GPUFamily string

// listGrep returns true if items in list matches specific regex pattern.
func listGrep(list []string, query string) bool {
	re := regexp.MustCompile(query)
	for _, item := range list {
		if match := re.FindStringSubmatch(item); match != nil {
			return true
		}
	}
	return false
}
func getCPUArch() (CPUArch, error) {
	var archPrefixes = map[CPUArch][]string{
		archArm: {"aarch64", "arm"},
		archX64: {"x86_64"},
		archX86: {"i386"},
	}

	// Using 'uname -m' should be very portable way to do this since the format is pretty standard.
	out, err := exec.Command("uname", "-m").Output()
	if err != nil {
		return archUnknown, errors.Wrap(err, "failed to run uname -m")
	}
	machineName := string(out)
	for archName, archPrefixes := range archPrefixes {
		for _, prefix := range archPrefixes {
			if strings.HasPrefix(machineName, prefix) {
				return archName, nil
			}
		}
	}
	return archUnknown, fmt.Errorf("Unsupported machine type %s", machineName)
}

// getARMSOCFamilyFromCompatible determines the ARM SOC we're running on based on 'compatible' property of the base node of devicetree.
func getARMSOCFamilyFromCompatible() (CPUSOCFamily, error) {
	out, err := ioutil.ReadFile("/sys/firmware/devicetree/base/compatible")
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to read compatible file")
	}
	compatibles := strings.Split(string(out), "\000")
	if listGrep(compatibles, "^qcom,") {
		return socQualcomm, nil
	} else if listGrep(compatibles, "^mediatek,mt8173") {
		return socMT8173, nil
	}
	return socUnknown, fmt.Errorf("Failed to determine ARM SOC from compatible: %v", compatibles)
}

func getARMSOCFamily() (CPUSOCFamily, error) {
	return getARMSOCFamilyFromCompatible()
}

func getCPUSOCFamily() (CPUSOCFamily, error) {

	// Use cpuinfo to figure out AMD
	out, err := ioutil.ReadFile("/proc/cpuinfo")
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to read /proc/cpuinfo")
	}
	if listGrep(strings.Split(string(out), "\n"), "^vendor_id.*:.*AMD") {
		return socAMD, nil
	}

	cpuArch, err := getCPUArch()
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to get cpu arch type")
	}
	if cpuArch == archArm {
		return getARMSOCFamily()
	}
	if cpuArch == archX64 {
		return socX64, nil
	}
	if cpuArch == archX86 {
		return socX86, nil
	}
	return socUnknown, fmt.Errorf("failed to determine soc")
}

// hasMaliGPUEnabled checks if mali driver is in the device.
func hasMaliGPUEnabled() (bool, error) {
	if _, err := os.Stat("/dev/mali0"); err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return false, nil
		}
		return false, errors.Wrap(err, "failed to determine if the device has mali driver")
	}
	return true, nil
}

func getWaffleInfo() (string, error) {
	getUseFlags := func() ([]string, error) {
		flags := []string{}
		out, err := ioutil.ReadFile("/etc/ui_use_flags.txt")
		if err != nil {
			return nil, errors.Wrap(err, "failed to read ui_use_flags")
		}
		// Remove all comment
		for _, line := range strings.Split(string(out), "\n") {
			flagBeforeComment := strings.TrimSpace(strings.Split(line, "#")[0])
			if len(flagBeforeComment) == 0 {
				continue
			}
			flags = append(flags, flagBeforeComment)
		}
		return flags, nil
	}
	getGraphicsAPI := func() (string, error) {
		useFlags, err := getUseFlags()
		if err != nil {
			return "", errors.Wrap(err, "failed to get use flags")
		}
		for _, flag := range useFlags {
			if "opengles" == flag {
				return "gles2", nil
			}
		}
		return "gl", nil
	}
	graphicsAPI, err := getGraphicsAPI()
	if err != nil {
		return "", errors.Wrap(err, "failed to get graphcis api")
	}
	out, err := exec.Command("wflinfo", "-p", "null", "-a", graphicsAPI).Output()
	if err != nil {
		return "", errors.Wrap(err, "failed to run wflinfo")
	}
	return string(out), nil
}

// getGPUFamily returns the GPU family name for the host.
func getGPUFamily() (GPUFamily, error) {
	// Check for mali
	if hasMali, err := hasMaliGPUEnabled(); err != nil {
		return "", errors.Wrap(err, "failed to determine Mali")
	} else if hasMali {
		wflinfo, err := getWaffleInfo()
		if err != nil {
			return "", errors.Wrap(err, "failed to get waffle info")
		}
		maliReg := regexp.MustCompile(`OpenGL renderer string: (Mali-\w+)`)
		matches := maliReg.FindStringSubmatch(wflinfo)
		if matches == nil {
			return "mali-unrecognized", nil
		}
		return GPUFamily(strings.ToLower(matches[1])), nil
	}

	// Check for qualcomm, rogue
	socFamily, err := getCPUSOCFamily()
	if err != nil {
		return "", errors.Wrap(err, "failed to determine CPU SOC family")
	}
	if socFamily == socQualcomm {
		return "qualcomm", nil
	}
	if socFamily == socMT8173 {
		// MT8173 doesn't have mali, instead it have rogue driver.
		return "rogue", nil
	}

	// For AMD and intel, check the pci_id_map for their respecitive GPU.
	const (
		amdVGAString   = "Advanced Micro Devices"
		intelVGAString = "Intel Corporation"
	)
	vgaDevices, err := GetVGADevices()
	if err != nil {
		return "", errors.Wrap(err, "failed to get VGA info")
	}
	if len(vgaDevices) > 1 {
		return "", fmt.Errorf("multiple VGA devices detected: %v", vgaDevices)
	}

	if strings.Contains(vgaDevices[0].Name, amdVGAString) {
		amdMap := getAMDPCIIDMap()
		deviceID := strings.ToLower(vgaDevices[0].DeviceID)
		gpuName, ok := amdMap[deviceID]
		if !ok {
			return "", fmt.Errorf("no matching device id (%v) in AMD pci id map, please update src/platform/graphics/.../hardware_probe/.../amd_pci_ids.go", deviceID)
		}
		return gpuName, nil
	}
	if strings.Contains(vgaDevices[0].Name, intelVGAString) {
		intelMap := getIntelPCIIDMap()
		deviceID := strings.ToLower(vgaDevices[0].DeviceID)
		gpuName, ok := intelMap[deviceID]
		if !ok {
			return "", fmt.Errorf("no matching device id (%v) in Intel pci id map", deviceID)
		}
		return gpuName, nil
	}
	return "", fmt.Errorf("failed to determine GPU from vgaDevices: %q", vgaDevices[0])
}

func main() {
	gpuQuery := flag.Bool("gpu-family", true, "Output the GPU family")
	flag.Parse()

	if *gpuQuery {
		gpuFamily, err := getGPUFamily()
		if err != nil {
			fmt.Printf("Failed to detemine GPU: %v\n", err)
		}
		fmt.Printf("%v\n", gpuFamily)
	}
}
