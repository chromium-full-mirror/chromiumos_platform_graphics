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

REMOTE_PACKS= \
	golang.org/x/image/colornames \
	gonum.org/v1/plot \
	gonum.org/v1/plot/plotter \
	github.com/chzyer/readline

ANALYZE_SRCS=main.go
ANALYZE_BIN=../../bin/analyze

all: build
build: import
import: env
format: env
clean: env

import:
	$(GOGET) $(REMOTE_PACKS)
build:
	$(GOBUILD) -o $(ANALYZE_BIN) $(ANALYZE_SRCS)

clean:
	$(GOCLEAN)
	rm -f $(ANALYZE_BIN)

format:
	$(GOFORMAT) main.go
	$(GOFORMAT) ./analyze/*.go

env:
	# Restore GOROOT to default.
	$(GOENV) -u GOROOT
	# Custom GOPATH for compatibility with emerge build.
	$(GOENV) -w GOPATH=$$HOME/go:$$PWD/../../../..
