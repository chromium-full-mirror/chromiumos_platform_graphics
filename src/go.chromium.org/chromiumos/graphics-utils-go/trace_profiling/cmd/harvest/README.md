# Tool Harvest
Tool Harvest is designed to harvest information from up to two DUTs connected
to the workstation. It is actually a collection of tools with each tool invoked
using the ```-tool``` cmd-line option: ```harvest -tool <tool-name> [options
for tool-name]```. Available tool names are:

* **profile**: Run companion tool Profile on the target device(s) to generate
  FPS, CPU or GPU profile data.
* **device-info**: Collect machine info from the target devices in the form of
  protobufs that can be uploaded to the trace-result DB.
* **gpuvis**: Collect GPU and CPU performance traces from one target device and
  optionally display the result in GpuVis.

These tools have multiple options that are configured through a JSON configuration
file. Sample config files are available in sub-dir
[./sample-config](./sample-config/README.md).


## Command-line options
Harvest takes several command-line options. They are:

* -**tool**: The tool to run (default is ```profile```).
* -**config**: specify path to configuration JSON file.
* -**compare-fps**: enable FPS comparison step after profiling.
* -**out**: specify output path for the fps-comparison file (default "compare_out.prof")
* -**verbose**: enable verbose mode
* -**help**: print help info.

## Configuration file ##
Harvest is configured through JSON configuration file that has the following structure:

``` json
{
  "TargetDevice1": {
    Profile configuration for 1st target device.
   },
  "TargetDevice2": {
    Profile configuration for 2nd target device.
  },
  "Harvest": {
    Harvest uses the config info in this section for the profile tool.
  },
  "DeviceInfoTool": {
    Harvest uses the information in this section for the device-info tool.
   },
  "GpuVis": {
    Harvest uses the information in this section for the gpuvis tool.
  }
}
```

**Tip**: Harvest ignores config properties that it does not recognize. That is
useful for turning options on and off. For instance, if you want to run Profile
on only the 1st target device, you can temporarily disable the 2nd target device
by renaming it something like ```"disabled_TargetDevice2"```.

## Target devices
Up to two target devices are specified with properties ```"TargetDevice1"``` and
```"TargetDevice2"```. Each of these properties contains the entire Profiler
configuration, including how to reach the device through SSH and how to run
profiles. For a complete description, see the
[README.md file for Profiler](../profile/README.md).

A few things to note:
1. When configuring the SSH parameters, it's often easier to run the Profile tool
  with cmd-line option ```-verbose```. Once you have that working, plug in the
  profiler config inside the TargetDevice section for Harvest.
1. You can use "include" inside each target device, as in<br> ```
  "TargetDevice1": {
    "include": "relative path to profile config for this device"
  }```<br>
  The extra benefit of that approach is that the file that is included is a valid
  config file for tool Profile.
1. Harvest supplies its own trace files to Profile and ignores any trace files
  specified in the target's Profiler config.

## Profiling
Use Harvest's ```profile``` tool option to run traces on target devices and collect
performance data:

``` bash
> harvest -config <path to config file> -tool profile
```

Harvest automates several tasks:

1. It automatically downloads trace data, in the form of game archives or trace
  files, from Google Storage and caches them locally.
2. Extract the trace data from the archive.
3. Run the profile tool with the trace data on the target device(s).
4. Retrieve the profile data from the target device(s) and store it locally.

This is configured through the ```"Harvest"``` section in the config file, which
looks as follows:

``` json
  "Harvest": {
    "traceCacheDir": "local dir path where trace and trace archives are cached",
    "keepTracesInCache": true,
    "profileBinPath": "path to companion tool Profile",
    "delay": 60,
    "traces": [
      list of trace or game archives, in Google Storage or in local dir
    ]
  },
```

Recall that how profiling is carried out on each target devices is configured
through the ```TargetDevice1/2``` configuration.

## Collecting device info
To collect device info from the target devices, use tool option ```device-info```:

``` bash
> harvest -config <path to config file> -tool device-info
```

Again, Harvest automates a number of tasks:
1. Upload companion tool ```get_device_info``` to the target device.
2. Run ```get_device_info``` to collect the data in a form of protobufs.
3. Download the protobuf to the workstation into a designated folder.
4. Optionally upload the protobuf to the trace-result DB with Python tool
  ```bq_insert_pb.by```. (See
  [readme in results_database](../../../results_database/README.md)).

Collecting device info is configured through the ```"DeviceInfoTool"``` section in
the config file. It looks as follows:

``` json
{
  "DeviceInfoTool": {
    "getDeviceInfoBinPath": "<path to>/bin/get_device_info",
    "protoBufsOutputDir": "<path to folder for>/protobufs",
    "uploadScript": "<path to>/results_database/bq_insert_pb.py",
    "owner": "user/gwink", // optional owner
    "machine": {
      "enabled": true,
      "uploadToDb": "yes"
    },
    "software": {
      "enabled": true,
      "skipPackages": false,
      "alsoRunOnParent": true,
      "uploadToDb": "yes"
    }
  },
}
```

**Important notes:**
1. Harvest uses the machine name in the ```"TargetDevice1/2"``` config section
  to form file names for the protobufs. For example
  ```machine_info_gwink-helios-C278883.json```.
2. When ```"uploadToDb"``` is enabled, Harvest compares the new protobuf data
  with previous protobufs found in dir ```protoBufsOutputDir```. It uploads the
  new protobufs only if there are meaningful differences. (E.g. it ignores the
  creation time.) Obviously, that only works if the machine name and dir
  ```protoBufsOutputDir``` folder for protobufs are kept consistent between runs.
3. When collecting software-config data from an embedded target, such as Steam VM
  or Crostini, Harvest will also collect the software config from the Chrome-OS
  device and link the two through the Parent-ID. Harvest uses the SSH configuration
  as an indication it needs to do so. I.e. if a tunnel is setup, it knows to reach
  the target through the tunnel and Chrome OS through the tunnel's server.


## Collecting GpuVis data
Tool option ```gpuvis``` is intended to make collecting low-level trace data and
visualizing it with [GpuVis](https://github.com/mikesart/gpuvis) easier. It is
invoked as follows:

``` bash
> harvest -config <path to config file> -tool gpuvis
```

Tool gpuvis requires having a local build of GpuVis, including all its data-collection
scripts. It is configured with the ```"GpuVis"``` section in the configuration
file:

``` json
{
  "GpuVis": {
    // Path to working dir on chrome-OS device.
    "chromeOSWorkingDir": "gpuperf",
    // Env var to use on chrome-OS device when gathering trace data.
    "chromeOSEnvVar": [
      "USE_I915_PERF=1",
      "I915_PERF_METRIC=RenderPipeProfile"
    ],
    // GpuVis dir on local machine. GpuVis binaries and scripts should be available
    // from this dir. Trace data will also be stored here.
    "localGpuVisDir": "/home/gwink/Gaming/gpuvis",
    // What frame within trace to loop on and how many time.
    "frameLoop": "3500-3509,300",
    // How to invoke gpuvis, relative to localGpuVisDir above. Set to empty string
    // if you do not want to run gpuvis on collected trace data.
    "runGpuVis": "./gpuvis"
  }
}
```

**For Googlers**<br>
Googlers will find more info about setting up a Chrome-OS device for gathering
and visualizing GpuVis in the [GpuVis How-to](http://go/cros-gpuvis-howto)
document.
