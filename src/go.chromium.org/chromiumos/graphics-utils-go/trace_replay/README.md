# ChromeOS graphics trace_replay utilities

This folder contains binaries to replay graphics traces in ChromeOS.

## Overview

In this directory, you will find a binary called trace_replay which instructs
the machine on how to run the provided trace.

## How to build the tools

There are two ways to build the cmd-line tools. The first is with emerge:

``` bash
# In the cros_sdk chroot:
> emerge --board=<BOARD> graphics-utils-go
```
The executables are installed in `/usr/local/graphics/` for that board.

The second way to build the tools is provided for developer convenience and uses
make. It requires a standard installation of the golang development tools.
``` bash
# Make sure you are in the same directory as this README.md
> make
```

## How to run it

To run it, write your own config json file, which compatible to comm.TestGroupConfig
structure, then pass the config json file as an argument.

See cmd/trace_replay/comm/comm.go for more information.

``` bash
> ./bin/trace_replay $CONFIG_JSON
```
