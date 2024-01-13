// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"github.com/pkg/errors"
	"io/ioutil"
	"os"
	"os/exec"
	"regexp"
	"strconv"
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
	socRockchip
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
	case socRockchip:
		return "rockchip"
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
	} else if match = listGrep(compatibles, `^rockchip,(\S+)`); match != nil {
		return socRockchip, match[1], nil
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

// Memory is the size of the system RAM in Gb.
type Memory int64

func getMemory() (Memory, error) {
	memoryBytes, err := func() (int64, error) {
		b, err := ioutil.ReadFile("/proc/meminfo")
		if err != nil {
			return 0, err
		}
		memReg := regexp.MustCompile(`MemTotal:\s+(\d+)\s+(\S+)`)
		sc := bufio.NewScanner(bytes.NewReader(b))
		for sc.Scan() {
			text := sc.Text()
			match := memReg.FindStringSubmatch(text)
			if match == nil {
				continue
			}
			if match[2] != "kB" {
				return 0, errors.Errorf("expect MemTotal in kB, got %v", match[2])
			}
			val, err := strconv.ParseInt(match[1], 10, 64)
			if err != nil {
				return 0, errors.Wrapf(err, "failed to parse %v", text)
			}
			// meminfo reported kB is 2^10 bytes not 10^3.
			return val << 10, nil
		}
		return 0, fmt.Errorf("MemTotal not found; got: %v", string(b))
	}()
	if err != nil {
		return 0, errors.Wrap(err, "failed to get memory size")
	}
	// memoryBytes reported by /proc/meminfo is less than actual installed memory.
	// We report it by rounding to the nearest Gb.
	return Memory((memoryBytes + 1<<29) >> 30), nil
}

type Disk struct {
	Name   string `json:"name"`
	Size   int64  `json:"size"`    // Size is in bytes.
	SizeGb int64  `json:"size_gb"` // SizeGb is in bytes_gb.
}

// getLargestDisk returns the largest size disk captured by lsblk tool.
func getLargestDisk() (Disk, error) {
	b, err := exec.Command("lsblk", "-J", "-b").Output()
	if err != nil {
		return Disk{}, err
	}
	r, err := parseLsblk(b)
	if err != nil {
		return Disk{}, err
	}
	largest := blockDevices{}
	for _, blockDevice := range r.BlockDevices {
		if blockDevice.Type != "disk" {
			continue
		}
		if largest.Size < blockDevice.Size {
			largest = blockDevice
		}
	}
	return Disk{
		Name:   largest.Name,
		Size:   largest.Size,
		SizeGb: largest.Size / 1_000_000_000,
	}, nil
}

type hardwareResult struct {
	CPUFamily  CPUSOCFamily `json:"CPU_SOC_Family"`
	Disk       Disk         `json:"Disk"`
	GPUInfos   []GPUInfo    `json:"GPU_Family"`
	Memory     Memory       `json:"Memory"`
	VGADevices []VGADevice  `json:"VGA_Devices,omitempty"`
}

type softwareResult struct {
	OpenGLES         *Version        `json:"OpenGLES,omitempty"`
	OpenGLESPackage  *PortagePackage `json:"OpenGLESPackage,omitempty"`
	VulkanAPIVersion *Version        `json:"VulkanAPIVersion,omitempty"`
	VulkanPackage    *PortagePackage `json:"VulkanPackage,omitempty"`
}

func log(format string, args ...interface{}) {
	fmt.Fprintf(os.Stdout, format+"\n", args...)
}

func debug(format string, args ...interface{}) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
}

func fatal(format string, args ...interface{}) {
	debug(format, args...)
	os.Exit(1)
}

func queryHardware() hardwareResult {
	result := hardwareResult{}
	memory, err := getMemory()
	if err != nil {
		debug("Failed to get total memory: %v", err)
	} else {
		result.Memory = memory
	}

	disk, err := getLargestDisk()
	if err != nil {
		debug("Failed to get disk storage size: %v", err)
	} else {
		result.Disk = disk
	}

	cpuSocFamily, err := getCPUSOCFamily()
	if err != nil {
		debug("Failed to determine CPU SOC family: %v", err)
	} else {
		result.CPUFamily = cpuSocFamily
	}

	gpuInfos, err := getGPUInfos()
	if err != nil {
		debug("Failed to determine GPU: %v", err)
	} else {
		result.GPUInfos = gpuInfos
	}

	vgaDevices, err := GetVGADevices()
	if err != nil {
		debug("Failed to determine VGA device: %v", err)
	} else {
		result.VGADevices = vgaDevices
	}
	return result
}

func querySoftware() softwareResult {
	result := softwareResult{}
	gles, err := getGLESVersion()
	if err != nil {
		debug("Failed to get opengl es version: %v", err)
	} else {
		result.OpenGLES = gles
	}

	_, apiVersion, err := getVulkanVersion()
	if err != nil {
		debug("Failed to get vulkan information: %v", err)
	} else {
		result.VulkanAPIVersion = apiVersion
	}
	glDriverPackage, err := getGLESDriverPackage()
	if err != nil {
		debug("Failed to get gl driver package: %v", err)
	} else {
		result.OpenGLESPackage = glDriverPackage
	}

	vulkanDriverPackage, err := getVulkanDriverPackage()
	if err != nil {
		debug("Failed to get vulkan driver package: %v", err)
	} else {
		result.VulkanPackage = vulkanDriverPackage
	}
	return result
}

func queryLabels() (interface{}, error) {
	combineMap := func(a, b map[string]string) {
		for k, v := range b {
			a[k] = v
		}
	}

	hardware := queryHardware()
	software := querySoftware()

	result := make(map[string]string)
	result["PlatformCPUVendor"] = hardware.CPUFamily.String()
	result["PlatformDiskSize"] = fmt.Sprintf("%v", hardware.Disk.SizeGb)
	result["PlatformMemorySize"] = fmt.Sprintf("%v", hardware.Memory)
	// We sorted GPUInfos by bootVGA flag, this is the one comes by default when booting.
	// Assume it is the integrated GPU.
	combineMap(result, hardware.GPUInfos[0].Labels("GPU"))
	if len(hardware.GPUInfos) > 1 {
		// TODO: Support 3+ GPUs.
		combineMap(result, hardware.GPUInfos[1].Labels("dGPU"))
	}

	// Software properties.
	if software.VulkanAPIVersion != nil {
		result["VulkanVersion"] = software.VulkanAPIVersion.String()
	}
	if software.OpenGLES != nil {
		result["OpenGLESVersion"] = software.OpenGLES.String()
	}
	return result, nil

}

func main() {
	software := flag.Bool("software", false, "If set, query the software properties instead of hardware")
	labels := flag.Bool("labels", false, "If set, output gathered information key:value pair for infra.")
	outputFile := flag.String("output", "", "If set, output result to file.")
	flag.Parse()

	var result []byte
	var err error
	if *labels {
		resultMap, err := queryLabels()
		if err != nil {
			fatal("Failed to generate infra labels: ", err)
		}
		// Output it in key: val format.
		for k, v := range resultMap.(map[string]string) {
			result = append(result, []byte(fmt.Sprintf("%v: %v\n", k, v))...)
		}
	} else if *software {
		result, err = json.MarshalIndent(querySoftware(), "", "    ")
		if err != nil {
			fatal("failed to marshal softare result: %v", err)
		}
	} else {
		result, err = json.MarshalIndent(queryHardware(), "", "    ")
		if err != nil {
			fatal("Failed to marshal hardware result: %v", err)
		}
	}
	if len(*outputFile) != 0 {
		if err := os.WriteFile(*outputFile, result, 0755); err != nil {
			fatal("Failed to write to %v: %v", *outputFile, err)
		}
	} else {
		log(string(result))
	}
}
