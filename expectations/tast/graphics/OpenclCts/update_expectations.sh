#!/bin/bash
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -xe

# Usage: bash update_expectations.sh "<build_regex>" "<chipset(s)>"
[[ $# -eq 2 ]]

SCRIPT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
CREATE_EXPECTATIONS="${SCRIPT_DIR}/../../create_expectations_from_stainless.go"

chipsets=(
    # ARM
    "mali-g57"
    "mali-g52"
    "mali-g72"
    "mali-g925-immortalis"
    # Intel
    "alderlake"
    "apollolake"
    "cometlake"
    "geminilake"
    "jasperlake"
    "kabylake"
    "meteorlake"
    "raptorlake"
    "tigerlake"
    "whiskeylake"
    # Qualcomm
    "sc7180"
    "sc7280"
    # AMD
    "picasso"
    "stoney"
    "cezanne"
    "gc_10_3_7"
    # Imagination
    "rogue"
)

if [ "$2" != "all" ]
then
   chipsets=("$2")
fi

for chipset in "${chipsets[@]}"
do
    touch "chipset-${chipset}.yml"
    ~/chromiumos/src/platform/tast/tools/go.sh \
        run \
        "${CREATE_EXPECTATIONS}" \
        --input "chipset-${chipset}.yml" \
        --update_input \
        --gpu_family "${chipset}" \
        --exclude_board_regex "-kernelnext$" \
        --test_regex "OpenclCts" \
        --build_regex "$1"
done
