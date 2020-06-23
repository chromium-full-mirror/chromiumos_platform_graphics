// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"flag"
	"fmt"

	db "go.chromium.org/chromiumos/config/go/api/test/results/v1"
)

// CmdTraceResult encapsulates the trace-result sub-command.
type CmdTraceResult struct {
	flagSet       *flag.FlagSet
	argHelp       bool
	argOutputFile string
}

// NewCmdTraceResult allocates and returns a new CmdTraceResult object.
func NewCmdTraceResult() *CmdTraceResult {
	cmd := CmdTraceResult{
		flagSet: flag.NewFlagSet("trace-result", flag.ContinueOnError),
	}

	cmd.flagSet.StringVar(&cmd.argOutputFile, "output", "", "Output file (default is stdout)")
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
		c.flagSet.Usage()
		return nil
	}

	// Process each file listed after the cmd-line options. Each successfully parsed
	// file yields a Result protobuf object that is added to ResultList.
	results := db.ResultList{}
	for _, f := range c.flagSet.Args() {
		result, err := parseProfile(f)
		if err != nil {
			return fmt.Errorf("failed to parse profile %s, err = %s", f, err.Error())
		}

		results.Value = append(results.Value, result)
	}

	err := writeProtobuf(&results, c.argOutputFile)
	if err != nil {
		return fmt.Errorf("unable to write output, err = %s", err.Error())
	}

	return nil
}
