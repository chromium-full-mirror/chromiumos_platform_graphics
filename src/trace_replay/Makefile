# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Top level Makefile for all go projects
GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean

SRC_DIR=./cmd
BIN_DIR=./bin
TRACE_REPLAY_SRCS=$(SRC_DIR)/trace_replay/
TRACE_REPLAY_BIN=$(BIN_DIR)/trace_replay

all: build
build:
	$(GOBUILD) -o $(TRACE_REPLAY_BIN) $(TRACE_REPLAY_SRCS)

clean:
	$(GOCLEAN)
	rm -f -r $(BIN_DIR)
