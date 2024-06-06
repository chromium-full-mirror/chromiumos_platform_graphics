# ChromeOS graphics hardware_probe utility

This folder contains binaries to retrieve information for graphics team to categories device hardware specs in ChromeOS.

## Overview

In this directory, you can build a binary called `hardware_probe`, which tries to query features and return various field for efficiently testing our graphics stacks.

## How to build the tools

### How to build for a specific ChromeOS board

``` bash
# In the cros_sdk chroot:
$ emerge-${BOARD} graphics-utils-go
```
The executables are installed in `/usr/local/graphics/` for that board.
Then follow the standard `cros deploy` tool to push the binary to your device.

### How to build for Linux

To build the tool for Linux, it requires a standard installation of the golang development tools and `make`.

``` bash
# Make sure you are in the same directory as this README.
$ make
```

## How to run the binary

``` bash
# Run on a DUT after cros deploy:
$ /usr/local/graphics/hardware_probe

# Or run locally after running `make`:
$ ./bin/hardware_probe
```

Example output:
``` json
$ ./hardware_probe
{
    "CPU_SOC_Family": "intel",
    "Disk": {
        "name": "nvme0n1",
        "size": 256060514304,
        "size_gb": 256
    },
    "GPU_Family": [
        {
            "Family": "raptorlake",
            "GPUVendor": "intel",
            "ID": "intel:a7a1"
        }
    ],
    "Memory": 8,
    "VGA_Devices": [
        {
            "BDF": "0000:00:02.0",
            "Class": "VGA compatible controller",
            "Name": "Intel Corporation Raptor Lake-P [Iris Xe Graphics] (rev 04)",
            "VendorID": "0x8086",
            "DeviceID": "0xa7a1",
            "BootVGA": true,
            "GPUInfo": {
                "Family": "raptorlake",
                "GPUVendor": "intel",
                "ID": "intel:a7a1"
            }
        }
    ],
"ConnectedDisplays": [
        {
            "Connector": {
                "ConnectorID": 236,
                "EncoderID": 0,
                "Connected": true,
                "Name": "eDP-1",
                "Width": 300,
                "Height": 190,
                "CountModes": 2,
                "Encoders": [
                    235
                ],
                "Modes": [
                    {
                        "Index": 0,
                        "Name": "1920x1200",
                        "Refresh": 60.03,
                        "HDisplay": 1920,
                        "HSyncStart": 1936,
                        "HSyncEnd": 1952,
                        "HTotal": 2104,
                        "VDisplay": 1200,
                        "VSyncStart": 1203,
                        "VSyncEnd": 1217,
                        "VTotal": 1236,
                        "Preferred": true
                    },
                    {
                        "Index": 1,
                        "Name": "1920x1200",
                        "Refresh": 48.02,
                        "HDisplay": 1920,
                        "HSyncStart": 1936,
                        "HSyncEnd": 1952,
                        "HTotal": 2104,
                        "VDisplay": 1200,
                        "VSyncStart": 1203,
                        "VSyncEnd": 1217,
                        "VTotal": 1236,
                        "Preferred": false
                    }
                ],
                "VrrCapable": true,
                "Edid": {
                    "ManufacturerName": "AUO",
                    "ModelNumber": 29344,
                    "VsyncRateMin": 48,
                    "VsyncRateMax": 60,
                    "HDRBlock": false,
                    "Base64Bytes": "AP///////wAGr6ByAAAAAB4fAQSlHhN4AwAlqFVJnyUOUFQAAAABAQEBAQEBAQEBAQEBAQEB+jyAuHCwJEAQED4ALbwQAAAYyDCAuHCwJEAQED4ALbwQAAAYAAAA/QAwPEtLEAEKICAgICAgAAAA/gBCMTQwVUFOMDIuMiAKALgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="
                }
            },
            "Encoders": [
                {
                    "Encoder": {
                        "EncoderID": 235,
                        "CrtcID": 0,
                        "EncoderType": "TMDS",
                        "PossibleCrtcs": 15,
                        "PossibleClones": 1
                    }
                }
            ]
        }
    ],
    "OpenGLES": "3.2",
    "OpenGLESPackage": {
        "Name": "media-libs/mesa-iris",
        "Version": "24.0.2",
        "Revision": "152"
    },
    "VulkanAPIVersion": "1.3.274",
    "VulkanPackage": {
        "Name": "media-libs/mesa-iris",
        "Version": "24.0.2",
        "Revision": "152"
    },
    "ClvkPackage": {
        "Name": "media-libs/clvk",
        "Version": "0.0.1",
        "Revision": "82"
    },
    "LabelsReporting": {
        "display_panel_name": "AUO 29344",
        "display_present_hdr": "hdr unsupported",
        "display_present_psr": "psr supported",
        "display_present_vrr": "vrr supported",
        "display_refresh_rate": "60.03",
        "display_resolution": "1920x1200",
        "gpu_family": "raptorlake",
        "gpu_id": "intel:a7a1",
        "gpu_open_gles_version": "3.2",
        "gpu_vendor": "intel",
        "gpu_vulkan_version": "1.3.274",
        "platform_cpu_vendor": "intel",
        "platform_disk_size": "256",
        "platform_memory_size": "8"
    }
}


# Only print software related properties
$ ./hardware_probe --software
{
    "OpenGLES": "3.2",
    "OpenGLESPackage": {
        "Name": "media-libs/mesa-iris",
        "Version": "24.0.2",
        "Revision": "152"
    },
    "VulkanAPIVersion": "1.3.274",
    "VulkanPackage": {
        "Name": "media-libs/mesa-iris",
        "Version": "24.0.2",
        "Revision": "152"
    },
    "ClvkPackage": {
        "Name": "media-libs/clvk",
        "Version": "0.0.1",
        "Revision": "82"
    }
}

# Only print the gathered field for infra
$ ./hardware_probe --labels-reporting
{
    "display_panel_name": "AUO 29344",
    "display_present_hdr": "hdr unsupported",
    "display_present_psr": "psr supported",
    "display_present_vrr": "vrr supported",
    "display_refresh_rate": "60.03",
    "display_resolution": "1920x1200",
    "gpu_family": "raptorlake",
    "gpu_id": "intel:a7a1",
    "gpu_open_gles_version": "3.2",
    "gpu_vendor": "intel",
    "gpu_vulkan_version": "1.3.274",
    "platform_cpu_vendor": "intel",
    "platform_disk_size": "256",
    "platform_memory_size": "8"
}
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

## How to update the PCIID mapping

Right now, we have four PCIID map in hardware_probe

- amd_pci_ids.go
- intel_pci_ids.go
- nvidia_pci_ids.go
- vm_pci_ids.go

Most of them have comments in their headers indicating how to properly update
the mappings from either the mesa/kernel repository.
