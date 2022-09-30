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
	"sort"
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
	socIntel
	socQualcomm
	socMediaTek
)

func (s CPUSOCFamily) String() string {
	switch s {
	case socIntel:
		return "intel"
	case socAMD:
		return "amd"
	case socQualcomm:
		return "qualcomm"
	case socMediaTek:
		return "mediatek"
	default:
		return "unknown"
	}
}

// GPUFamily is type of GPU family.
type GPUFamily string

// listGrep returns the matches of the first items in list matches the specific regex pattern.
func listGrep(list []string, query string) []string {
	re := regexp.MustCompile(query)
	for _, item := range list {
		if match := re.FindStringSubmatch(item); match != nil {
			return match
		}
	}
	return nil
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
	if listGrep(compatibles, "^qcom,") != nil {
		return socQualcomm, nil
	} else if listGrep(compatibles, "^mediatek,") != nil {
		return socMediaTek, nil
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
	if listGrep(strings.Split(string(out), "\n"), "^vendor_id.*:.*AMD") != nil {
		return socAMD, nil
	}

	cpuArch, err := getCPUArch()
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to get cpu arch type")
	}
	if cpuArch == archArm {
		return getARMSOCFamily()
	}
	if cpuArch == archX86 || cpuArch == archX64 {
		// AMD is determined earlier in this function.
		return socIntel, nil
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

func getMediaTekSoc() (string, error) {
	out, err := ioutil.ReadFile("/sys/firmware/devicetree/base/compatible")
	if err != nil {
		return "", errors.Wrap(err, "failed to read compatible file")
	}
	compatibles := strings.Split(string(out), "\000")
	if match := listGrep(compatibles, "^mediatek,(.*)"); match != nil {
		return match[1], nil
	}
	return "", errors.Errorf("failed to find mediatek in compatible file: %v", compatibles)
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
	if socFamily == socMediaTek {
		mediaTekSoc, err := getMediaTekSoc()
		if err == nil && mediaTekSoc == "mt8173" {
			// MT8173 doesn't have mali, instead it have rogue driver.
			return "rogue", nil
		}
	}

	// For AMD and intel, check the pci_id_map for their respecitive GPU.
	const (
		amdVGAString    = "Advanced Micro Devices"
		intelVGAString  = "Intel Corporation"
		nvidiaVGAString = "NVIDIA Corporation"
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
	if strings.Contains(vgaDevices[0].Name, nvidiaVGAString) {
		nvidiaMap := getNvidiaPCIIDMap()
		deviceID := strings.ToLower(vgaDevices[0].DeviceID)
		gpuName, ok := nvidiaMap[deviceID]
		if !ok {
			return "", fmt.Errorf("no matching device id (%v) in Nvidia pci id map, please update src/platform/graphics/.../hardware_probe/.../nvidia_pci_ids.go", deviceID)
		}
		return gpuName, nil
	}
	return "", fmt.Errorf("failed to determine GPU from vgaDevices: %q", vgaDevices[0])
}

func main() {
	gpuQuery := flag.Bool("gpu-family", false, "Output the GPU family")
	cpuQuery := flag.Bool("cpu-soc-family", false, "Output the CPU family")
	flag.Parse()

	result := map[string]string{}
	if *gpuQuery {
		gpuFamily, err := getGPUFamily()
		if err != nil {
			fmt.Printf("Failed to detemine GPU: %v\n", err)
		} else {
			result["GPU_Family"] = string(gpuFamily)
		}
	}
	if *cpuQuery {
		cpuSocFamily, err := getCPUSOCFamily()
		if err != nil {
			fmt.Printf("Failed to determine CPU SOC family: %v\n", err)
		} else {
			result["CPU_SOC_Family"] = cpuSocFamily.String()
		}
	}

	if len(result) != 0 {
		// Sort the keys so the order of output is stable.
		keys := make([]string, 0, len(result))
		for k := range result {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			fmt.Printf("%s: %s\n", k, result[k])
		}
	} else {
		// Simply returns the gpuFamily.
		gpuFamily, err := getGPUFamily()
		if err != nil {
			fmt.Printf("Failed to determine GPU: %v\n", err)
		}
		fmt.Printf("%v\n", gpuFamily)
	}
}
