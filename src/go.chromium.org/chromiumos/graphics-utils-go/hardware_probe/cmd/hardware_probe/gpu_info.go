// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"fmt"
	"github.com/pkg/errors"
	"io/ioutil"
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
	vendorQualcomm
	vendorMediatek
	vendorNvidia
	vendorVirtio
	vendorVmware
)

// String is string representation of the enum.
func (s gpuVendor) String() string {
	switch s {
	case vendorAMD:
		return "amd"
	case vendorQualcomm:
		return "qualcomm"
	case vendorMediatek:
		return "mediatek"
	case vendorIntel:
		return "intel"
	case vendorNvidia:
		return "nvidia"
	case vendorVirtio:
		return "virtio"
	case vendorVmware:
		return "vmware"
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
	GPUVendor gpuVendor // GPUVendor is the vendor of the GPU, e.g. Intel, qualcomm, mediatek, etc.
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

// getGPUInfos returns the GPU family name for the host.
// TODO(ddmail): Support returning mulitple mali/qualcomm GPUs.
func getGPUInfos() ([]GPUInfo, error) {
	// Check for mali
	hasMali, err := hasMaliGPUEnabled()
	if err != nil {
		return nil, errors.Wrap(err, "failed to determine Mali")
	}
	if hasMali {
		wflinfo, err := getWaffleInfo()
		if err != nil {
			return nil, errors.Wrap(err, "failed to get waffle info")
		}
		maliReg := regexp.MustCompile(`OpenGL renderer string: (Mali-\w+)`)
		matches := maliReg.FindStringSubmatch(wflinfo)
		if matches == nil {
			return nil, errors.Errorf("failed to find mali version: %v", wflinfo)
		}
		return []GPUInfo{{Family: strings.ToLower(matches[1]), GPUVendor: vendorUnknown}}, nil
	}

	// Check for qualcomm, rogue
	socFamily, err := getCPUSOCFamily()
	if err != nil {
		return nil, errors.Wrap(err, "failed to determine CPU SOC family")
	}
	if socFamily == socQualcomm || socFamily == socMediaTek {
		family, name, err := getARMSOCFamilyFromCompatible()
		if err != nil {
			return nil, errors.Wrap(err, "failed to get ARM SOC information")
		}
		if family == socQualcomm {
			return []GPUInfo{{Family: name, GPUVendor: vendorQualcomm}}, nil
		} else if family == socMediaTek && name == "mt8173" {
			// For old mediaTek board, it has rogue driver instead of mali.
			return []GPUInfo{{Family: "rogue", GPUVendor: vendorMediatek}}, nil
		} else {
			return nil, errors.Errorf("not recognizing Qualcomm or Mediatek device: %v", family)
		}
	}

	// For AMD and intel, check the pci_id_map for their respecitive GPU.
	vgaDevices, err := GetVGADevices()
	if err != nil {
		return nil, errors.Wrap(err, "failed to get VGA info")
	}
	// If multiple VGA devices are found, sort it so that devices with BootVGA start first.
	sort.Slice(vgaDevices, func(i, j int) bool {
		return vgaDevices[i].BootVGA
	})
	if len(vgaDevices) == 0 {
		return nil, fmt.Errorf("failed to determine GPU from vgaDevices: %v", vgaDevices)
	}
	gpuNames := []GPUInfo{}
	for _, device := range vgaDevices {
		gpuNames = append(gpuNames, device.GPUInfo)
	}
	return gpuNames, nil
}
