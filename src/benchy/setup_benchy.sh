#!/bin/bash

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

# Run in script directory.
SCRIPT_PATH=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd "${SCRIPT_PATH}"

# Set up Python venv.
VENV="${HOME}/.benchy_venv"
if [[ ! -d "${VENV}" ]]; then
  echo "Creating Python venv in ${VENV}."
  python3 -m venv "${VENV}"
fi
echo "Entering Python venv in ${VENV}."
# shellcheck disable=SC1091
source "${VENV}/bin/activate"

echo "Installing/updating Python dependencies."
pip3 install --require-hashes -r requirements.txt
pip3 install --no-deps --no-index --no-build-isolation \
  ../../../../../src/config/python \
  ../results_database

echo "Setting vars and entering bash."
export PATH="${PATH}:${SCRIPT_PATH}/../results_database:${SCRIPT_PATH}"

# For BigQuery upload.
export CLOUDSDK_CORE_PROJECT=chromeos-graphics
export CLOUDSDK_BILLING_QUOTA_PROJECT=chromeos-graphics

# TODO Remove this once Benchy's protos are compiled with protoc >= 3.19.0.
export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python

# shellcheck disable=SC2016
bash --rcfile <(cat ~/.bashrc; echo 'PS1="(benchy) $PS1"')
