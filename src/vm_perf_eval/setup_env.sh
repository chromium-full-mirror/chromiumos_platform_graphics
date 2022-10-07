#!/bin/bash
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

git clone https://github.com/tianocore/edk2.git --recurse-submodules
cd edk2 || die 'edk2 directory does not exist! Check Git checkout'
(
cd BaseTools || die 'edk2/BaseTools does not exist! Check Git checkout'
make
)
ln -s ../VmPerfEvalPkg VmPerfEvalPkg
