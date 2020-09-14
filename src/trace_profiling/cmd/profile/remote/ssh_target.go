// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package remote

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"
)

// A unique and easily recognizable shell prompt. It's used by shellSession
// as a way of detecting the end of a command's output.
const shellPrompt = ">>->>"

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
	shell               *shellSession
	extraSCPFileSpaceMb int
}

// Type shellSession provides support for a shell-based SSH session. Unlike
// ssh.Session, shellSession makes it possible to run several commands while
// preserving the context between these commands, much like a SSH shell.
type shellSession struct {
	session      *ssh.Session   // The underlying ssh Session.
	shellStdin   io.WriteCloser // Shell's stdin, used to write command to the shell.
	shellStdout  *bufio.Reader  // Shell's stdout.
	readBuffer   []byte         // Buffer for reading from the shell's stdout.
	stdout       io.Writer      // Command's output is pipe to this writer.
	statusBuffer bytes.Buffer   // Buffer used to read last command status

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
		Timeout: 5 * time.Minute,
	}

	return &SSHTarget{
		params:              *params,
		config:              client,
		connection:          nil,
		shell:               nil,
		extraSCPFileSpaceMb: 10 << 10 /* 10Mb extra when sending files */}, nil
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

// Set the shell's writer that will receive the command's output. Set stdout
// to nil to discard the output.
func (shell *shellSession) setStdout(stdout io.Writer) {
	shell.stdout = stdout
}

// Run a command on the shell and pipe the output to shell.stdout.
func (shell *shellSession) Run(cmd string) error {
	// Ensure the command ends with a single newline char before writing the shell's stdin.
	_, err := fmt.Fprintf(shell.shellStdin, "%s\n", strings.TrimSpace(cmd))
	if err != nil {
		return err
	}

	// Read the command output to shell.stdout.
	shell.readCommandOutput(shell.stdout)

	// Get the command status by running "echo $?".
	_, err = fmt.Fprintf(shell.shellStdin, "echo $?\n")
	if err != nil {
		return err
	}

	shell.statusBuffer.Truncate(0)
	shell.readCommandOutput(&shell.statusBuffer)
	status := strings.TrimSpace(shell.statusBuffer.String())
	if status != "0" {
		return fmt.Errorf("cmd status = %s, output = %s", status, shell.stdout)
	}

	return nil
}

// Read the shell's output after executing a command. The output is sanitized so
// as to remove the shell's prompt line and the resulting command output is
// send to shell.stdout.
func (shell *shellSession) readCommandOutput(out io.Writer) {
	// This index is used to match the input against the shell's prompt.
	iMatchPrompt := 0
	for {
		b, err := shell.shellStdout.ReadByte()
		if err != nil {
			break
		}

		shell.readBuffer = append(shell.readBuffer, b)

		// We expect the last thing to come through the output to be the shell's
		// prompt. Once that happens, we're done processing the output for that
		// command.
		if b == shellPrompt[iMatchPrompt] {
			iMatchPrompt++
			if iMatchPrompt == len(shellPrompt) {
				// Shell prompt is matched. that signals the end of output.
				shell.flushReadBuffer(out)
				break
			}
		} else {
			iMatchPrompt = 0
			if b == '\n' {
				// Flush the read buffer at the end of each line.
				shell.flushReadBuffer(out)
			}
		}
	}
}

// Flush and clear the content of the read buffer. Only the line that are part of
// the command's output are piped to out. The line with the shell's prompt
// is discarded.
func (shell *shellSession) flushReadBuffer(out io.Writer) {
	if out != nil && len(shell.readBuffer) > 0 {
		s := string(shell.readBuffer)
		s = strings.TrimRight(s, "\r\n")
		if !strings.HasSuffix(s, shellPrompt) {
			fmt.Fprintf(out, "%s\n", s)
		}
	}

	shell.readBuffer = shell.readBuffer[:0]
}

// Close the shell session.
func (shell *shellSession) Close() error {
	if shell.session != nil {
		err := shell.session.Close()
		shell.session = nil
		return err
	}

	return nil
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
	if s.shell != nil {
		s.shell.Close()
		s.shell = nil
	}

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

// OpenShellSession opens a shell session. Once a shell session is opened, all
// commands executed through RunCmd and RunCmdWithWriter execute within the
// context of that shell, similarly to running commands manually in a SSH shell.
func (s *SSHTarget) OpenShellSession() error {
	if s.shell != nil {
		return fmt.Errorf("a shell session is already opened")
	}

	session, err := s.connection.NewSession()
	if err != nil {
		return fmt.Errorf("SSHTarget shell-session: %w", err)
	}

	stdin, err := session.StdinPipe()
	if err != nil {
		session.Close()
		return err
	}

	stdout, err := session.StdoutPipe()
	if err != nil {
		session.Close()
		return err
	}

	modes := ssh.TerminalModes{
		ssh.ECHO:          0, // disable echoing
		ssh.TTY_OP_ISPEED: 14400,
		ssh.TTY_OP_OSPEED: 14400,
	}

	err = session.RequestPty("xterm", 80, 200, modes)
	if err != nil {
		session.Close()
		return err
	}

	err = session.Shell()
	if err != nil {
		session.Close()
		return err
	}

	shell := &shellSession{
		session:     session,
		shellStdin:  stdin,
		shellStdout: bufio.NewReader(stdout),
		readBuffer:  make([]byte, 0, 512),
	}

	s.shell = shell

	// Set a custom bash prompt that we can easily recognize when processing
	// the output from commands. Discard the output from that command.
	fmt.Fprint(stdin, "PS1=\""+shellPrompt+"\"\n")
	s.shell.readCommandOutput(nil)

	return nil
}

// CloseShellSession closes the current shell session. A no-op if there is no
// ongoing shell session.
func (s *SSHTarget) CloseShellSession() error {
	if s.shell != nil {
		err := s.shell.Close()
		s.shell = nil
		return err
	}

	return nil
}

// ListFiles returns a list of files found in dir that match the given glob
// pattern. Files are returned as a slice with each entry of the form
// "<dir>/filename".
func (s *SSHTarget) ListFiles(dir, glob string) ([]string, error) {
	// use ls to list all files with single-column format.
	path := dir
	if glob != "" {
		path = filepath.Join(dir, glob)
	}
	cmd := fmt.Sprintf("ls -1 %s", path)
	output, err := s.RunCmd(cmd)
	if err != nil {
		return nil, err
	}

	// Extract the files from each line of output.
	lines := strings.Split(output, "\n")
	files := make([]string, 0, len(lines))
	for _, l := range lines {
		f := strings.TrimSpace(l)
		if f != "" {
			files = append(files, f)
		}
	}

	return files, nil
}

// CheckFileExists returns whether the file at the given path exists on the target.
func (s *SSHTarget) CheckFileExists(filePath string) (bool, error) {
	cmd := fmt.Sprintf(
		"if [ -f \"%s\" ]; then (echo present) fi", strings.TrimSpace(filePath))
	output, err := s.RunCmd(cmd)
	return strings.HasPrefix(output, "present"), err
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

// MkTempDir creates a temporary directory on the target and returns its path.
// The directory is created relative to <base> or /tmp if base is "".
func (s *SSHTarget) MkTempDir(base string) (string, error) {
	var cmd string
	if base != "" {
		cmd = fmt.Sprintf("mktemp -d -p %s", base)
	} else {
		cmd = fmt.Sprintf("mktemp -d")
	}
	out, err := s.RunCmd(cmd)
	if err != nil {
		return "", err
	}

	// Ensure we always return an absolute path.
	cmd = fmt.Sprintf("realpath %s", out)
	out, err = s.RunCmd(cmd)
	if err != nil {
		return "", err
	}

	out = strings.TrimSuffix(out, "\n")
	return out, err
}

// DelFile removes the file or dir at the given path on the target.
func (s *SSHTarget) DelFile(filePath string) error {
	cmd := fmt.Sprintf("rm -f -r \"%s\"", strings.TrimSpace(filePath))
	_, err := s.RunCmd(cmd)
	return err
}

// RunCmd runs a shell command, presented as a string, over the current
// connection. Returns the command output or an error. (Also see RunCmdWithWriter.)
func (s *SSHTarget) RunCmd(command string) (string, error) {
	var outBuffer bytes.Buffer
	err := s.RunCmdWithWriter(command, &outBuffer)
	if err != nil {
		return "", err
	}

	return outBuffer.String(), nil
}

// RunCmdWithWriter runs a command on the target and pipes the output from
// stdout to outWriter. If a shell session is opened (see OpenShellSession),
// the command runs in the context of that shell. Otherwise, the command runs
// within a one-time ssh Session.
func (s *SSHTarget) RunCmdWithWriter(command string, outWriter io.Writer) error {
	if s.connection == nil {
		return fmt.Errorf("RunCmdWithWriter: Not connected")
	}

	type cmdSession interface {
		Close() error
		Run(cmd string) error
	}

	// If there's an ongoing shell session use it. Otherwise, create a one-time
	// session to run the command.
	var session cmdSession
	var errBuffer = bytes.Buffer{}
	if s.shell != nil {
		s.shell.setStdout(outWriter)
		session = s.shell
	} else {
		sshSession, err := s.connection.NewSession()
		if err != nil {
			return fmt.Errorf("SSHTarget run-cmd error: %w", err)
		}
		sshSession.Stdout = outWriter
		sshSession.Stderr = &errBuffer
		defer sshSession.Close()

		session = sshSession
	}

	err := session.Run(command)
	if err != nil {
		if errBuffer.Len() > 0 {
			return fmt.Errorf("Remote cmd error: %s", errBuffer.String())
		}
		return fmt.Errorf("Remote cmd error: %w", err)
	}

	return nil
}

// SendFile copies a file to the target using the SCP protocol.
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
	dirPath = strings.TrimSpace(dirPath)
	dfOut, err := s.RunCmd(fmt.Sprintf("df -Pk %s | tail -1 | awk '{print $4}'", dirPath))
	if err != nil {
		return -1, fmt.Errorf("failed to get free space for \"%s\" on target: %s",
			dirPath, err.Error())
	}

	freeSpace, err := strconv.Atoi(strings.TrimSpace(dfOut))
	if err != nil {
		return -1, fmt.Errorf("failed to get free space for \"%s\" on target", dirPath)
	}

	return freeSpace, nil
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
