# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Top level Makefile for all go projects
GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean

SRC_DIR=./cmd
BIN_DIR=./bin
BINARIES=$(notdir $(wildcard $(SRC_DIR)/*))

define make-t-target
  target_$1:
	$(GOBUILD) -o $(BIN_DIR)/$1 $(SRC_DIR)/$1/
  all:: target_$1
endef
$(foreach binary,$(BINARIES),$(eval $(call make-t-target,$(binary))))

clean:
	$(GOCLEAN)
	rm -f -r $(BIN_DIR)
