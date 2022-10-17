// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
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

// MarshalJSON marshals the enum as a quoted json string.
func (s CPUSOCFamily) MarshalJSON() ([]byte, error) {
	buffer := bytes.NewBufferString(`"`)
	buffer.WriteString(s.String())
	buffer.WriteString(`"`)
	return buffer.Bytes(), nil
}

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

// getARMSOCFamilyFromCompatible returns the ARM SOC we're running on and its name based on 'compatible' property of the base node of devicetree.
func getARMSOCFamilyFromCompatible() (CPUSOCFamily, string, error) {
	out, err := ioutil.ReadFile("/sys/firmware/devicetree/base/compatible")
	if err != nil {
		return socUnknown, "", errors.Wrap(err, "failed to read compatible file")
	}
	compatibles := strings.Split(string(out), "\000")
	if match := listGrep(compatibles, `^qcom,(\S+)`); match != nil {
		return socQualcomm, match[1], nil
	} else if match = listGrep(compatibles, `^mediatek,(\S+)`); match != nil {
		return socMediaTek, match[1], nil
	}
	return socUnknown, "", fmt.Errorf("Failed to determine ARM SOC from compatible: %v", compatibles)
}

func getCPUSOCFamily() (CPUSOCFamily, error) {
	cpuArch, err := getCPUArch()
	if err != nil {
		return socUnknown, errors.Wrap(err, "failed to get cpu arch type")
	}
	if cpuArch == archArm {
		socFamily, _, err := getARMSOCFamilyFromCompatible()
		return socFamily, err
	}
	if cpuArch == archX86 || cpuArch == archX64 {
		// Use cpuinfo to figure out AMD
		out, err := ioutil.ReadFile("/proc/cpuinfo")
		if err != nil {
			return socUnknown, errors.Wrap(err, "failed to read /proc/cpuinfo")
		}
		if listGrep(strings.Split(string(out), "\n"), "^vendor_id.*:.*AMD") != nil {
			return socAMD, nil
		}
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

type probeResult struct {
	CPUFamily  CPUSOCFamily `json:"CPU_SOC_Family"`
	GPUInfos   []GPUInfo    `json:"GPU_Family"`
	VGADevices []VGADevice  `json:"VGA_Devices"`
}

func fatal(format string, args ...interface{}) {
	fmt.Printf(format+"\n", args...)
	os.Exit(1)
}

func log(format string, args ...interface{}) {
	fmt.Printf(format+"\n", args...)
}

func main() {
	gpuQuery := flag.Bool("gpu-family", false, "Output the GPU family to stdout")
	gpuVendorQuery := flag.Bool("gpu-vendor", false, "Output the GPU vendor to stdout")
	cpuQuery := flag.Bool("cpu-soc-family", false, "Output the CPU family to stdout")
	outputFile := flag.String("output", "", "Output result to file in json format")
	flag.Parse()

	result := probeResult{}
	cpuSocFamily, err := getCPUSOCFamily()
	if err != nil {
		fatal("Failed to determine CPU SOC family: %v", err)
	}
	result.CPUFamily = cpuSocFamily

	gpuInfos, err := getGPUInfos()
	if err != nil {
		fatal("Failed to detemine GPU: %v", err)
	}
	result.GPUInfos = gpuInfos

	vgaDevices, err := GetVGADevices()
	if err != nil {
		fatal("Failed to determine VGA device: %v", err)
	}
	result.VGADevices = vgaDevices

	if *gpuQuery {
		for _, gpu := range result.GPUInfos {
			log("GPU_Family: %v", gpu.Family)
		}
	}
	if *gpuVendorQuery {
		for _, gpu := range result.GPUInfos {
			log("GPU_Vendor: %v", gpu.GPUVendor.String())
		}
	}
	if *cpuQuery {
		log("CPU_SOC_Family: %v", result.CPUFamily)
	}

	// Output JSON file
	if len(*outputFile) != 0 {
		b, err := json.Marshal(result)
		if err != nil {
			fatal("Failed to marshal result: %v", result)
		}
		log("Output result to %v", *outputFile)
		if err := os.WriteFile(*outputFile, b, 0755); err != nil {
			fatal("Failed to write to %v: %v", *outputFile, err)
		}
	}
}
