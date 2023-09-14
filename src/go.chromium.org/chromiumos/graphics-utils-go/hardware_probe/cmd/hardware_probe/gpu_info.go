// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
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

// getGPUInfos returns the GPU family name for the host.
// TODO(ddmail): Support returning mulitple mali/qualcomm GPUs.
func getGPUInfos() ([]GPUInfo, error) {
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
		// Fill in GPU_Vendor for qualcomm and mediatek.
		socFamily, err := getCPUSOCFamily()
		if err == nil {
			if socFamily == socQualcomm {
				return []GPUInfo{{Family: gpuFamily, GPUVendor: vendorQualcomm}}, nil
			}
			if socFamily == socMediaTek {
				return []GPUInfo{{Family: gpuFamily, GPUVendor: vendorMediatek}}, nil
			}
			if socFamily == socRockchip {
				return []GPUInfo{{Family: gpuFamily, GPUVendor: vendorRockchip}}, nil
			}
		}
		return []GPUInfo{{Family: gpuFamily, GPUVendor: vendorUnknown}}, nil
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
		return []GPUInfo{{Family: renderer, GPUVendor: vendorSoftware}}, nil
	}
	return nil, errors.Errorf("unknown opengl renderer: %v", renderer)
}
