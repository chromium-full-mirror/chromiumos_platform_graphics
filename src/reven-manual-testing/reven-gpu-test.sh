#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

RESULTS="/tmp/graphics/results"
DOWNLOADS="/home/chronos/user/MyFiles/Downloads"
rm -rf "${RESULTS}"
mkdir -p "${RESULTS}"
DEQP="/usr/local/deqp"

# TODO(ihf): switch skylake to reven
DEQP_EXPECT="/usr/local/graphics/expectations/deqp/skylake"

# Get hardware info
echo 'Hardware info:' | tee -a "${RESULTS}"/log.txt
/usr/local/graphics/hardware_probe -cpu-soc-family -gpu-family -gpu-vendor \
| tee "${RESULTS}"/hardware_probe.txt | tee -a "${RESULTS}"/log.txt
echo '---' | tee -a "${RESULTS}"/log.txt

# Get reven image/lsb-release info
echo 'Image info:' | tee -a "${RESULTS}"/log.txt
cat /etc/lsb-release \
| tee "${RESULTS}"/lsb-release.txt | tee -a "${RESULTS}"/log.txt
echo '---' | tee -a "${RESULTS}"/log.txt

# Running wflinfo for driver
echo 'Running wflinfo:' | tee -a "${RESULTS}"/log.txt
wflinfo -a gles2 -p null \
| tee "${RESULTS}"/wflinfo.txt | tee -a "${RESULTS}"/log.txt
echo '---' | tee -a "${RESULTS}"/log.txt

# Running modetest
echo 'Running modetest:' | tee -a "${RESULTS}"/log.txt
modetest \
| tee "${RESULTS}"/modetest.txt | tee -a "${RESULTS}"/log.txt
echo '---' | tee -a "${RESULTS}"/log.txt

# Run glbench in hasty mode:
echo 'Running glbench hasty:' | tee -a "${RESULTS}"/log.txt
/usr/local/glbench/bin/glbench -outdir="${RESULTS}"/graphics.GLBench.hasty \
                               -save -hasty \
| tee "${RESULTS}"/glbench-hasty.txt | tee -a "${RESULTS}"/log.txt
echo '---' | tee -a "${RESULTS}"/log.txt

# Run dEQP gles2
echo 'Running dEQP gles2:' | tee -a "${RESULTS}"/log.txt
deqp-runner run --output="${RESULTS}"/deqp-runner \
                --deqp="${DEQP}"/modules/gles2/deqp-gles2 \
                --testlog-to-xml="${DEQP}"/executor/testlog-to-xml \
                --caselist="${DEQP}"/caselists/gles2.txt \
                --flakes="${DEQP_EXPECT}"-flakes.txt \
                --skips="${DEQP_EXPECT}"-skips.txt \
                --baseline="${DEQP_EXPECT}"-fails.txt \
                -- \
                --deqp-surface-type=pbuffer \
                --deqp-surface-width=256 \
                --deqp-surface-height=256 \
                --deqp-gl-config-name=rgba8888d24s8ms0 \
| tee "${RESULTS}"/deqp-gles2.txt | tee -a "${RESULTS}"/log.txt
echo '---' | tee -a "${RESULTS}"/log.txt

# Copy results to Downloads folder so they can be attached to a feedback report
cp "${RESULTS}"/log.txt "${DOWNLOADS}"/log.txt

# On older devices this can take > 20 minutes and is not always supported.
# TODO(ihf): enable when all reven devices support it.
## Run dEQP gles3
#echo 'Running dEQP gles3:' | tee -a "${RESULTS}"/log.txt
#deqp-runner run --output="${RESULTS}"/deqp-runner \
#                --deqp="${DEQP}"/modules/gles3/deqp-gles3 \
#                --testlog-to-xml="${DEQP}"/executor/testlog-to-xml \
#                --caselist="${DEQP}"/caselists/gles3.txt \
#                --flakes="${DEQP_EXPECT}"-flakes.txt \
#                --skips="${DEQP_EXPECT}"-skips.txt \
#                --baseline="${DEQP_EXPECT}"-fails.txt \
#                -- \
#                --deqp-surface-type=pbuffer \
#                --deqp-surface-width=256 \
#                --deqp-surface-height=256 \
#                --deqp-gl-config-name=rgba8888d24s8ms0 \
#| tee "${RESULTS}"/deqp-gles3.txt | tee -a "${RESULTS}"/log.txt
#echo '---' | tee -a "${RESULTS}"/log.txt

echo "Full output is in ""${RESULTS}"", log.txt is in Downloads folder."
