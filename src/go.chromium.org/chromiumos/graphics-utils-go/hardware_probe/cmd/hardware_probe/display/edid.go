// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package display

import (
	"encoding/hex"
	"os"
	"os/exec"
	"regexp"
	"unicode"

	"github.com/pkg/errors"
)

// Edid structure is used to describe the structure of Edid
type Edid struct {
	ManufacturerName string // Manufacturer name
	ModelNumber      uint16 // Model number

	VsyncRateMin uint16 // Minimum vsync rate in Hz
	VsyncRateMax uint16 // Maximum vsync rate in Hz

	HDRBlock bool // True if HDR metadata block exists

	Bytes []byte `json:"Base64Bytes"`
	// Add additional fields as needed.
}

// EdidStringToBytes converts the provided EDID string into an array of bytes
func EdidStringToBytes(edidString string) ([]byte, error) {
	// Prune all whitespace from the EDID string.
	prunedEdidString := regexp.MustCompile(`\s*`).ReplaceAllString(edidString, "")

	// Decode the hex-encoded EDID string into bytes.
	edidBytes, err := hex.DecodeString(prunedEdidString)
	if err != nil {
		return nil, errors.Wrap(err, "failed to decode EDID string into bytes")
	}

	// Pad the decoded EDID to 256 bytes.
	if len(edidBytes) < 256 {
		padding := make([]byte, 256-len(edidBytes))
		edidBytes = append(edidBytes, padding...)
	}

	return edidBytes, nil
}

// ParseEdid parses the provided EDID byte array into a structured type
func ParseEdid(edid []byte) Edid {
	parsedEdid := Edid{Bytes: edid}

	parsedEdid.ManufacturerName = getManufacturerName(edid)
	parsedEdid.ModelNumber = getModelNumber(edid)
	parsedEdid.HDRBlock = getHDRSupport(edid)

	// bytes 54-125: 4 18-byte descriptors.
	const (
		descriptorOffset             = 54
		numDescriptors               = 4
		descriptorLength             = 18
		displayRangeLimitsDescriptor = 0xfd
	)

	for i := 0; i < numDescriptors; i++ {
		if len(edid) < descriptorOffset+(i+1)*descriptorLength {
			break
		}

		offset := descriptorOffset + i*descriptorLength

		// Display range limits descriptor.
		if edid[offset] == 0 && edid[offset+1] == 0 && edid[offset+2] == 0 &&
			edid[offset+3] == displayRangeLimitsDescriptor {
			// byte 4: Offsets for display range limits.
			rateOffset := edid[offset+4]
			// bits 7-4: Reserved \0.
			if rateOffset&0xf0 != 0 {
				continue
			}
			// bit 0: Vertical min rate offset.
			var verticalMinRateOffset uint16 = 0
			if rateOffset&(1<<0) != 0 {
				verticalMinRateOffset = 255
			}
			// bit 1: Vertical max rate offset.
			var verticalMaxRateOffset uint16 = 0
			if rateOffset&(1<<1) != 0 {
				verticalMaxRateOffset = 255
			}

			// bytes 5-8: Rate limits. Each byte must be within [1, 255].
			if edid[offset+5] == 0 || edid[offset+6] == 0 ||
				edid[offset+7] == 0 || edid[offset+8] == 0 {
				continue
			}
			// byte 5: Min vertical rate in Hz.
			parsedEdid.VsyncRateMin = uint16(edid[offset+5]) + verticalMinRateOffset
			// byte 6: Max vertical rate in Hz.
			parsedEdid.VsyncRateMax = uint16(edid[offset+6]) + verticalMaxRateOffset

			continue
		}
	}

	// Parse additional descriptors as needed.

	return parsedEdid
}

func getManufacturerName(x []byte) string {
	manufacturerNameOffset := 0x08
	name := make([]byte, 3)

	name[0] = ((x[manufacturerNameOffset+0] & 0x7c) >> 2) + '@'
	name[1] = ((x[manufacturerNameOffset+0] & 0x03) << 3) + ((x[manufacturerNameOffset+1] & 0xe0) >> 5) + '@'
	name[2] = (x[manufacturerNameOffset+1] & 0x1f) + '@'

	if !(unicode.IsUpper(rune(name[0])) && unicode.IsUpper(rune(name[1])) && unicode.IsUpper(rune(name[2]))) {
		return "Unknown Manufacturer"
	}

	return string(name)
}

func getModelNumber(x []byte) uint16 {
	return uint16(x[0x0a]) + (uint16(x[0x0b]) << 8)
}

func getHDRSupport(x []byte) bool {
	f, err := os.CreateTemp("/tmp", "")
	if err != nil {
		return false
	}
	defer os.Remove(f.Name())
	if _, err := f.Write(x); err != nil {
		return false
	}
	output, err := exec.Command("edid-decode", f.Name()).Output()
	if err != nil {
		return false
	}
	re := regexp.MustCompile(`HDR .* Data Block`)
	if match := re.FindSubmatch(output); match != nil {
		return true
	}
	return false
}
