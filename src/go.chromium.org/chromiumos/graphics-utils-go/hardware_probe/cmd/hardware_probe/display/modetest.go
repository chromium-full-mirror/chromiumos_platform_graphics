// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package display contains display related codes.
package display

import (
	"context"
	"fmt"
	"github.com/pkg/errors"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"

	"go.chromium.org/chromiumos/graphics-utils-go/hardware_probe/cmd/hardware_probe/util"
)

const (
	minModeTestMatches = 11
	numBits            = 16
	connectorPattern   = `^(?P<id>\d+)\s+(?P<encoder>\d+)\s+(?P<status>connected|disconnected)\s+(?P<name>\S+)\s+(?P<width>\d+)x(?P<height>\d+)\s+(?P<countModes>\d+)\s+(?P<encoders>.+)$`
	crtcPattern        = `^(?P<id>\d+)\s+(?P<fb>\d+)\s+\((?P<x>\d+),(?P<y>\d+)\)\s+\((?P<width>\d+)x(?P<height>\d+)\)$`
	modePattern        = `^\s*#(?P<index>\d+)\s+(?P<name>\S+)\s+(?P<refresh>\d+\.?\d*)\s+(?P<hdisp>\d+)\s+(?P<hss>\d+)\s+(?P<hse>\d+)\s+(?P<htot>\d+)\s+(?P<vdisp>\d+)\s+(?P<vss>\d+)\s+(?P<vse>\d+)\s+(?P<vtot>\d+)\s+.*$`
	blobPattern        = `(?:\s*[a-f0-9]{32}\n?)*`
	propPattern        = `^\s*(?P<propID>\d+)(?:\s+(?P<name>.+))?:\n\s*flags:\s+(?P<flags>.*)\n\s*(?:(?:blobs:\n(?P<blobs>` +
		blobPattern + `)\s*value:\n(?P<blobValue>` + blobPattern +
		`)(?:\n.*decoded:.*)?)|(?:enums:\s+(?P<enums>.*)\n\s*value:\s+(?P<enumValue>.*))|(?:values:\s+(?P<values>.*)\n\s*value:\s+(?P<valueValue>.*)))`
)

var (
	connectorRegexp = regexp.MustCompile(`(?m)` + connectorPattern + `\n(?:\s*modes:\n.*\n(?P<modes>(?:` +
		modePattern + `\n?)*))?(?:\s*props:\n(?P<props>(?:` + propPattern + `\n?)*))?`)
	crtcRegexp = regexp.MustCompile(`(?m)` + crtcPattern + `\n(?P<mode>` + modePattern +
		`)\n(?:\s*props:\n(?P<props>(?:` + propPattern + `\n?)*))`)
	encoderRegexp = regexp.MustCompile(`^(?P<id>\d+)\s+(?P<crtc>\d+)\s+(?P<type>\S+)\s+0x(?P<possibleCrtcs>[0-9a-f]+)\s+0x(?P<possibleClones>[0-9a-f]+)$`)
	modeRegexp    = regexp.MustCompile(`(?m)` + modePattern)
	propRegexp    = regexp.MustCompile(`(?m)` + propPattern)
	planesRegexp  = regexp.MustCompile(
		`^(\d+)\s+(\d+)\s+(\d+)\s+(\d+),(\d+)\s+(\d+),(\d+)\s+(\d+)\s+(0x)(?P<bit>([[:xdigit:]]+))`)
	planesStartRegexp = regexp.MustCompile(
		`^id\s+crtc\s+fb\s+CRTC\s+x,y\s+x,y\s+gamma\s+size\s+possible\s+crtcs`)
)

// Encoder attributes parsed from modetest.
type Encoder struct {
	EncoderID      uint32
	CrtcID         uint32
	EncoderType    string
	PossibleCrtcs  uint32
	PossibleClones uint32
}

// Connector attributes parsed from modetest.
type Connector struct {
	ConnectorID uint32
	EncoderID   uint32
	Connected   bool
	Name        string
	Width       uint32
	Height      uint32
	CountModes  int
	Encoders    []uint32
	Modes       []*Mode
	VrrCapable  bool
	Edid        *Edid
}

// Crtc attributes parsed from modetest.
type Crtc struct {
	CrtcID        uint32
	FrameBufferID uint32
	X             uint32
	Y             uint32
	Width         uint32
	Height        uint32
	Mode          *Mode
	VrrEnabled    bool
}

// Mode attributes parsed from modetest.
type Mode struct {
	Index      int
	Name       string
	Refresh    float64
	HDisplay   uint16
	HSyncStart uint16
	HSyncEnd   uint16
	HTotal     uint16
	VDisplay   uint16
	VSyncStart uint16
	VSyncEnd   uint16
	VTotal     uint16
	Preferred  bool
}

// Display described by a connector and its possible encoders.
type Display struct {
	Connector *Connector       // The display's connector.
	Encoders  []DisplayEncoder // The encoders matching the ID provided by the connector (if found).
}

// Labels extracts relevant values into a key:value map for label reporting.
func (d Display) Labels(keyPrefix string) map[string]string {
	m := make(map[string]string)

	m[keyPrefix+"PanelName"] = "unknown"
	if strings.Contains(d.Connector.Name, "eDP") {
		m[keyPrefix+"PanelName"] = fmt.Sprintf("%v %v", d.Connector.Edid.ManufacturerName, d.Connector.Edid.ModelNumber)
	}

	m[keyPrefix+"Resolution"] = fmt.Sprintf("%vx%v", d.Connector.Height, d.Connector.Width)
	for _, mode := range d.Connector.Modes {
		if !mode.Preferred {
			continue
		}
		m[keyPrefix+"Resolution"] = fmt.Sprintf("%vx%v", mode.HDisplay, mode.VDisplay)
	}

	m[keyPrefix+"RefreshRate"] = "0.00"
	for _, mode := range d.Connector.Modes {
		if !mode.Preferred {
			continue
		}
		m[keyPrefix+"RefreshRate"] = fmt.Sprintf("%.2f", mode.Refresh)
	}

	m[keyPrefix+"PresentVRR"] = "vrr unsupported"
	if d.Connector.VrrCapable {
		m[keyPrefix+"PresentVRR"] = "vrr supported"
	}

	// TODO: parse the following field via edid. Ask display team to help fill this in.
	m[keyPrefix+"PresentPSR"] = "psr unsupported"
	if file, err := util.GetValidKernelDriverDebugFile([]string{"i915_edp_psr_status"}); err == nil {
		if content, err := os.ReadFile(file); err == nil {
			re := regexp.MustCompile(`Sink support: yes`)
			if match := re.FindSubmatch(content); match != nil {
				m[keyPrefix+"PresentPSR"] = "psr supported"
			}
		}
	}

	m[keyPrefix+"PresentHDR"] = "hdr unsupported"
	if d.Connector.Edid.HDRBlock {
		m[keyPrefix+"PresentHDR"] = "hdr supported"
	}
	return m
}

// DisplayEncoder described by a tuple of Encoder and Crtc.
type DisplayEncoder struct {
	Encoder *Encoder `json:"Encoder,omitempty"` // The encoder matching the ID provided by the connector (if found).
	Crtc    *Crtc    `json:"Crtc,omitempty"`    // The CRTC matching the ID provided by the encoder (if found).
}

// asBits converts an unsigned int value to 16 bit binary.
func asBits(val uint64) []uint64 {
	var bits = []uint64{}
	for i := 0; i < numBits; i++ {
		bits = append([]uint64{val & 0x1}, bits...)
		val = val >> 1
	}
	return bits
}

// GetModeTestPlanes runs modetest and returns a 2D array of uint binary representation of planes.
func GetModeTestPlanes(ctx context.Context) ([][]uint64, error) {
	var output [][]uint64
	stdout, err := exec.CommandContext(ctx, "modetest", "-p").Output()
	if err != nil {
		return output, errors.Wrap(err, "failed to run modetest ")
	}
	found := false
	for _, line := range strings.Split(string(stdout), "\n") {
		// Example of regex that will match:
		// 31      91      340     0,0             0,0     0               0x00000001
		if matches := planesRegexp.FindStringSubmatch(line); found && len(matches) >= minModeTestMatches {
			binary, err := strconv.ParseUint(matches[10], 16, 64)
			if err != nil {
				return output, errors.Wrapf(err, "failed to parse %s as unsigned int ", matches[10])
			}
			output = append(output, asBits(binary))
			if err != nil {
				return output, errors.Wrapf(err, "failed to convert %s to binary ", matches[10])
			}
		}
		matches := planesStartRegexp.FindStringSubmatch(line)
		if matches != nil {
			found = true
		}
	}
	return output, nil
}

// ModetestEncoders returns the list of encoders parsed from modetest.
func ModetestEncoders(ctx context.Context) ([]*Encoder, error) {
	output, err := exec.CommandContext(ctx, "modetest", "-e").Output()
	if err != nil {
		return nil, err
	}

	var encoders []*Encoder
	for _, line := range strings.Split(string(output), "\n") {
		matches := encoderRegexp.FindStringSubmatch(line)
		if matches == nil {
			continue
		}

		idMatch := matches[encoderRegexp.SubexpIndex("id")]
		crtcMatch := matches[encoderRegexp.SubexpIndex("crtc")]
		encoderType := matches[encoderRegexp.SubexpIndex("type")]
		possibleCrtcsMatch := matches[encoderRegexp.SubexpIndex("possibleCrtcs")]
		possibleClonesMatch := matches[encoderRegexp.SubexpIndex("possibleClones")]

		encoderID, err := strconv.ParseUint(idMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse encoder id %s", idMatch)
		}
		crtcID, err := strconv.ParseUint(crtcMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse crtc id %s", crtcMatch)
		}
		possibleCrtcs, err := strconv.ParseUint(possibleCrtcsMatch, 16, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse possible crtcs %s",
				possibleCrtcsMatch)
		}
		possibleClones, err := strconv.ParseUint(possibleClonesMatch, 16, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse possible clones %s",
				possibleClonesMatch)
		}
		encoders = append(encoders, &Encoder{
			EncoderID:      uint32(encoderID),
			CrtcID:         uint32(crtcID),
			EncoderType:    encoderType,
			PossibleCrtcs:  uint32(possibleCrtcs),
			PossibleClones: uint32(possibleClones),
		})
	}
	return encoders, nil
}

// splitAndConvertInt splits string with comma and whitespace then converts each sub-string to int.
func splitAndConvertInt(input string) ([]uint32, error) {
	splitPattern := regexp.MustCompile(` *, *`)
	substrings := splitPattern.Split(input, -1)
	var result []uint32
	for _, substring := range substrings {
		i, err := strconv.ParseUint(substring, 10, 32)
		if err != nil {
			return nil, err
		}
		result = append(result, uint32(i))
	}
	return result, nil
}

// ModetestConnectors returns the list of connectors parsed from modetest.
func ModetestConnectors(ctx context.Context) ([]*Connector, error) {
	output, err := exec.CommandContext(ctx, "modetest", "-c").Output()
	if err != nil {
		return nil, err
	}

	var connectors []*Connector
	matches := connectorRegexp.FindAllStringSubmatch(string(output), -1)
	if matches == nil {
		return nil, errors.Wrap(err, "failed to match connectors in modetest output")
	}
	for _, match := range matches {
		IDMatch := match[connectorRegexp.SubexpIndex("id")]
		encoderMatch := match[connectorRegexp.SubexpIndex("encoder")]
		statusMatch := match[connectorRegexp.SubexpIndex("status")]
		name := match[connectorRegexp.SubexpIndex("name")]
		widthMatch := match[connectorRegexp.SubexpIndex("width")]
		heightMatch := match[connectorRegexp.SubexpIndex("height")]
		countModesMatch := match[connectorRegexp.SubexpIndex("countModes")]
		encodersMatch := match[connectorRegexp.SubexpIndex("encoders")]
		modesMatch := match[connectorRegexp.SubexpIndex("modes")]
		propsMatch := match[connectorRegexp.SubexpIndex("props")]

		connectorID, err := strconv.ParseUint(IDMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse connector id %s", IDMatch)
		}
		encoderID, err := strconv.ParseUint(encoderMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse encoder id %s", encoderMatch)
		}
		connected := (statusMatch == "connected")
		width, err := strconv.ParseUint(widthMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse width %s", widthMatch)
		}
		height, err := strconv.ParseUint(heightMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse height %s", heightMatch)
		}
		countModes, err := strconv.Atoi(countModesMatch)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse countModes %s", countModesMatch)
		}
		encoders, err := splitAndConvertInt(encodersMatch)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse encoders %s", encodersMatch)
		}
		var modes []*Mode
		if modeMatches := modeRegexp.FindAllStringSubmatch(modesMatch, -1); modeMatches != nil {
			for _, modeMatch := range modeMatches {
				mode, err := parseMode(modeMatch)
				if err != nil {
					return nil, errors.Wrap(err, "failed to parse mode")
				}
				modes = append(modes, mode)
			}
		}
		vrrCapable := false
		var edid Edid
		if propMatches := propRegexp.FindAllStringSubmatch(propsMatch, -1); propMatches != nil {
			for _, propMatch := range propMatches {
				propName := propMatch[propRegexp.SubexpIndex("name")]
				blobValue := propMatch[propRegexp.SubexpIndex("blobValue")]
				valueValue := propMatch[propRegexp.SubexpIndex("valueValue")]
				if propName == "vrr_capable" {
					vrrCapable = valueValue == "1"
					continue
				}
				if propName == "EDID" {
					edidBytes, err := EdidStringToBytes(blobValue)
					if err != nil {
						return nil, errors.Wrap(err, "failed to convert EDID string to bytes")
					}
					edid = ParseEdid(edidBytes)
					continue
				}
			}
		}
		connectors = append(connectors, &Connector{
			ConnectorID: uint32(connectorID),
			EncoderID:   uint32(encoderID),
			Connected:   connected,
			Name:        name,
			Width:       uint32(width),
			Height:      uint32(height),
			CountModes:  countModes,
			Encoders:    encoders,
			Modes:       modes,
			VrrCapable:  vrrCapable,
			Edid:        &edid,
		})
	}
	return connectors, nil
}

// NumberOfOutputsConnected returns the number of connected connectors from modetest.
func NumberOfOutputsConnected(ctx context.Context) (int, error) {
	connectors, err := ModetestConnectors(ctx)
	if err != nil {
		return 0, err
	}
	connected := 0
	for _, display := range connectors {
		if display.Connected {
			connected++
		}
	}
	return connected, nil
}

// ModetestCrtcs returns the list of crtcs parsed from modetest.
func ModetestCrtcs(ctx context.Context) ([]*Crtc, error) {
	output, err := exec.CommandContext(ctx, "modetest", "-p").Output()
	if err != nil {
		return nil, err
	}

	var crtcs []*Crtc
	matches := crtcRegexp.FindAllStringSubmatch(string(output), -1)
	if matches == nil {
		return nil, errors.Wrap(err, "failed to match CRTCs in modetest output")
	}
	for _, match := range matches {
		IDMatch := match[crtcRegexp.SubexpIndex("id")]
		fbMatch := match[crtcRegexp.SubexpIndex("fb")]
		xMatch := match[crtcRegexp.SubexpIndex("x")]
		yMatch := match[crtcRegexp.SubexpIndex("y")]
		widthMatch := match[crtcRegexp.SubexpIndex("width")]
		heightMatch := match[crtcRegexp.SubexpIndex("height")]
		modeMatchRangeStart := crtcRegexp.SubexpIndex("mode")
		modeMatchRangeEnd := modeMatchRangeStart + modeRegexp.NumSubexp() + 1
		modeMatches := match[modeMatchRangeStart:modeMatchRangeEnd]
		propsMatch := match[crtcRegexp.SubexpIndex("props")]

		crtcID, err := strconv.ParseUint(IDMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse crtc id %s", IDMatch)
		}
		frameBufferID, err := strconv.ParseUint(fbMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse fb id %s", fbMatch)
		}
		x, err := strconv.ParseUint(xMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse x-position %s", xMatch)
		}
		y, err := strconv.ParseUint(yMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse y-position %s", yMatch)
		}
		width, err := strconv.ParseUint(widthMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse width %s", widthMatch)
		}
		height, err := strconv.ParseUint(heightMatch, 10, 32)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse height %s", heightMatch)
		}
		mode, err := parseMode(modeMatches)
		if err != nil {
			return nil, errors.Wrap(err, "failed to parse mode")
		}
		vrrEnabled := false
		if propMatches := propRegexp.FindAllStringSubmatch(propsMatch, -1); propMatches != nil {
			for _, propMatch := range propMatches {
				propName := propMatch[propRegexp.SubexpIndex("name")]
				valueValue := propMatch[propRegexp.SubexpIndex("valueValue")]
				if propName == "VRR_ENABLED" {
					vrrEnabled = valueValue == "1"
					break
				}
			}
		}
		crtcs = append(crtcs, &Crtc{
			CrtcID:        uint32(crtcID),
			FrameBufferID: uint32(frameBufferID),
			X:             uint32(x),
			Y:             uint32(y),
			Width:         uint32(width),
			Height:        uint32(height),
			Mode:          mode,
			VrrEnabled:    vrrEnabled,
		})
	}
	return crtcs, nil

}

// parseMode returns the mode parsed from the provided array of regexp substring matches.
func parseMode(matches []string) (*Mode, error) {
	indexMatch := matches[modeRegexp.SubexpIndex("index")]
	name := matches[modeRegexp.SubexpIndex("name")]
	refreshMatch := matches[modeRegexp.SubexpIndex("refresh")]
	hdispMatch := matches[modeRegexp.SubexpIndex("hdisp")]
	hssMatch := matches[modeRegexp.SubexpIndex("hss")]
	hseMatch := matches[modeRegexp.SubexpIndex("hse")]
	htotMatch := matches[modeRegexp.SubexpIndex("htot")]
	vdispMatch := matches[modeRegexp.SubexpIndex("vdisp")]
	vssMatch := matches[modeRegexp.SubexpIndex("vss")]
	vseMatch := matches[modeRegexp.SubexpIndex("vse")]
	vtotMatch := matches[modeRegexp.SubexpIndex("vtot")]

	index, err := strconv.Atoi(indexMatch)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode index %s", indexMatch)
	}
	refresh, err := strconv.ParseFloat(refreshMatch, 64)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode refresh rate %s", refreshMatch)
	}
	hDisplay, err := strconv.ParseUint(hdispMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode hDisplay %s", hdispMatch)
	}
	hSyncStart, err := strconv.ParseUint(hssMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode hSyncStart %s", hssMatch)
	}
	hSyncEnd, err := strconv.ParseUint(hseMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode hSyncEnd %s", hseMatch)
	}
	hTotal, err := strconv.ParseUint(htotMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode hTotal %s", htotMatch)
	}
	vDisplay, err := strconv.ParseUint(vdispMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode vDisplay %s", vdispMatch)
	}
	vSyncStart, err := strconv.ParseUint(vssMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode vSyncStart %s", vssMatch)
	}
	vSyncEnd, err := strconv.ParseUint(vseMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode vSyncEnd %s", vseMatch)
	}
	vTotal, err := strconv.ParseUint(vtotMatch, 10, 16)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse mode vTotal %s", vtotMatch)
	}
	return &Mode{
		Index:      index,
		Name:       name,
		Refresh:    refresh,
		HDisplay:   uint16(hDisplay),
		HSyncStart: uint16(hSyncStart),
		HSyncEnd:   uint16(hSyncEnd),
		HTotal:     uint16(hTotal),
		VDisplay:   uint16(vDisplay),
		VSyncStart: uint16(vSyncStart),
		VSyncEnd:   uint16(vSyncEnd),
		VTotal:     uint16(vTotal),
		Preferred:  strings.Contains(matches[0], "preferred"),
	}, nil
}

// ModetestConnectedDisplays returns of list of displays which are connected according to modetest.
func ModetestConnectedDisplays(ctx context.Context) ([]Display, error) {
	var displays []Display

	connectors, err := ModetestConnectors(ctx)
	if err != nil {
		return displays, errors.Wrap(err, "failed to get connectors from modetest")
	}
	encoders, err := ModetestEncoders(ctx)
	if err != nil {
		return displays, errors.Wrap(err, "failed to get encoders from modetest")
	}
	crtcs, err := ModetestCrtcs(ctx)
	if err != nil {
		return displays, errors.Wrap(err, "failed to get CRTCs from modetest")
	}

	encoderMap := make(map[uint32]*Encoder)
	for _, encoder := range encoders {
		encoderMap[encoder.EncoderID] = encoder
	}
	crtcMap := make(map[uint32]*Crtc)
	for _, crtc := range crtcs {
		crtcMap[crtc.CrtcID] = crtc
	}

	for _, connector := range connectors {
		if !connector.Connected {
			continue
		}

		displayEncoders := []DisplayEncoder{}
		for _, encoderID := range connector.Encoders {
			if _, ok := encoderMap[encoderID]; !ok {
				continue
			}
			encoder := encoderMap[encoderID]
			crtc := crtcMap[encoder.CrtcID]
			displayEncoders = append(displayEncoders, DisplayEncoder{
				Encoder: encoder,
				Crtc:    crtc,
			})
		}
		displays = append(displays, Display{
			Connector: connector,
			Encoders:  displayEncoders,
		})
	}
	return displays, nil
}
