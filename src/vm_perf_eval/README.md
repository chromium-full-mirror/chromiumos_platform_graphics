VM Performance Evaluation Package
=================================

This directory contains a set of UEFI applications that can be
utilized to evaluate VM performance. In theory, the guest kernel
can be removed from the equation and the host can be anaylzed and
tweaked with the help of these tools.

A TECHNICAL.md will be authored to document the lower level details.

# Intro
These applications are built outside the CrOS source tree. The UEFI
applications will be transferred to either a USB stick or a RWDISK image for
booting on a baremetal machine or under a VM respectively (preferrably CrosVM,
 but QEMU has also been able to run these).

# Requirements
See the EDKII guide on their GitHub for up to date requirements and
ensure these are installed on your machine.

In genenral, you will need the following packages:
1. `nasm`
2. `gcc` and `g++`
3. `make`
4. `python3`
5. `python-is-python3` (for Ubuntu)
6. `uuid-dev`
7. `iasl`

# Build Environment Setup
Run the script (outside of a chroot):
* `./setup_env.sh`

and once those steps are complete:
1. `cd edk2`
2. `source edksetup.sh`

The script will perform the following steps:

1. Retrieve the EDKII package from GitHub (including referenced submodules)
2. Perform early build steps of tools necessary to support EDKII's build system
3. Create symbolic link from the package directory into the EDKII directory

The second part will setup the environment needed to perform the build.

# To Build
To build the UEFI application(s):

1. `cd edk2`
2. `build -p VmPerfEvalPkg/VmPerfEvalPkg.dsc -t GCC5 -a X64 -b <DEBUG|RELEASE>`

This will start the build of all the UEFI applications and place
the binaries somewhere under the Build directory. In general
they will be under:

 `Build/VmPerfEval/<DEBUG|RELEASE>_GCC5/X64/<ApplicationName>.efi`

With the exact directory depending on whether a debug or release build
was triggered.

# Image Setup
The path to setup an image with your application will depend on whether you are running
on a baremetal machine or a VM. Regardless of what method you choose, the filesystem
MUST be FAT32/FAT16/FAT12. *NO OTHER FILESYSTEM* will work. FAT12/FAT16 are not particularly
useful with the filesizes we are dealing with, so this realistically limits you to FAT32.

## Bare Metal
You will need to ensure that your machine is EFI compliant and is able to boot
off a USB device (flash or hard drive). You can also dedicate a partition (64 -> 128GB) on your hard drive for this.
Most PCs released since 2012/2013 are UEFI compliant.

1. Rename the application you want to run to "BOOTX64.EFI".
2. Place it on your device such that it appears as follows:
    `EFI/BOOT/BOOTX64.efi`

## Virtual Machine
In this case you will need to create a RWDISK image. A raw disk image can be created:
1. `sudo dd if=/dev/zero of=imageName.img bs=1G count=NumberOfGigs`
2. `mkfs.fat imageName.img`

Once created, one can mount it as follows:

* `sudo mount -o loop imageName.img (mountDirectory)`

And then follow the procedure in bare metal to place the EFI binary in the appropriate place
within the image

* `mkdir -p (mountDirectory)/EFI/BOOT`
* `cp (BOOTX64.efi) (mountDirectory)/EFI/BOOT/BOOTX64.efi`

Then unmount the disk and it should be ready to go.

# To Run

## Bare Metal
Setup your machine in the system setup menu to boot your USB device or partition.
The application should run on the next boot.

## Virtual Machine
For a virtual machine, you will need a package known as the OVMF (Open Virtual Machine Firmware).

This firmware offers an EFI implementation to virtual machines. This is necessary to load
and start the EFI application.

### QEMU
QEMU and the OVMF package will need to be installed on your machine.
Under Ubuntu and derivatives it will be:
1. qemu-system-x86
2. ovmf

Then launch the application as follows:

`qemu-system-x86_64 --bios /usr/share/OVMF/OVMF_CODE.fd imageName.img`

### CrosVM
The OVMF build for CrosVM does not seem to work with serial output. Any output from the application will need to be extracted from the image after the application has been run.

Retrieve the BIOS from here:
[BIOS](https://b.corp.google.com/issues/238360580#comment4)

This can be run with something along the lines of (with 'bios' being the file from above):

`crosvm run --cpus <NumOfCPUs> --bios bios --rwdisk imageName.img`

Currently, you will need to forcefully close CrosVM after the test has run (as there is no output).
At some point it may be possible to trigger a shutdown from the EFI application and (hopefully) this will automatically close CrosVM.

Although there is no output, files written by the application will be available within the rwdisk image after completion.
One must ensure that the file buffers within the application are flushed after every write, especially in the case of logging.

# Bootstrapper
The VmPerfEval library contains an assembly bootstrapper in the ApBoot sub-directory. This bootstrapper is responsible for initializing other CPU's on the system and directing them to an entry point of your choice. It is embedded into the library as a raw series of bytes.

Part of the initialization involves copying this binary into the lower 1MiB of the physical address space so it can be accessed by the target CPU in all operating modes (along with some of the other core data structures).

The files in this directory are:
* `ApBoot.h`: Defines the interface between the C code and the bootstrapper (auto generated)
* `ApBoot.inc`: Defines the above interface from the assembly level
* `ApBootCode.h`: The bootstrapper binary converted to a series of bytes

For this reason it is a built a little differently than the rest of the project.

## Bootstrapper Build
Below are instructions to build the bootstrapper if one wants to do so:

### Bootstapper Package Requirements
The following packages are required in addition to the ones mentioned in earlier sections:

1. `a56`

The a56 package contains the bin2h tool that is used to convert the binary to an array.

### Bootstrapper Build Steps

### Assemble:
`nasm ApBoot.nasm`

This should produce a file by the name of `ApBoot`. This is the resulting binary file.

### Convert to C array
`bin2h 8 < ApBoot > ApBootCode.h`

This will convert this binary into a sequence of bytes suitable for embedding in the library.
Be sure to put the appropriate header in place when you wish to submit changes to this file.

### INC file conversion
If the interface between the C code and the binary has changed. The ApBoot.h header file MUST be regenerated.
This can be accomplished with...

`<ToolsDirectory>/NasmIncToH.py --incfile ApBoot.inc`

Once all these steps have been completed, simply perform a rebuild as normal. The next build will pick up the updated files.
