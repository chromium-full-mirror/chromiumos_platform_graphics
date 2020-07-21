// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"flag"
	"fmt"
	"os"
	"time"

	"github.com/google/uuid"
	db "go.chromium.org/chromiumos/config/go/api/test/results/v1"
)

// CmdMachineInfo encapsulates the machine-info sub-command.
type CmdMachineInfo struct {
	flagSet        *flag.FlagSet
	argOutputFile  string
	argMachineName string
	argOwner       string
	argHelp        bool
}

// NewCmdMachineInfo allocates and returns a new CmdMachineInfo object.
func NewCmdMachineInfo() *CmdMachineInfo {
	cmd := CmdMachineInfo{
		flagSet: flag.NewFlagSet("machine-info", flag.ContinueOnError),
	}

	// Note: default value for "owner" means not set.
	cmd.flagSet.StringVar(&cmd.argOwner, "owner", ".", "Owner to assign")
	cmd.flagSet.StringVar(&cmd.argMachineName, "name", "", "Machine name to assign")
	cmd.flagSet.StringVar(&cmd.argOutputFile, "output", "", "Output file (default is stdout)")
	cmd.flagSet.BoolVar(&cmd.argHelp, "help", false, "Show help info")

	return &cmd
}

// CmdName returns the command name.
func (c *CmdMachineInfo) CmdName() string {
	return c.flagSet.Name()
}

// Setup is called to setup the sub-command.
func (c *CmdMachineInfo) Setup(args []string) error {
	return c.flagSet.Parse(args)
}

// Execute is called to execute the sub-command.
func (c *CmdMachineInfo) Execute() error {
	if c.argHelp {
		c.flagSet.Usage()
		return nil
	}

	d, _ := readBiosKeyValFile("/var/log/bios_info.txt")
	machine := db.Machine{}
	if hwid, ok := d["hwid"]; ok {
		machine.Hwid = hwid
	}

	if c.argMachineName == "" {
		c.argMachineName = uuid.New().String()
	}
	machine.Name = &db.MachineId{
		Value: c.argMachineName,
	}

	if c.argOwner == "." {
		c.argOwner = os.Getenv("USER")
		if c.argOwner == "root" {
			return fmt.Errorf("machine owner may not be \"root\"")
		}
	}
	machine.Owner = c.argOwner

	now := time.Now()
	machine.CreateTime = createTimestamp(&now)

	err := WriteProtobuf(&machine, c.argOutputFile)
	if err != nil {
		return fmt.Errorf("failed to write output, err = %s", err.Error())
	}

	return nil
}
