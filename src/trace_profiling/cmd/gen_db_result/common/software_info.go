// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/google/uuid"
	db "go.chromium.org/chromiumos/config/go/api/test/results/v1"
)

// CmdSoftwareInfo encapsulates the software-info sub-command.
type CmdSoftwareInfo struct {
	flagSet             *flag.FlagSet
	argOutputFile       string
	argHelp             bool
	argSoftwareConfigID string
	argParent           string
	argLoadPbFrom       string
	argSkipPackages     bool
	argGentooPkgDb      string

	config *db.SoftwareConfig
}

// NewCmdSoftwareInfo creates and returns a new CmdSoftwareInfo object.
func NewCmdSoftwareInfo() *CmdSoftwareInfo {
	cmd := CmdSoftwareInfo{
		flagSet: flag.NewFlagSet("software-info", flag.ContinueOnError),
	}

	cmd.flagSet.StringVar(&cmd.argSoftwareConfigID, "id", "", "SoftwareConfigId to assign")
	cmd.flagSet.StringVar(&cmd.argLoadPbFrom, "load", "", "SoftwareConfig protobuf to load for merging")
	cmd.flagSet.StringVar(&cmd.argParent, "parent", "", "SoftwareConfig protobuf or ID of parent")
	cmd.flagSet.BoolVar(&cmd.argSkipPackages, "skip-packages", false, "Do not record packages")
	cmd.flagSet.StringVar(&cmd.argGentooPkgDb, "gentoo-pkg-db", "/var/db/pkg", "Gentoo package database")
	cmd.flagSet.StringVar(&cmd.argOutputFile, "output", "", "Output file (default is stdout)")
	cmd.flagSet.BoolVar(&cmd.argHelp, "help", false, "Show help info")

	return &cmd
}

// CmdName returns the command name.
func (c *CmdSoftwareInfo) CmdName() string {
	return c.flagSet.Name()
}

// Setup is called to setup the sub-command.
func (c *CmdSoftwareInfo) Setup(args []string) error {
	return c.flagSet.Parse(args)
}

// Execute is called to execute the sub-command.
func (c *CmdSoftwareInfo) Execute() error {
	var err error

	if c.argHelp {
		appName := filepath.Base(os.Args[0])
		fmt.Printf("Usage: %s [app-options] %s [options]\n", appName, c.CmdName())
		fmt.Printf("where options are:\n")
		c.flagSet.Usage()
		return nil
	}

	c.config = &db.SoftwareConfig{}
	if c.argLoadPbFrom != "" {
		if err := ReadProtoFromFile(c.argLoadPbFrom, c.config); err != nil {
			return err
		}
		if c.argSoftwareConfigID != "" {
			c.config.Id = &db.SoftwareConfigId{Value: c.argSoftwareConfigID}
		}
	} else {
		if c.argSoftwareConfigID != "" {
			c.config.Id = &db.SoftwareConfigId{Value: c.argSoftwareConfigID}
		} else {
			c.config.Id = &db.SoftwareConfigId{Value: uuid.New().String()}
		}
	}

	var parentID string
	if parentID, err = c.getParentSoftwareConfigID(); err != nil {
		return err
	}
	if parentID != "" {
		c.config.Parent = &db.SoftwareConfigId{
			Value: parentID,
		}
	}

	now := time.Now()
	c.config.CreateTime = createTimestamp(&now)

	// If the config was not loaded from an existing protobuf, then gather and add
	// software info.
	if c.argLoadPbFrom == "" {
		if !c.argSkipPackages {
			c.addDebianPackages()
			c.addGentooPackages(c.argGentooPkgDb)
		}

		if kernelRel, err := getKernelRelease(); err == nil {
			c.config.KernelRelease = kernelRel
		}

		if kernelVer, err := getKernelVersion(); err == nil {
			c.config.KernelVersion = kernelVer
		}

		c.addLsbReleaseInfo()
		c.addOSReleaseInfo()
		c.addBiosInfo()
		c.addECInfo()
	}

	return WriteProtobuf(c.config, c.argOutputFile)
}

// Return a parent-software-config ID if one can be found or and empty string
// otherwise.
func (c *CmdSoftwareInfo) getParentSoftwareConfigID() (string, error) {
	// If a parent ID is specified in the cmd-line options, it should be either an
	// path for an existing protobuf file or directly a parent ID. In the former case,
	// read the protobuf data as a software-config and return its ID.
	if c.argParent == "" {
		return "", nil
	}

	if strings.HasSuffix(c.argParent, ".json") || strings.HasSuffix(c.argParent, ".pb") {
		parent := db.SoftwareConfig{}
		if err := ReadProtoFromFile(c.argParent, &parent); err != nil {
			return "", err
		}
		return parent.Id.Value, nil
	}

	// Assume that argParent, if not empty, is a parent ID string.
	return c.argParent, nil
}

// Add the lsb-release info to the software config.
func (c *CmdSoftwareInfo) addLsbReleaseInfo() {
	dict, err := readSimpleKeyValFile("/etc/lsb-release")
	if err == nil {
		itemCount := 0

		cros := &db.SoftwareConfig_ChromeOS{}
		if val, ok := dict["CHROMEOS_RELEASE_BOARD"]; ok {
			cros.Board = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_BRANCH_NUMBER"]; ok {
			cros.BranchNumber = strToUint32(val)
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_BUILDER_PATH"]; ok {
			cros.BuilderPath = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_BUILD_NUMBER"]; ok {
			cros.BuildNumber = strToUint32(val)
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_BUILD_TYPE"]; ok {
			cros.BuildType = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_CHROME_MILESTONE"]; ok {
			cros.ChromeMilestone = strToUint32(val)
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_DESCRIPTION"]; ok {
			cros.Description = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_KEYSET"]; ok {
			cros.Keyset = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_NAME"]; ok {
			cros.Name = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_PATCH_NUMBER"]; ok {
			cros.PatchNumber = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_TRACK"]; ok {
			cros.Track = val
			itemCount++
		}
		if val, ok := dict["CHROMEOS_RELEASE_VERSION"]; ok {
			cros.Version = val
			itemCount++
		}

		// On Chromebooks only, print a friendly warning if the number of items found
		// is less than expected. This is to help us track unexpected changes to
		// lsb-release over time.
		if itemCount > 0 && itemCount != 12 {
			printWarningToStderr(
				"Warning: only %d of 12 expected entries found in lsb-release.\n", itemCount)
		}

		c.config.Chromeos = cros
	}
}

// Add the os-release info to the software config.
func (c *CmdSoftwareInfo) addOSReleaseInfo() {
	dict, err := readSimpleKeyValFile("/etc/os-release")
	if err == nil {
		osConfig := &db.SoftwareConfig_OS{}
		if val, ok := dict["BUILD_ID"]; ok {
			osConfig.BuildId = val
		}
		if val, ok := dict["VERSION_CODENAME"]; ok {
			osConfig.Codename = val
		}
		if val, ok := dict["ID"]; ok {
			osConfig.Id = val
		}
		if val, ok := dict["NAME"]; ok {
			osConfig.Name = val
		}
		if val, ok := dict["PRETTY_NAME"]; ok {
			osConfig.PrettyName = val
		}
		if val, ok := dict["VERSION_ID"]; ok {
			osConfig.VersionId = val
		}
		if val, ok := dict["VERSION"]; ok {
			osConfig.Version = val
		}

		c.config.Os = osConfig
	}
}

// Add the bios info, if available, the the software config.
func (c *CmdSoftwareInfo) addBiosInfo() {
	dict, err := readBiosKeyValFile("/var/log/bios_info.txt")
	if err == nil {
		if val, ok := dict["fwid"]; ok {
			c.config.BiosVersion = val
		}
	}
}

// Add the EC info, if available, to the software config.
func (c *CmdSoftwareInfo) addECInfo() {
	dict, err := readBiosKeyValFile("/var/log/ec_info.txt")
	if err == nil {
		if val, ok := dict["fw_version"]; ok {
			c.config.EcVersion = val
		}
	}
}

// Add the Debian-package info, gather with dpkg-query, to the software config.
// Fails gracefully if that info is not available.
func (c *CmdSoftwareInfo) addDebianPackages() {
	cmd := exec.Command("dpkg-query", "--show", "--showformat=${binary:Package} ${Version}\\n")
	cmdOuput, err := cmd.CombinedOutput()
	if err == nil {
		scanner := bufio.NewScanner(strings.NewReader(string(cmdOuput)))
		for scanner.Scan() {
			line := strings.TrimSpace(scanner.Text())
			tokens := strings.Split(line, " ")
			if len(tokens) == 2 {
				pack := db.Package{
					Name:    tokens[0],
					Version: tokens[1],
				}
				c.config.Packages = append(c.config.Packages, &pack)
			}
		}
	}
}

// Add the Gentoo-package info the the software config. The info is gathered by
// looking for license.yaml files in the directory hierarchy rooted at basePath.
func (c *CmdSoftwareInfo) addGentooPackages(basePath string) {
	if _, err := os.Stat(basePath); !os.IsNotExist(err) {
		visitLicense := func(root string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if info.IsDir() {
				license := filepath.Join(root, "license.yaml")
				if _, err := os.Stat(license); !os.IsNotExist(err) {
					dict, err := readPkgYaml(license)
					if err != nil {
						return err
					}
					c.addYamlLicensePackage(dict)
				}
			}

			return nil
		}

		filepath.Walk(basePath, visitLicense)
	}
}

// Given a dictionary of key-value pairs parsed from a yaml license file,
// add its info the software config.
func (c *CmdSoftwareInfo) addYamlLicensePackage(dict map[string]string) {
	pack := db.Package{
		Name: fmt.Sprintf("%s/%s", dict["category"], dict["name"]),
	}

	revision, ok := dict["revision"]
	if ok {
		pack.Version = fmt.Sprintf("%s-r%s", dict["version"], revision)
	} else {
		pack.Version = dict["version"]
	}

	c.config.Packages = append(c.config.Packages, &pack)
}

// Get and return the kernel release info as a string.
func getKernelRelease() (string, error) {
	cmd := exec.Command("uname", "-r")
	result, err := cmd.CombinedOutput()
	return strings.TrimSpace(string(result)), err
}

// Get and return the kernel version info as a string.
func getKernelVersion() (string, error) {
	cmd := exec.Command("uname", "-v")
	result, err := cmd.CombinedOutput()
	return strings.TrimSpace(string(result)), err
}

// Read a yaml package license file and return the relevant info as a ditcionary.
func readPkgYaml(filename string) (map[string]string, error) {
	// Look for lines of the form (double quotes not included):
	//  "- !!python/tuple [name, !!python/unicode 'dash']", or
	//  "- !!python/tuple [version, 1.5.0_pre20110524131526]"
	re := regexp.MustCompile(`^\s?- !!python/tuple \[([a-z]+), (?:!!python/unicode )?(.*)\]$`)
	return parseKeyValFileWithRegex(filename, re)
}

// Read a simple key-value file, such as a lsb-release info file and returns its
// content as a dictionary.
func readSimpleKeyValFile(filename string) (map[string]string, error) {
	// Look for lines of the form key=val, such as:
	//   CHROMEOS_RELEASE_KEYSET=devkeys
	re := regexp.MustCompile(`^([^=]+)=(.+)`)
	return parseKeyValFileWithRegex(filename, re)
}
