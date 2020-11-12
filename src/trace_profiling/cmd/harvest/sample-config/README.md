# Configuration Files
JSON configuration files are used by tools Profile and Harvest. They specify how to
connect to the DUTs, options to run the various tools, where to find and store
files, etc. Configuration details relevant to each tool are described in the readme
files for Profile and Harvest. In this document, we'll just go over generic
information about configuration files.

**About the sample files**: There are several sample configuration files in this
directory. File *sample-config-mono.json* contains all the configuration parameters
in a single file together with comments to explain the purpose of the various properties.
The other files contain the same configuration parameters but spread into several,
shareable files. The root file is *sample-config-part0.json*.

**About comments in the JSON code:** If you open file *sample-config-mono.json*,
you'll see a lot of c-style comments. There's limited support for such comments in
the json parsing code. Comments on their own lines and at the end of the line should
work. Block comments on their own lines should work too. Keep in mind that it is
largely a hack, introduced primarily to document these configuration files.

## Structure of configuration files
Harvest configuration files have the following structure:

``` json
General structure of a Harvest config file:
{
  "TargetDevice1": {
    Information about the first target device, including:
    - The target device globally unique name and type (crostini, crouton, etc.).
    - Optional SSH tunnel parameters to reach the target by tunelling through the
      host OS.
    - SSH parameters to connect to the target.
    - Parameters used to run traces on the target device to gather profile data.
  },

  "TargetDevice2": {
    Similar information for the second target device. This is optional.
  },

  "Harvest": {
    Parameters used by the Harvest tool to run traces on the target devices and
    gather profile data.
  },

  "DeviceInfoTool": {
    The device-info-tool is part of Harvest and activated with cmd-line option
    '-tool device-info'. It is used to gather machine-info and software-configuration
    information from the target device and optionally upload that information to the
    trace-result database.
    "machine": {
      Whether and how to gather machine-information from the target device.
    },
    "software": {
      Whether and how to gather software-configuration information from the target
      device.
    }
  },

  "GpuVis": {
    The gpuvis tool is also part of Harvest and is activated with cmd-line option
    '-tool gpuvis'. It is designed to gather low-level CPU and GPU trace data on
    the host OS while running a trace in the target environment, such as crostini.
  }
}

```

Details about the configuration information in the *TargetDevice1/2* properties are
found in the [readme file for tool Profile](../../profile/README.md). Details
about the other properties are found in the
[readme file for tool Harvest](../README.md).

## Using includes
Includes provide a nice way to divide configuration files into several re-usable
files. Here's what you need to know about includes.

**Includes can appear in only two places**, either at the very top of a file, after
the first bracket, or within ```TargetDevice1/2``` properties.

**Includes take two forms**,
- Include a single file: `"include": "path to single file to include"`.
- Include several files: `"include": ["file 1", "file2"...]`.

**Included file paths** are relative to the dir of the including file.

**Included files must have the same structure as the including file**. I.e. the
files are merged structurally. When property names collide during merging, properties
in the including file take precedence over properties in the included file.

Example:

``` json
main.json
{
  "include": ["target1.json", "target2.json"],
  "Harvest": {
    "keepTracesInCache": true
  }
}
```
``` json
target1.json:
{
  "TargetDevice1": {
    "Device": {
      "execEnv": "crostini"
    }
  }
}
```
``` json
target2.json:
{
  "TargetDevice2": {
    "Device": {
      "execEnv": "crouton"
    }
  }
}
```

Result in the following configuration:
``` json
{
  "TargetDevice1": {
    "Device": {
      "execEnv": "crostini"
    }
  },
  "TargetDevice2": {
    "Device": {
      "execEnv": "crouton"
    }
  },
  "Harvest": {
    "keepTracesInCache": true
  }
}
```
