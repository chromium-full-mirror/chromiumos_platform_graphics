# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean
GOGET = $(GOCMD) get
GOFORMAT = $(GOCMD) fmt
GOENV = $(GOCMD) env
export GO111MODULE=off

SRCS=main.go
BIN=../../bin/profile

REMOTE_PACKS= \
	golang.org/x/crypto/ssh

all: build
build: import
import: env
format: env
clean: env

build:
	$(GOBUILD) -o $(BIN) $(SRCS)

import:
	$(GOGET) $(REMOTE_PACKS)

clean:
	$(GOCLEAN)
	rm -f $(BIN)

format:
	$(GOFORMAT) $(SRCS)
	$(GOFORMAT) profile/*.go
	$(GOFORMAT) remote/*.go

env:
	# Restore GOROOT to default.
	$(GOENV) -u GOROOT
	# Custom GOPATH for compatibility with emerge build.
	$(GOENV) -w GOPATH=$$HOME/go:$$PWD/../../../..
