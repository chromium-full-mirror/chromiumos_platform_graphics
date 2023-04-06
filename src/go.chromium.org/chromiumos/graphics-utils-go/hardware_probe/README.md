# ChromeOS graphics hardware_probe utility

This folder contains binaries to retrieve hardware information for graphics team to categories device hardware specs in ChromeOS.

## Overview

In this directory, you will find a binary called hardware_probe, which tries to query the hardware features and return various field for efficiently testing our graphics stacks.

## How to build the tools

### How to build for a specific ChromeOS board

``` bash
# In the cros_sdk chroot:
> emerge-${BOARD} graphics-utils-go
```
The executables are installed in `/usr/local/graphics/` for that board.
Then follow the standard `cros deploy` tool to push the binary to your device.

### How to build for Linux

To build the tool for Linux, tt requires a standard installation of the golang development tools and `make`.

``` bash
# Make sure you are in the same directory as this README.md
> make
```

## How to run the binary

``` bash
localhost ~ # ./hardware_probe --gpu-vendor --cpu-soc-family --gpu-family
GPU_Family: sc7280
GPU_Vendor: qualcomm
CPU_SOC_Family: qualcomm
```

* Note that it may report more than one `GPU_Family` and `GPU_Vendor` if multiple GPUs are detected in the system.


## What fields are generated

Right now, this tools aims to generate the following fields.

- GPU_Family
- GPU_Vendor
- CPU_SOC_Family

## What is GPU_Family

GPU_family is graphics teams' way to categorize the GPU we use in our ChromeOS
system.

| **Platform** |                                **GPU_Vendor**                               |         **Example**        |
|:------------:|:---------------------------------------------------------------------------:|:--------------------------:|
| AMD          | Code name<br /> check cmd/hardware_probe/amd_pci_ids.go for complete list   | carrizo, stoney            |
| Intel        | Code name<br /> check cmd/hardware_probe/intel_pci_ids.go for complete list | alderlake, cometlake, etc. |
| ARM (mali)   | Mali product name                                                           | mali-t860, mali-g72, etc.  |
| ARM (others) | Device name exposed to the compatible layers                                | sc7180, sc7280             |

## What is GPU_Vendor

The following is the current supported vendor name in GPU_Vendor

- amd
- intel
- mediatek
- nvidia
- qualcomm
- rockchip
- virtio
- vmware

## What is CPU_SOC_Family

The following is the current supported name in CPU_SOC_Family

- amd
- intel
- mediatek
- qualcomm
- rockchip
