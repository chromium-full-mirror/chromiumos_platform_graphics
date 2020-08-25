// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	gen_db "trace_profiling/cmd/gen_db_result/common"
	remote "trace_profiling/cmd/profile/remote"

	"github.com/golang/protobuf/jsonpb"
	"github.com/google/uuid"
	db "go.chromium.org/chromiumos/config/go/api/test/results/v1"
)

const (
	// Tool to run on the device to get device info.
	getInfoToolName = "get_device_info"
)

// DeviceInfoTool represents the tool used to get information from a device
// reachable through SSH.
type DeviceInfoTool struct {
	getDeviceInfoBinPath string // Path to get_device_info tool on device.
	machineOwner         string // Machine owner, e.g. "user/gwink".
	machineName          string // Unique machine name for the device.
	execEnv              string // The exec env, e.g. as "crostini".

	// SSH and tunneling parameters from the profile configuration.
	targetSSHParams *remote.SSHParams
	tunnelParams    *remote.TunnelParams

	// SSH connection to the actual chrome-OS device
	deviceSSH *remote.SSHTarget

	// Machine-info protobuf gathered from device.
	machinePb *db.Machine

	verbose bool
}

// NewDeviceInfoTool create and returns a new DeviceInfoTool objects.
func NewDeviceInfoTool(verbose bool) *DeviceInfoTool {
	return &DeviceInfoTool{
		verbose: verbose,
	}
}

// Setup is used to setup DeviceInfoTool with the device's SSH parameters.
// It must be invoked before calling Run. It is ok to re-use a DeviceInfoTool
// object multiple times by calling Setup/Run in tandem.
func (dit *DeviceInfoTool) Setup(
	getDeviceInfoBinPath string,
	machineName string,
	machineOwner string,
	execEnv string,
	sshParams *remote.SSHParams,
	tunnelParams *remote.TunnelParams) error {

	dit.deviceSSH = nil
	dit.getDeviceInfoBinPath = getDeviceInfoBinPath
	dit.machineName = machineName
	dit.machineOwner = machineOwner
	dit.execEnv = execEnv
	dit.targetSSHParams = sshParams
	dit.tunnelParams = tunnelParams
	dit.machinePb = nil

	if machineName == "" {
		dit.printMachineNameInfoRequirement()
		return fmt.Errorf("no machine name provided in config file")
	}

	return nil
}

// Run runs DeviceInfoTool with the current configuration to collect device
// information from an attached device.
func (dit *DeviceInfoTool) Run() error {
	machinePb, err := dit.recordMachineInfo()
	dit.machinePb = machinePb
	return err
}

// WriteProtoBufToFile writes the protobuf to the output file. The file name is
// derived from the given template. If the template is the empty string, no
// output takes place. The output file format is determined by the filename
// extension and may be either JSON or raw protobuf.
func (dit DeviceInfoTool) WriteProtoBufToFile(dir, fileTemplate string) error {
	// Empty file template means no file output.
	if fileTemplate == "" {
		return nil
	}

	// Build the file name from the template.
	filename := strings.ReplaceAll(fileTemplate, "[[name]]", dit.machineName)
	filename = strings.ReplaceAll(filename, "[[exec-env]]", dit.execEnv)
	filename = strings.ReplaceAll(filename, "[[utc-date]]", GetUTCDateString())
	filename = strings.ReplaceAll(filename, "[[utc-time]]", GetUTCTimeString())
	filename = strings.ReplaceAll(filename, "[[hwid]]", dit.machinePb.Hwid)

	// Create the output Dir if necessary.
	if dir != "" {
		err := os.MkdirAll(dir, os.ModePerm)
		if err != nil {
			return err
		}

		filename = filepath.Join(dir, filename)
	}

	dit.printIfVerbose("Writing protobuf to %s\n", filename)

	return gen_db.WriteProtobuf(dit.machinePb, filename)
}

// Record the machine-info from the device reachable through SSH by running
// the get_device_info tool on that device. On success, a machine-info
// protobuf is returned.
func (dit *DeviceInfoTool) recordMachineInfo() (*db.Machine, error) {
	// Get a SSH connection to the actual device (not a VM or crouton).
	ssh, err := dit.getChromeOSDeviceSSH()
	if err != nil {
		return nil, err
	}
	defer ssh.Disconnect()

	// Create a temp folder on target for binaries and data.
	tmpDir, err := ssh.MkTempDir("./")
	if err != nil {
		return nil, err
	}
	defer ssh.DelFile(tmpDir)

	// If the path to the get_device_info tool in the config is not empty, then
	// copy the tool binaries to the device.
	toolPath := ""
	if dit.getDeviceInfoBinPath != "" {
		toolPath, err = dit.installToolOnTarget(tmpDir)
		if err != nil {
			return nil, err
		}
	}

	// Run the tool to get the machine info.
	output, err := dit.runToolForMachineInfo(toolPath)
	if err != nil {
		return nil, err
	}

	// Take output from running the tool, which should be JSON, and create a
	// machine-info protobuf.
	machineInfo := db.Machine{}
	err = jsonpb.Unmarshal(strings.NewReader(output), &machineInfo)
	return &machineInfo, err
}

// Return a SSH connection to the Chrome-OS device.
func (dit *DeviceInfoTool) getChromeOSDeviceSSH() (*remote.SSHTarget, error) {
	if dit.deviceSSH != nil {
		return dit.deviceSSH, nil
	}

	// Get the SSH parameters to the Chrome-OS device. If tunneling parameters are
	// specified, we must use the tunnel's server SSH parameters to reach the device.
	sshParams := dit.targetSSHParams
	if dit.tunnelParams != nil {
		sshParams = &dit.tunnelParams.Server
	}

	if sshParams == nil {
		return nil, fmt.Errorf("no SSH config available for target device")
	}

	var err error
	dit.deviceSSH, err = remote.CreateSSHTargetWithParams(sshParams)
	if err != nil {
		return nil, err
	}

	err = dit.deviceSSH.Connect()
	return dit.deviceSSH, err
}

// Install the get_device_info tool on the SSH target. Return the full path to
// the tool's binary.
func (dit *DeviceInfoTool) installToolOnTarget(dir string) (string, error) {
	dstFilename := filepath.Join(dir, getInfoToolName)
	dit.printIfVerbose("Installing tool on device: %s\n", dstFilename)
	err := dit.deviceSSH.SendFile(dit.getDeviceInfoBinPath, dstFilename, "0755")
	return dstFilename, err
}

// Run the get_device_info tool on the target device to get machine info.
func (dit *DeviceInfoTool) runToolForMachineInfo(toolPath string) (string, error) {
	// If toolpath is "", we did not install the tool. Instead, we assume it is
	// pre-installed and available in PATH, so that it can be invoked by its name.
	if toolPath == "" {
		toolPath = getInfoToolName
	}

	// Setup cmd-line options as mandated by the machine configuration.
	cmd := fmt.Sprintf("%s machine-info", toolPath)
	if dit.machineName != "" {
		cmd = cmd + " -name " + dit.machineName
	}
	if dit.machineOwner != "" {
		cmd = cmd + " -owner " + dit.machineOwner
	}

	dit.printIfVerbose("Run cmd on device: \"%s\"\n", cmd)

	return dit.deviceSSH.RunCmd(cmd)
}

// If verbose mode is enabled, print the formatted string.
func (dit *DeviceInfoTool) printIfVerbose(format string, a ...interface{}) {
	if dit.verbose {
		fmt.Printf(format, a...)
	}
}

// Print useful information about the requirement for a unique machine name
// in the configuration file.
func (dit *DeviceInfoTool) printMachineNameInfoRequirement() {
	uuidMachineName := uuid.New().String()
	fmt.Fprintf(os.Stderr,
		"Error: A globally unique machine name must be provided in the configuration\n"+
			"file. You may use the uuid \"%s\", or something\n"+
			"like \"<ldap>-<asset ID>\", where the asset-ID is taken from the asset tag,\n"+
			"or you may create your own unique machine name. Once you have chosen a\n"+
			"machine name, enter it in the JSON config file under Device -> name \n"+
			"for device %s.\n", uuidMachineName, dit.execEnv)
}
