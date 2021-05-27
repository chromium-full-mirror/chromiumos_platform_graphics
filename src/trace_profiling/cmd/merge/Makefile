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

SRCS=merge.go
BIN=../../bin/merge

all: build
build: env
format: env
clean: env

build:
	$(GOBUILD) -o $(BIN) $(SRCS)

clean:
	$(GOCLEAN)
	rm -f $(BIN)

format:
	$(GOFORMAT) $(SRCS)

env:
	# Restore GOROOT to default.
	$(GOENV) -u GOROOT
	# Custom GOPATH for compatibility with emerge build.
	$(GOENV) -w GOPATH=$$HOME/go:$$PWD/../../../..
