# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean
GOGET=$(GOCMD) get
GOFORMAT=$(GOCMD) fmt
GOENV=$(GOCMD) env
export GO111MODULE=off

SRCS=main.go
BIN=../../bin/get_device_info

REMOTE_PACKS= \
	google.golang.org/protobuf/encoding/protojson \
	google.golang.org/protobuf/proto \
	google.golang.org/protobuf/types/known/timestamppb \
	github.com/google/uuid \
	go.chromium.org/chromiumos/config/go/api/test/results/v1 \
	go.chromium.org/chromiumos/config/go/api/test/results/graphics/v1 \
	golang.org/x/crypto/ssh

all: build
build: import
import: env
format: env
clean: env

import:
	$(GOGET) $(REMOTE_PACKS)

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
