#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -xe

# Usage: bash update_expectations.sh "<build_regex>" "<chipset(s)>"
[[ $# -eq 2 ]]

SCRIPT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
CREATE_EXPECTATIONS="${SCRIPT_DIR}/../../create_expectations_from_stainless.go"

declare -A all_chipsets
#ARM
all_chipsets[mali-g57]="asurada|cherry"
all_chipsets[mali-g52]="corsola"
all_chipsets[mali-g72]="jacuzzi|kukui"
# Intel
all_chipsets[kabylake]="atlas|eve|fizz|kalista|nami|nautilus|nocturne|rammus"
all_chipsets[kabylake]+="|soraka"
all_chipsets[alderlake]="brask|brya|nissa"
all_chipsets[jasperlake]="dedede|keeby"
all_chipsets[cometlake]="drallion|hatch|puff"
all_chipsets[geminilake]="octopus"
all_chipsets[tigerlake]="volteer"
# Qualcomm
all_chipsets[sc7180]="strongbad|trogdor"
all_chipsets[sc7280]="herobrine"
# AMD
all_chipsets[picasso]="zork"
all_chipsets[stoney]="grunt"
all_chipsets[cezanne]="guybrush"
all_chipsets[gc_10_3_7]="skyrim"
# Imagination
all_chipsets[rogue]="elm|hana"

declare -A exclude_models
exclude_models[alderlake]="pujjoteen15w"

if [ "$2" == "all" ]
then
    IFS=" " read -r -a chipsets <<< "${!all_chipsets[@]}"
else
    IFS=" " read -r -a chipsets <<< "$2"
fi

for chipset_id in "${!chipsets[@]}"
do
    chipset="${chipsets[${chipset_id}]}"
    board_regex="^(${all_chipsets[${chipset}]})$"
    exclude_model_regex="^(${exclude_models[${chipset}]})$"

    touch "chipset-${chipset}.yml"
    ~/chromiumos/src/platform/tast/tools/go.sh \
        run \
        "${CREATE_EXPECTATIONS}" \
        --input "chipset-${chipset}.yml" \
        --update_input \
        --board_regex "${board_regex}" \
        --exclude_model_regex "${exclude_model_regex}" \
        --exclude_reason_regex \
        "deadline exceeded|Failed to set up Proton environment|[Ll]ost SSH connection|Tast command timeout|Failed to create test API connection|Failed to start borealis" \
        --test_regex "borealis.DxvkUnittest" \
        --build_regex "$1"
done
