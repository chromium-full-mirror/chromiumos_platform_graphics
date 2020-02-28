// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package remote

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
)

// TunnelParams encapsulates the parameters needed to setup SSH tunneling.
type TunnelParams struct {
	// Local port to forward.
	LocalPort int `json:"localPort"`
	// Target host address and port.
	TargetHostAddr string `json:"targetHostAddr"`
	TargetPort     int    `json:"targetPort"`
	// Parameters for SSH server handling port forwarding.
	Server SSHParams `json:"server"`
}

// Tunnel provides support to setup data-tunneling through a SSH server.
type Tunnel struct {
	params       TunnelParams
	ssh          *SSHTarget
	connListener net.Listener
}

// CreateTunnel creates and return a Tunnel object using the provided tunnel
// parameters.
func CreateTunnel(params *TunnelParams) (*Tunnel, error) {
	var err error
	tunnel := Tunnel{*params, nil, nil}
	tunnel.ssh, err = CreateSSHTargetWithParams(&params.Server)
	if err != nil {
		return nil, err
	}
	return &tunnel, nil
}

// BeginTunneling initiates data tunneling through the tunnel. Once all the
// connections have been established and the tunnel is ready to accept
// connection requests to the local port, Tunnel sends true to channel
// tunnelReady. BeginTunneling is a blocking function that doesn't return until
// the tunnel is close (see function Close below). In case of failure,
// BeginTunneling returns the error immediately.
func (t *Tunnel) BeginTunneling(tunnelReady chan bool) error {
	// Establish a TCP connection to the SSH server.
	err := t.ssh.Connect()
	if err != nil {
		return fmt.Errorf("SSH Server connection error: %w", err)
	}
	defer t.ssh.Disconnect()

	// Establish a TCP connection from server to target.
	targetAddr := fmt.Sprintf("%s:%d", t.params.TargetHostAddr, t.params.TargetPort)
	targetConn, err := t.ssh.GetConnection().Dial("tcp", targetAddr)
	if err != nil {
		return fmt.Errorf("Target connection error: %w", err)
	}
	defer targetConn.Close()

	// Setup the local TCP-connection listener.
	localAddr := fmt.Sprintf("localhost:%d", t.params.LocalPort)
	t.connListener, err = net.Listen("tcp", localAddr)
	if err != nil {
		return fmt.Errorf("Local connection error: %w", err)
	}

	// The tunnel is setup and ready to listen for local connection requests.
	tunnelReady <- true

	for {
		localConn, err := t.connListener.Accept()
		if err != nil {
			if opErr, ok := err.(*net.OpError); ok &&
				opErr.Err.Error() == "use of closed network connection" {
				// This specific error happens when we close the listener (func Close
				// below). It's an expected error and can safely be ignored.
				break
			}
			err = fmt.Errorf("Tunneling error: %w", err)
			fmt.Println(err.Error())
			break
		}

		go func(local, target net.Conn) {
			t.doDataTunneling(local, target)
			local.Close()
		}(localConn, targetConn)
	}

	tunnelReady <- false
	return err
}

// Close the tunnel. After this call, the tunnel no longer accepts connections.
// It causes function BeginTunneling above returns without error, after sending
// false to channel tunnelReady.
func (t *Tunnel) Close() {
	if t.connListener != nil {
		t.connListener.Close()
	}
}

// ReadTunnelParamsFromJSON is a convenience function to read tunnel parameters
// from a JSON file. Returns a TunnelParams object or an error.
func ReadTunnelParamsFromJSON(jsonFilepath string) (*TunnelParams, error) {
	file, err := os.Open(jsonFilepath)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot open tunnel param file <%s>; error=%w",
			jsonFilepath, err)
	}
	defer file.Close()

	jsonData, err := ioutil.ReadAll(file)
	if err != nil {
		return nil, fmt.Errorf("Error: Cannot read tunnel param file <%s>; error=%w",
			jsonFilepath, err)
	}

	var params TunnelParams
	json.Unmarshal(jsonData, &params)
	return &params, nil
}

// Function doDataTunneling sends data back and forth between the local and
// remote connections. It is a blocking function that returns either when both
// connections are closed or one connection issues an error.
func (t *Tunnel) doDataTunneling(local net.Conn, remote net.Conn) error {
	errChannel := make(chan error, 2)
	doneCh := make(chan bool, 2)

	// Initiate remote -> local data transfer.
	go func(errChan chan error) {
		var err error
		if _, err = io.Copy(local, remote); err != nil {
			errChan <- fmt.Errorf("Data transfer error: %w", err)
		}
		doneCh <- true
	}(errChannel)

	// Initiate local -> remote data transfer.
	go func(errChan chan error) {
		var err error
		if _, err = io.Copy(remote, local); err != nil {
			errChan <- fmt.Errorf("Data transfer error: %w", err)
		}
		doneCh <- true
	}(errChannel)

	// Block until all data tunneling is done or an error is issued.
	doneCount := 0
	for {
		select {
		case err := <-errChannel:
			return err
		case <-doneCh:
			doneCount++
			if doneCount == 2 {
				return nil
			}
		}
	}
}
