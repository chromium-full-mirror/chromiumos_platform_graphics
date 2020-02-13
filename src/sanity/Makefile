# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Top level Makefile for all go projects
GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean

CROS_RETRACE_SRCS=./cmd/world/
CROS_RETRACE_BIN=./bin/world

all: build
build:
	$(GOBUILD) -o $(CROS_RETRACE_BIN) $(CROS_RETRACE_SRCS)

clean:
	$(GOCLEAN)
	rm -f $(CROS_RETRACE_BIN)
