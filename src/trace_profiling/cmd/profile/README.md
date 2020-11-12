# Profile Tool
**Important note**: Although tool Profile can be used on its own, it is more
convenient to use tool Harvest instead. Harvest can connect to up to two devices
at once and is able to gather various information from these devices, including
profiles.

Tool Profile is used to gather trace-based profiles from target devices.
The entire profiling activity is initiated from a workstation which runs the
Profile tool. Profile connects to the target device through SSH.

Run `profile -help` to print information about the available cmd-line
options.

For each trace file provided, Profile will:
1. Upload the trace file to the target device.
1. Run the tracing application on the target device to collect performance
   data.
1. Copy the performance-data file back to the workstation into a designated
   folder.

These steps and the SSH parameters needed to reach the target device are
configured via JSON configuration file that looks as follows.

``` json
profile_config.json:
{
  "Device": {
    Information about the device that consists ofna unique name and the
    execution environment.
  },
  "Profile": {
    "tunnelConfig": {
      The tunneling parameters are required when the target device is not
      reachable from the workstation with a direct SSH connection. That is
      most often the case with devices such as crostini, steam VM or crouton.
      When provided, these parameters are used to establish a SSH tunnel from
      the workstation to the target device.
    },
    "sshConfig": {
      These are the SSH parameters needed to reach the target device, either
      through a tunnel established with the tunneling parameters provided above,
      or directly.
    },
    "profilerConfig": {
      These are the parameters needed to run traces on the target devices, to
      collect performance data, once the SSH connection has been established.
    }
  }
}
```

## About SSH tunneling
It is usually not possible to SSH into VM or chroot devices, such as Crostini or
Crouton, directly from your workstation. Instead, you need to setup a SSH tunnel.
You can find a reasonable explanation about SSH tunneling here:
https://en.wikipedia.org/wiki/Tunneling_protocol.

In our scenario, the SSH client is your workstation, the SSH server is the
Chrome-OS device and the localhost is the target device.

Before you can configure SSH and tunneling, you need to have the following ready:
1. The SSH parameters to reach Chrome-OS device, including IP address, port, user
   name and authentication credentials.
1. Ensure that the target device is running a SSH server and you have the address
   and port to reach that server from the Chrome-OS shell. For instance, Crostini
   has a SSH server built-in, but it needs to be explicitly enabled. Once it is
   enabled, the Crostini SSH server is reachable at address and port
   ```penguin.linux.test:22```.
1. An unused port on your workstation. We'll use that port to tunnel to the
   target device.

Once you have that information, you are ready to configure the device connection.

### 1. Configure Chrome-OS SSH

Let's assume that you SSH from your workstation to your Chrome-OS device as follows:

``` bash
ssh -i "/home/gwink/.ssh/testing_rsa" root@192.168.1.29:22
```

Furthermore, let's assume that SSH asks for a password, and that the password is
``` test0000 ```.

Configure the server as follows:

``` json
profile_config.json:
{
  "Profile": {
    "tunnelConfig": {
      "server": {
        "addr": "192.168.1.29",
        "port": 22,
        "username": "root",
        "password": "test0000",
        "publicKeyFilepath": "/home/gwink/.ssh/testing_rsa"
      }
    },
  }
}
```

**Note**: In this example, we'll always use both a password and a RSA key for
for authentication. You should only need one or the other, depending on how the
server is configured and whether you have a public key setup on the
remote device.

### 2. Configure the SSH tunnel
Going back to the Crostini target example, from a shell on the Chrome-OS device,
we can ssh into Crostini at address and port ``` penguin.linux.test:22 ```. Let's
use port ``` 9933 ``` on our workstation for tunneling.

You can test the tunneling parameters with the following shell command on
your workstation:

``` bash
ssh -i /home/gwink/.ssh/testing_rsa -N -L 9933:penguin.linux.test:22 root@182.168.1.29:22
```

If tunneling is successful, a tunnel is established and the command will not return
until you hit ctrl-C.

You are then ready to add the tunneling parameters to the json file:

``` json
{
  "Profile": {
    "tunnelConfig": {
      "localPort": 9933,
      "targetHostAddr": "penguin.linux.test",
      "targetPort": 22,
      "server": {
        [...]
      }
    },
  }
}
```

### 3. Configure SSH from workstation to target device
Finally, we're ready to configure SSH from the workstation to the target device.
Since we tunnel through local port ```9933```, the address and port are
```localhost:9933```. For this example, the account on the target device -- the one
we are tunelling to -- has login name ```gwink```.

SSH is configured as follows:

``` json
{
  "Profile": {
    "tunnelConfig": {
      [...]
      "server": {
        [...]
      }
    },
    "sshConfig": {
      "addr": "localhost",
      "port": 9933,
      "username": "gwink",
      "password": "test0000",
      "publicKeyFilepath": "/home/gwink/.ssh/testing_rsa"
    },
```

## Config property Device
The ```Device``` property contains information that is used to identify the device.
It consists of:

* **execEnv**: A string that identifies the execution environment for the device.
  One of ``` "host", "termina", "crostini", "steam", "arc", "arcvm",
  "crouton",``` or ``` "crosvm"```.
* **name**: A name that uniquely identifies the device globally. You can construct
  this name any way you prefer. A good way to generate unique name that are easy to
  interpret is as follows: ``` "name": "<ldap>-<device code name>-<asset tag>"```

Example:
``` json
{
  "Device": {
    "execEnv": "crostini",
    "name": "gwink-sona-C097462"
  },
  [...]
}
```

## Profile configuration
Finally, tool Profile is configured as follows:

``` json
{
  "Profile": {
    "profilerConfig": {
      "localTraceDir": "/home/gwink/Gaming/profiles/cache/",
      "targetTraceDir": "/home/gwink/traces/",
      "traces": [
        "game_1.trace",
        "game_2.trace"
      ],
      "keepTraceOnTarget": false,
      "localProfileDir": "/home/gwink/Gaming/profiles/fps/prof",
      "profileNameSuffix": ".crouton.prof",
      "localProfAppPath": "/home/gwink/Gaming/apitrace/usr/bin/",
      "targetProfAppPath": "/home/gwink/apitrace/",
      "profCommand": "/home/gwink/apitrace/glretrace -b [[trace-file]] > [[prof-file]]",
      "targetDisplay": "1"
    }
  }
}
```

The parameters are:
- **localTraceDir**: path to dir where trace files are stored on workstation.
- **targetTraceDir**: path to dir where to store trace files on target device.
- **traces**: list of trace files available from ```localTraceDir```.
- **keepTraceOnTarget**: When true the trace files are kept on the target device.
  That can save considerable time, as these traces will not be copied over when
  used again. But beware that trace files are large.
- **profileNameSuffix**: suffix added to each profile data file downloaded from
  the target device.
- **profCommand**: This is the command that is used to run each trace. It uses two
  template placeholders, ```[[trace-file]]``` which is replaced with the actual
  trace file path and ```[[prof-file]]``` which is replaced with the output
  file name.
- **targetDisplay**: ```DISPLAY`` to use when running traces.

There are two more parameters ```localProfAppPath``` and ```targetProfAppPath```.
When provided, Profile will copy all the exe files found in ```localProfAppPath```
on the workstation to ```targetProfAppPath``` on the target device. These parameters
are somewhat obsolete. You're most likely better off using the built-in ApiTrace
tools or building a local version of ApiTrace on the target device.

## Full Profile config file sample ###
We can finally put the entire config file for Profile together:

``` json
{
  "Device": {
    "execEnv": "crostini",
    "name": "gwink-sona-C097462"
  },
  "Profile": {
    "tunnelConfig": {
      "localPort": 9933,
      "targetHostAddr": "penguin.linux.test",
      "targetPort": 22,
      "server": {
        "addr": "192.168.1.29",
        "port": 22,
        "username": "root",
        "password": "test0000",
        "publicKeyFilepath": "/home/gwink/.ssh/testing_rsa"
      },
    },
    "sshConfig": {
      "addr": "localhost",
      "port": 9933,
      "username": "gwink",
      "password": "test0000",
      "publicKeyFilepath": "/home/gwink/.ssh/testing_rsa"
    },
    "profilerConfig": {
      "localTraceDir": "/home/gwink/Gaming/profiles/cache/",
      "targetTraceDir": "/home/gwink/traces/",
      "traces": [
        "game_1.trace",
        "game_2.trace"
      ],
      "keepTraceOnTarget": false,
      "localProfileDir": "/home/gwink/Gaming/profiles/fps/prof",
      "profileNameSuffix": ".crouton.prof",
      "localProfAppPath": "/home/gwink/Gaming/apitrace/usr/bin/",
      "targetProfAppPath": "/home/gwink/apitrace/",
      "profCommand": "/home/gwink/apitrace/glretrace -b [[trace-file]] > [[prof-file]]",
      "targetDisplay": "1"
    }
  }
}
```
