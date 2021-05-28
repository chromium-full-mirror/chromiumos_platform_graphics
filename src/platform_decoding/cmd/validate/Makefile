# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean

VALIDATE_SRCS=main.go
VALIDATE_BIN=../../bin/validate

all: build
build:
	$(GOBUILD) -o $(VALIDATE_BIN) $(VALIDATE_SRCS)

clean:
	$(GOCLEAN)
	rm -f -r $(BIN_DIR)
