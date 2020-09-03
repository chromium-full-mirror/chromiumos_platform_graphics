#!/bin/bash
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z "$1" ]] || [[ -z "$2" ]] || [[ -z "$3" ]] || [[ -z "$4" ]]; then
  echo "Run from desktop to print periodic CSV with memory stats."
  echo "Usage: $0 <target-host> <target-guest> <interval-in-seconds>"
  echo "       <guest-process-name>"
  echo "Example: $0 sona buster 3 glretrace"
  exit 1
fi

host="$1"
guest="$2"
interval_seconds="$3"
guest_proc_name="$4"

# Test ssh connections.
ssh "${host}" true || { echo "Error: could not ssh to ${host}" ; exit 1; }
ssh "${guest}" true || { echo "Error: could not ssh to ${guest}" ; exit 1; }

# Collect crosvm process IDs from host.
crosvm_pids=$(ssh "${host}" pgrep crosvm)
# Get the crosvm GPU PID -- it has a thread named virtio_gpu.
# First get the thread TID from pstree output:
virtio_gpu_tid=$(ssh "${host}" pstree -pt | grep virtio_gpu | \
    sed -e 's/.*virtio_gpu}(//' -e 's/).*//')
if ! [[ "${virtio_gpu_tid}" =~ ^[0-9]+$ ]]; then
  echo "Error: could not find virtio_gpu_tid."; exit 1
fi
# Then grep ps results for the TID to get the PID:
virtio_gpu_pid=$(ssh "${host}" ps -AT | grep "\s${virtio_gpu_tid}\s" | \
    sed -e 's/ .*//')
if ! [[ "${virtio_gpu_pid}" =~ ^[0-9]+$ ]]; then
  echo "Error: could not find virtio_gpu_pid."; exit 1
fi
crosvm_parent_pid=$(ssh "${host}" pstree -pa | grep "^  |   |-crosvm" | \
    sed -e 's/.*crosvm\,//' -e 's/ .*//')
if ! [[ "${crosvm_parent_pid}" =~ ^[0-9]+$ ]]; then
  echo "Error: could not find crosvm_parent_pid."; exit 1
fi
guest_pid=$(ssh "${guest}" pgrep "${guest_proc_name}")

columns="Time, HostMemTotal, HostMemFree, HostMemAvailable, HostActive, \
CrosvmGpuRss, CrosvmParentRss, GuestProcRss, GpuTotal, GpuPurgeable"
echo "${columns}"

# Setup temporary file path.
tmp_dir=$(mktemp -d)
trap "rm -rf ${tmp_dir}" EXIT
mem_info="${tmp_dir}/mem_info"
gpu_info="${tmp_dir}/gpu_info"

ssh "${host}" cat /sys/kernel/debug/dri/0/i915_gem_objects > "${gpu_info}"
if [[ ! -s "${gpu_info}" ]]; then
  echo "Error: unexpected GPU, please file bug and cc:jbates to add "
  echo "       support for this device."
  exit 1
fi

while true
do
  Time=$(date +%s)
  ssh "${host}" cat /proc/meminfo > "${mem_info}"
  ssh "${host}" cat /sys/kernel/debug/dri/0/i915_gem_objects > "${gpu_info}"
  HostMemTotal=$(grep "MemTotal:" "${mem_info}" | sed 's/[^0-9]*//g')
  HostMemFree=$(grep "MemFree:" "${mem_info}" | sed 's/[^0-9]*//g')
  HostMemAvailable=$(grep "MemAvailable:" "${mem_info}" | sed 's/[^0-9]*//g')
  HostActive=$(grep "Active:" "${mem_info}" | sed 's/[^0-9]*//g')
  CrosvmGpuRss=$(ssh "${host}" ps -o rss= "${virtio_gpu_pid}")
  CrosvmParentRss=$(ssh "${host}" ps -o rss= "${crosvm_parent_pid}")
  GpuTotal=$(sed -n 's/^[0-9]* objects, \([0-9]*\) bytes/\1/p' "${gpu_info}")
  GpuPurgeable=$(sed -n 's/^[0-9]* purgeable objects, \([0-9]*\) bytes/\1/p' \
      "${gpu_info}")
  GpuTotal=$(("${GpuTotal}" / 1024))
  GpuPurgeable=$(("${GpuPurgeable}" / 1024))

  GuestProcRss=0
  last_run=0
  if [[ -z "${guest_pid}" ]]; then
    guest_pid=$(ssh "${guest}" pgrep "${guest_proc_name}")
  else
    # Gather mem info from guest process.
    GuestProcRss=$(ssh "${guest}" ps -o rss= "${guest_pid}")
    # When guest process is gone, print and exit.
    if [[ -z "${GuestProcRss}" ]]; then
      GuestProcRss=0
      last_run=1
    fi
  fi

  # Print results.
  echo "${Time}, ${HostMemTotal}, ${HostMemFree}, ${HostMemAvailable}, \
${HostActive}, ${CrosvmGpuRss}, ${CrosvmParentRss}, ${GuestProcRss}, \
${GpuTotal}, ${GpuPurgeable}"

  if [[ "${last_run}" == 1 ]]; then
    exit 0
  fi

  sleep "${interval_seconds}"
done

