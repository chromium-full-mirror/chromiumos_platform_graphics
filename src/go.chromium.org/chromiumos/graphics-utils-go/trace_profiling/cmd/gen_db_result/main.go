// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/gen_db_result/common"
)

// Interface command defines how individual sub-commands are setup and invoked.
type command interface {
	CmdName() string      // The name of the sub-command.
	Setup([]string) error // Call this function to setup the sub-command with cmd-line args.
	Execute() error       // Call this function to execute the sub-command.
}

func printUsage(subCommands []command) {
	appName := filepath.Base(os.Args[0])
	fmt.Printf("Usage: %s [options] <action> [options for action]\n", appName)
	fmt.Printf("       where <action> is one of: ")
	for i, c := range subCommands {
		if i > 0 {
			fmt.Printf(", ")
		}
		fmt.Print(c.CmdName())
	}
	fmt.Printf("\nAvailable options are:\n")
	flag.PrintDefaults()
	fmt.Printf("Use:   %s <action> -help  for additional help on action\n", appName)
}

func main() {
	subCommands := []command{
		common.NewCmdMachineInfo(),
		common.NewCmdSoftwareInfo(),
		common.NewCmdTraceResult(),
	}

	flag.Usage = func() {
		printUsage(subCommands)
	}
	suppressWarnings := flag.Bool("no-warnings", false, "Don't print warnings to Stderr")
	flag.Parse()
	common.FlagEnablePrintWarnings = !*suppressWarnings

	if len(flag.Args()) < 1 {
		printUsage(subCommands)
		os.Exit(1)
	}

	var err error
	var cmdDidRun = false
	for _, c := range subCommands {
		if c.CmdName() == flag.Args()[0] {
			err = c.Setup(flag.Args()[1:])
			if err == nil {
				err = c.Execute()
			}
			cmdDidRun = true
		}
	}

	if !cmdDidRun {
		fmt.Fprintf(os.Stderr, "%s is not a recognized action\n", os.Args[1])
		printUsage(subCommands)
		os.Exit(1)
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err.Error())
		os.Exit(1)
	}
}
