# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean

FFMPEG_MD5SUM_SRCS=main.go
FFMPEG_MD5SUM_BIN=../../bin/ffmpeg_md5sum

all: build
build:
	$(GOBUILD) -o $(FFMPEG_MD5SUM_BIN) $(FFMPEG_MD5SUM_SRCS)

clean:
	$(GOCLEAN)
	rm -f -r $(BIN_DIR)
