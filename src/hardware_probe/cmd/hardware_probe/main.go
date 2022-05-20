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
)

// GPUFamily is type of GPU family.
type GPUFamily string

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

func getCPUSOCFamily() (CPUSOCFamily, error) {
	// listGrep returns true if items in list matches specific regex pattern.
	listGrep := func(list []string, re *regexp.Regexp) bool {
		for _, item := range list {
			if match := re.FindStringSubmatch(item); match != nil {
				return true
			}
		}
		return false
	}

	// Use cpuinfo to figure out AMD
	out, err := ioutil.ReadFile("/proc/cpuinfo")
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to read /proc/cpuinfo")
	}
	re := regexp.MustCompile(`^vendor_id.*:.*AMD`)
	if listGrep(strings.Split(string(out), "\n"), re) {
		return socAMD, nil
	}

	cpuArch, err := getCPUArch()
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to get cpu arch type")
	}
	if cpuArch == archArm {
		// TODO: Figure out arm SOC family name
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
	hasMali, _ := hasMaliGPUEnabled()
	// TODO: Add some ARM soc here once ARM soc family detection works.
	if hasMali {
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
	// TODO: Use CPUSOCFamily information to determine GPU with ARM CPU, e.g. qualcomm.

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
		// TODO: figure out AMD, we need similar file as autotest's amd_pci_ids.json file.
	}
	if strings.Contains(vgaDevices[0].Name, intelVGAString) {
		intelMap := getIntelPCIIDMap()
		deviceID := vgaDevices[0].DeviceID
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
