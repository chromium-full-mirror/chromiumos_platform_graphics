#!/bin/bash

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

echo "Setting up virtualenv for pip-compile."
VENV_TEMP="$(mktemp -d)"
VENV_ROOT="${VENV_TEMP}/requirements"
python3 -m venv "${VENV_ROOT}"
# shellcheck disable=SC1091
source "${VENV_ROOT}/bin/activate"

echo "Installing pip-compile dependencies."
pip install --require-hashes -r base-tooling-requirements.txt

echo "Compiling hashes for Benchy dependencies."
pip-compile requirements.in --generate-hashes --upgrade
