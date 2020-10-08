// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	db "go.chromium.org/chromiumos/config/go/api/test/results/v1"
)

// CmdTraceResult encapsulates the trace-result sub-command.
type CmdTraceResult struct {
	flagSet           *flag.FlagSet
	argHelp           bool
	argOutputFile     string
	argMachine        string
	argSoftwareConfig string

	machineId        string
	softwareConfigId string
}

// NewCmdTraceResult allocates and returns a new CmdTraceResult object.
func NewCmdTraceResult() *CmdTraceResult {
	cmd := CmdTraceResult{
		flagSet: flag.NewFlagSet("trace-result", flag.ContinueOnError),
	}

	cmd.flagSet.StringVar(&cmd.argOutputFile, "output", "", "Output file (default is stdout)")
	cmd.flagSet.StringVar(&cmd.argMachine, "machine", "", "Machine protobuf or ID")
	cmd.flagSet.StringVar(&cmd.argSoftwareConfig, "software", "", "SoftwareConfig protobuf or ID")
	cmd.flagSet.BoolVar(&cmd.argHelp, "help", false, "Show help info")

	return &cmd
}

// CmdName returns the command name.
func (c *CmdTraceResult) CmdName() string {
	return c.flagSet.Name()
}

// Setup is called to setup the sub-command.
func (c *CmdTraceResult) Setup(args []string) error {
	return c.flagSet.Parse(args)
}

// Execute is called to execute the sub-command.
func (c *CmdTraceResult) Execute() error {
	if c.argHelp {
		appName := filepath.Base(os.Args[0])
		fmt.Printf("Usage: %s [app-options] %s [options] profile [profile...]\n",
			appName, c.CmdName())
		fmt.Printf("where options are:\n")
		c.flagSet.Usage()
		return nil
	}

	if c.argMachine != "" {
		if strings.HasSuffix(c.argMachine, ".json") || strings.HasSuffix(c.argMachine, ".pb") {
			machine := db.Machine{}
			if err := ReadProtoFromFile(c.argMachine, &machine); err != nil {
				return fmt.Errorf("failed to parse machine pb %s, err = %s", c.argMachine, err.Error())
			}
			c.machineId = machine.Name.Value
		} else {
			// Assume that argMachine, if not empty, is an ID string.
			c.machineId = c.argMachine
		}
	}

	if c.argSoftwareConfig != "" {
		if strings.HasSuffix(c.argSoftwareConfig, ".json") || strings.HasSuffix(c.argSoftwareConfig, ".pb") {
			softwareConfig := db.SoftwareConfig{}
			if err := ReadProtoFromFile(c.argSoftwareConfig, &softwareConfig); err != nil {
				return fmt.Errorf("failed to parse software config pb %s, err = %s", c.argSoftwareConfig, err.Error())
			}
			c.softwareConfigId = softwareConfig.Id.Value
		} else {
			// Assume that argSoftwareConfig, if not empty, is an ID string.
			c.softwareConfigId = c.argSoftwareConfig
		}
	}

	// Process each file listed after the cmd-line options. Each successfully parsed
	// file yields a Result protobuf object that is added to ResultList.
	results := db.ResultList{}
	numProfiles := len(c.flagSet.Args())
	for _, f := range c.flagSet.Args() {
		result, err := parseProfile(f)
		if err != nil {
			if numProfiles > 1 {
				fmt.Fprintf(os.Stderr, "Warning: skipping %s because of parsing error: %s\n",
					f, err.Error())
				continue
			} else {
				return fmt.Errorf("failed to parse profile %s, err = %s", f, err.Error())
			}
		}

		// Cmd-line overrides.
		if c.machineId != "" {
			result.Machine = &db.MachineId{Value: c.machineId}
		}
		if c.softwareConfigId != "" {
			result.SoftwareConfig = &db.SoftwareConfigId{Value: c.softwareConfigId}
		}

		results.Value = append(results.Value, result)
	}

	err := WriteProtobuf(&results, c.argOutputFile)
	if err != nil {
		return fmt.Errorf("unable to write output, err = %s", err.Error())
	}

	return nil
}
