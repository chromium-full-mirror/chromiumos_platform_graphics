# Trace Profiling Tools

## Overview
In this directory, you will find a collection of tools for collecting and analyzing
performance and device information from one or two Chrome-OS devices connected to a
workstation. By "connected" we mean that the Chrome-OS devices are reachable from
the workstation through a SSH connection.

The following tools are provided (click on the tool name to go to the readme file
for that tool):
* **[Profile](cmd/profile/README.md)**: Profile connects to a single target device
  over SSH and automatically runs traces on that device to collect performance and
  profile data that it then copies back to the workstation. Although Profile can
  be used on its own, it is also, and perhaps more commonly used through Harvest.
* [**Harvest**](cmd/harvest/README.md): Harvest is a convenience tool that connects
  to one or two target Chrome-OS devices and gather device and performance information.
  The device information consists of machine-data or software-/os-configuration
  protobufs that are suitable for uploading to the trace-result database. The
  performance information consists of ApiTrace profiles that can be analyzed with
  companion tool Analyze or ftrace CPU/GPU data that can be viewed with GpuVis.
* **[Analyze](cmd/analyze/README.md)**: Analyze is a command-line tool used for
  analyzing detailed CPU/GPU ApiTrace profiles. It is able to work with one or
  two profiles at a time. When two profiles are loaded, it provides tools for
  comparing the them.
* **[Merge](cmd/merge/README.md)**: Merge is specifically designed to merge GPU
  and CPU trace-based profiles geerated by Profile or Harvest into a single file
  that can be loaded into Analyze.
* **[get_device_info](cmd/gen_db_result/README.md)**: Get_device_info runs locally,
  either on the workstation or on a target device. On the workstation, it is used
  to generate trace-result protobufs from profile data. On a target device, it is
  used to generate machine-info or software-config protobufs. It is rarely used
  directly in that latter role, and is instead invoked by Harvest. All the protobufs
  that it generates are suitable for uploading to the trace-result DB.

## How to build the tools

There are two ways to build the cmd-line tools. The first is with emerge:

``` bash
# In the cros_sdk chroot:
> emerge --board=<BOARD> graphics-utils-go
```
The executables are installed in `/usr/local/graphics/` for that board.

The second way to build the tools is provided for developer convenience and uses
make. It requires a standard installation of the golang development tools.
Furthermore, each tool is built individually. For example:
``` bash
> cd .../chromiumos/src/platform/graphics/src/trace_profiling/cmd/harvest
> make
# Builds harvest and drops the executable in .../chromiumos/src/platform/graphics/src/trace_profiling/bin
```

## Built-in help

All the cmd-line tools have built-in help, invoked with the `-help` options.
For example:

``` text
-> harvest -help

Usage: harvest -config config-file.json [other options]
Available options:
  -compare-fps
    	Extract FPS from profile data and compare
  -config string
    	Filename for JSON config data
  -out string
    	Output file (default "compare_out.prof")
  -tool string
    	Tool to run, one of profile, device-info or gpuvis (default "profile")
  -verbose
    	Enable verbose mode
```
