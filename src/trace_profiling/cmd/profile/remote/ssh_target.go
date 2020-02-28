// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package remote

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"
)

// SSHParams parameters needed to contact and log into a SSH server.
// Password and PublicKeyFilepath may not be needed depending on the server
// configuration. Is such is the case, they can be set to the empty string.
type SSHParams struct {
	// Server address and port.
	Addr string `json:"addr"`
	Port int    `json:"port"`
	// SSH login credentials.
	Username          string `json:"username"`
	Password          string `json:"password"`
	PublicKeyFilepath string `json:"publicKeyFilepath"`
}

// SSHTarget represents a remote target device, reachable by SSH, which can
// run shell commands and receive files.
type SSHTarget struct {
	params              SSHParams
	config              *ssh.ClientConfig
	connection          *ssh.Client
	extraSCPFileSpaceMb int
}

// CreateSSHTargetWithParams creates a SSHTarget object from the given SSH
// parameters.
func CreateSSHTargetWithParams(params *SSHParams) (*SSHTarget, error) {
	auth := make([]ssh.AuthMethod, 0, 2)
	if params.PublicKeyFilepath != "" {
		var err error
		var buff []byte
		if buff, err = ioutil.ReadFile(params.PublicKeyFilepath); err != nil {
			return nil, err
		}

		var key ssh.Signer
		if key, err = ssh.ParsePrivateKey(buff); err != nil {
			return nil, err
		}

		auth = append(auth, ssh.PublicKeys(key))
	}

	if params.Password != "" {
		auth = append(auth, ssh.Password(params.Password))
	}

	client := &ssh.ClientConfig{
		User: params.Username,
		Auth: auth,
		HostKeyCallback: func(hostname string, remote net.Addr, key ssh.PublicKey) error {
			// Always accept key.
			return nil
		},
	}

	return &SSHTarget{*params, client, nil, 10 << 10 /* 10Mb extra when sending files */}, nil
}

// CreateSSHParamsFromJSON reads SSH parameters from a json file and returns
// a SSHParams object.
func CreateSSHParamsFromJSON(jsonFilepath string) (*SSHParams, error) {
	file, err := os.Open(jsonFilepath)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot open SSH config file <%s>; error=%w",
			jsonFilepath, err)
	}
	defer file.Close()

	jsonData, err := ioutil.ReadAll(file)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot read SSH config file <%s>; error=%w",
			jsonFilepath, err)
	}

	var config SSHParams
	json.Unmarshal(jsonData, &config)
	return &config, nil
}

// Connect initiates a connection to a SSH server given parameters presented
// as a SSHParams object.
func (s *SSHTarget) Connect() error {
	if s.connection != nil {
		return fmt.Errorf("SSHTarget is already connected")
	}

	var err error
	addr := fmt.Sprintf("%s:%d", s.params.Addr, s.params.Port)
	s.connection, err = ssh.Dial("tcp", addr, s.config)
	if err != nil {
		return fmt.Errorf("SSHTarget connection error: %w", err)
	}
	return nil
}

// Disconnect closes the current connection.
func (s *SSHTarget) Disconnect() error {
	if s.connection != nil {
		err := s.connection.Close()
		s.connection = nil
		return err
	}
	return nil
}

// GetConnection return a pointer to the ssh.Client for the current connection,
// or nil if not connected.
func (s *SSHTarget) GetConnection() *ssh.Client {
	return s.connection
}

// CheckFileExists returns whether the file at the given path exists on the target.
func (s *SSHTarget) CheckFileExists(filePath string) (bool, error) {
	cmd := fmt.Sprintf(
		"if [ -f \"%s\" ]; then (echo present) fi", filePath)
	output, err := s.RunCmd(cmd)
	return err == nil && strings.HasPrefix(output, "present"), err
}

// Mkdir make the directories in the given path on the target. Parent directories
// are created as needed.
func (s *SSHTarget) Mkdir(dirPath string) error {
	cmd := fmt.Sprintf("mkdir -p %s", dirPath)
	_, err := s.RunCmd(cmd)
	return err
}

// MkTempFileName creates a temporary file name on the target and returns its path.
func (s *SSHTarget) MkTempFileName() (string, error) {
	cmd := fmt.Sprintf("mktemp -u")
	return s.RunCmd(cmd)
}

// DelFile removes the file at the given path on the target.
func (s *SSHTarget) DelFile(filePath string) error {
	cmd := fmt.Sprintf("rm -f %s", filePath)
	_, err := s.RunCmd(cmd)
	return err
}

// RunCmd runs a shell command, presented as a string, over the current
// connection. Returns the command output or an error.
func (s *SSHTarget) RunCmd(command string) (string, error) {
	var outBuffer bytes.Buffer
	err := s.RunCmdWithWriter(command, &outBuffer)
	if err != nil {
		return "", err
	}

	return outBuffer.String(), nil
}

// RunCmdWithWriter runs a command on the target and pipes the output from
// stdout to outWriter.
func (s *SSHTarget) RunCmdWithWriter(command string, outWriter io.Writer) error {
	if s.connection == nil {
		return fmt.Errorf("RunCmdWithWriter: Not connected")
	}

	session, err := s.connection.NewSession()
	if err != nil {
		return fmt.Errorf("SSHTarget run-cmd error: %w", err)
	}
	defer session.Close()

	var errBuffer bytes.Buffer
	session.Stdout = outWriter
	session.Stderr = &errBuffer

	err = session.Run(command)
	if err != nil {
		return fmt.Errorf("RunCmd error: %w", err)
	}

	return nil
}

// SendFile sends a file to the target using the SCP protocol.
func (s *SSHTarget) SendFile(srcFilename string, dstFilename string, permissions string) error {
	dstFilenameOnly := path.Base(dstFilename)
	dstPathOnly := path.Dir(dstFilename)

	srcFileStat, err := os.Stat(srcFilename)
	if err != nil {
		return err
	}

	// Make sure there's enough space to receive the file. We need
	// file_size + s.extraSCPFileSpaceMb available.
	freeSpaceKb, err := s.GetRemoteFreeSpaceKb(dstPathOnly)
	if err != nil {
		return err
	}

	reqSpaceKb := int(srcFileStat.Size()>>10) + s.extraSCPFileSpaceMb
	if freeSpaceKb < reqSpaceKb {
		return fmt.Errorf("Not enough space. Needed %d MB, available %d MB",
			reqSpaceKb>>10, freeSpaceKb>>10)
	}

	// Estimate transfer time-out values assuming 1Mb/S minimal transfer speed.
	timeout := time.Duration(10+(srcFileStat.Size()>>20)) * time.Second

	session, err := s.connection.NewSession()
	if err != nil {
		return fmt.Errorf("SendFile: error creating new SSH session: %s", err.Error())
	}
	defer session.Close()

	errCh := make(chan error, 2)
	wg := sync.WaitGroup{}
	wg.Add(2)

	go func() {
		errCh <- copyFileWithScp(session, srcFilename, dstFilenameOnly, permissions)
		wg.Done()
	}()

	go func() {
		errCh <- session.Run(fmt.Sprintf("scp -qt %s", dstPathOnly))
		wg.Done()
	}()

	if waitTimeout(&wg, timeout) {
		return fmt.Errorf("SendFile: time-out error")
	}

	close(errCh)
	for err := range errCh {
		if err != nil {
			return err
		}
	}
	return nil
}

// GetRemoteFreeSpaceKb returns the amount of free disk space for the given
// dir path on the target.
func (s *SSHTarget) GetRemoteFreeSpaceKb(dirPath string) (int, error) {
	dfOut, err := s.RunCmd(fmt.Sprintf("df -Pk %s | tail -1 | awk '{print $4}'", dirPath))
	if err != nil {
		return -1, err
	}

	return strconv.Atoi(strings.TrimSpace(dfOut))
}

// Wait on the given waitGroup, but no longer than timeout.
func waitTimeout(wg *sync.WaitGroup, timeout time.Duration) bool {
	c := make(chan struct{})
	go func() {
		defer close(c)
		wg.Wait()
	}()
	select {
	case <-c:
		return false // completed normally
	case <-time.After(timeout):
		return true // timed out
	}
}

// Copy a file to the target using the scp protocol.
func copyFileWithScp(
	session *ssh.Session,
	srcFilepath string,
	dstFilenameOnly string,
	permissions string) error {

	srcFile, err := os.Open(srcFilepath)
	if err != nil {
		return err
	}
	defer srcFile.Close()

	writer, err := session.StdinPipe()
	if err != nil {
		return err
	}
	defer writer.Close()

	stat, err := srcFile.Stat()
	_, err = fmt.Fprintln(writer, "C"+permissions, stat.Size(), dstFilenameOnly)
	if err != nil {
		return err
	}

	_, err = io.Copy(writer, srcFile)
	if err != nil {
		return err
	}

	_, err = fmt.Fprint(writer, "\x00")
	return err
}
