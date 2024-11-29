// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"fmt"
	"github.com/pkg/errors"
	"os"
	"os/exec"
	"regexp"
	"sort"
	"strings"
)

type gpuVendor int

const (
	vendorUnknown gpuVendor = iota
	vendorAMD
	vendorIntel
	vendorMediatek
	vendorNvidia
	vendorQualcomm
	vendorRockchip
	vendorVirtio
	vendorVmware
	vendorSoftware
)

// String is string representation of the enum.
func (s gpuVendor) String() string {
	switch s {
	case vendorAMD:
		return "amd"
	case vendorIntel:
		return "intel"
	case vendorMediatek:
		return "mediatek"
	case vendorNvidia:
		return "nvidia"
	case vendorQualcomm:
		return "qualcomm"
	case vendorRockchip:
		return "rockchip"
	case vendorVirtio:
		return "virtio"
	case vendorVmware:
		return "vmware"
	case vendorSoftware:
		return "software"
	default:
		return "unknown"
	}
}

// MarshalJSON marshals the enum to JSON string.
func (s gpuVendor) MarshalJSON() ([]byte, error) {
	buffer := bytes.NewBufferString(`"`)
	buffer.WriteString(s.String())
	buffer.WriteString(`"`)
	return buffer.Bytes(), nil
}

// GPUInfo contains information for GPU.
type GPUInfo struct {
	Family    string    // Family is the architector of the GPU, e.g. alderlake, ampere, etc.
	GPUVendor gpuVendor // GPUVendor is the vendor of the GPU, e.g. Intel, Qualcomm, Mediatek, etc.
	ID        string    // For Intel, it is the PCIID, for ARM, we used the Family as its ID.
}

func (info GPUInfo) Labels(keyPrefix string) map[string]string {
	m := make(map[string]string)
	m[keyPrefix+"Family"] = info.Family
	m[keyPrefix+"Vendor"] = info.GPUVendor.String()
	m[keyPrefix+"ID"] = info.ID
	return m
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

// hasPanfrostGPUEnabled checks if the panfrost driver is enabled
func hasPanfrostGPUEnabled() (bool, error) {
	if _, err := os.Stat("/sys/bus/platform/drivers/panfrost"); err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return false, nil
		}
		return false, errors.Wrap(err, "failed to determine if the device has Panfrost driver")
	}
	return true, nil
}

func getWaffleInfo() (string, error) {
	getUseFlags := func() ([]string, error) {
		flags := []string{}
		out, err := os.ReadFile("/etc/ui_use_flags.txt")
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
			if flag == "opengles" {
				return "gles2", nil
			}
		}
		return "gl", nil
	}
	graphicsAPI, err := getGraphicsAPI()
	if err != nil {
		return "", errors.Wrap(err, "failed to get graphics api")
	}
	out, err := exec.Command("wflinfo", "-p", "null", "-a", graphicsAPI).Output()
	if err != nil {
		return "", errors.Wrap(err, "failed to run wflinfo")
	}
	return string(out), nil
}

func getGlxinfo() (string, error) {
	out, err := exec.Command("glxinfo").Output()
	if err != nil {
		return "", errors.Wrap(err, "failed to run glxinfo")
	}
	return string(out), nil
}

func getOpenGLRendererString() (string, error) {
	reg := regexp.MustCompile(`OpenGL renderer string: (\S+)`)

	glxinfo, glxErr := getGlxinfo()
	if glxErr == nil {
		if matches := reg.FindStringSubmatch(glxinfo); matches != nil {
			return matches[1], nil
		}
	}

	// Check waffleinfo instead.
	wflinfo, waffleErr := getWaffleInfo()
	if waffleErr == nil {
		if matches := reg.FindStringSubmatch(wflinfo); matches != nil {
			return matches[1], nil
		}
	}
	return "", errors.Wrap(glxErr, waffleErr.Error())
}

// getGPUInfoFromFamilyAndVendor calculates the GPU ID for devices using its renderer/vendor and returns the GPUInfo structure.
// This is mostly used in devices that have no access PCI information, e.g. ARM.
func getGPUInfoFromFamilyAndVendor(family string, vendor gpuVendor) GPUInfo {
	// The first part represents the vendorID.
	vendorID := vendor.String()
	// The second part represents the deviceID.
	deviceID := family
	ID := fmt.Sprintf("%v:%v", vendorID, deviceID)
	return GPUInfo{Family: family, GPUVendor: vendor, ID: ID}
}

// getGPUInfos returns the GPU family name for the host.
// TODO(ddmail): Support returning multiple mali/Qualcomm GPUs.
func getGPUInfos() ([]GPUInfo, error) {
	cpuArch, err := getCPUArch()
	if err != nil {
		debug("Failed to get CPU arch type: %v", err)
	}
	// Check for mali or panfrost
	hasMali, errMali := hasMaliGPUEnabled()
	hasPanfrost, errPanfrost := hasPanfrostGPUEnabled()
	if errMali != nil && errPanfrost != nil {
		return nil, errors.New("failed to detect either Mali or Panfrost")
	}

	if hasMali || hasPanfrost {
		renderer, err := getOpenGLRendererString()
		if err != nil {
			return nil, errors.Wrap(err, "failed to get renderer string")
		}
		if !strings.HasPrefix(renderer, "Mali-") {
			return nil, errors.Errorf("unexpected opengl renderer for mali: %v", renderer)
		}
		gpuFamily := strings.ToLower(renderer)
		// Fill in GPU_Vendor for Qualcomm and Mediatek.
		socFamily, err := getCPUSOCFamily(cpuArch)
		if err == nil {
			if socFamily == socQualcomm {
				return []GPUInfo{getGPUInfoFromFamilyAndVendor(gpuFamily, vendorQualcomm)}, nil
			}
			if socFamily == socMediaTek {
				return []GPUInfo{getGPUInfoFromFamilyAndVendor(gpuFamily, vendorMediatek)}, nil
			}
			if socFamily == socRockchip {
				return []GPUInfo{getGPUInfoFromFamilyAndVendor(gpuFamily, vendorRockchip)}, nil
			}
		}
		return []GPUInfo{getGPUInfoFromFamilyAndVendor(gpuFamily, vendorUnknown)}, nil
	}

	// Check for Qualcomm, Rogue
	socFamily, err := getCPUSOCFamily(cpuArch)
	if err != nil {
		return nil, errors.Wrap(err, "failed to determine CPU SOC family")
	}
	if socFamily == socQualcomm || socFamily == socMediaTek {
		family, name, err := getARMSOCFamilyFromCompatible()
		if err != nil {
			return nil, errors.Wrap(err, "failed to get ARM SOC information")
		}
		if family == socQualcomm {
			return []GPUInfo{getGPUInfoFromFamilyAndVendor(name, vendorQualcomm)}, nil
		} else if family == socMediaTek && name == "mt8173" {
			// For old mediaTek board, it has rogue driver instead of mali.
			return []GPUInfo{getGPUInfoFromFamilyAndVendor("rogue", vendorMediatek)}, nil
		} else {
			return nil, errors.Errorf("not recognizing Qualcomm or Mediatek device: %v", family)
		}
	}

	// For AMD and intel, check the pci_id_map for their respective GPU.
	vgaDevices, err := GetVGADevices()
	if err != nil {
		return nil, errors.Wrap(err, "failed to get VGA info")
	}
	// If multiple VGA devices are found, sort it so that devices with BootVGA start first.
	sort.Slice(vgaDevices, func(i, j int) bool {
		return vgaDevices[i].BootVGA
	})
	if len(vgaDevices) > 0 {
		gpuNames := []GPUInfo{}
		for _, device := range vgaDevices {
			gpuNames = append(gpuNames, device.GPUInfo)
		}
		return gpuNames, nil
	}

	// Most likely it is running under a VM and doesn't expose GPU drivers to the VGA devices.
	renderer, err := getOpenGLRendererString()
	if err != nil {
		return nil, errors.Wrap(err, "failed to get opengl renderer string")
	}
	renderer = strings.ToLower(renderer)
	if strings.Contains(renderer, "llvmpipe") || strings.Contains(renderer, "software") {
		return []GPUInfo{getGPUInfoFromFamilyAndVendor(renderer, vendorSoftware)}, nil
	}
	return nil, errors.Errorf("unknown opengl renderer: %v", renderer)
}
