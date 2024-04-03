# dx_tests

This directory houses a DX unit test suite.

GOOGLERS: Execution and builds on corp Windows devices may be blocked by corp security due to unrecognized binaries. It is advisable to request a temporary exception since every update to the source will change the binary signature of the test apps, which may be blocked again.

## Minimum build Requirements
- Windows 10
- Visual Studio 2017
- CMake 3.23.2
- Windows 10 SDK version 2104

## Build Instructions
1. Update the git submodules (`git submodule update --init --recursive`).
2. Generate the project with CMake. Choose the generator that matches your Visual Studio version. If your Visual Studio has updated since the last time you configured the project, you will need to clear the CMake cache (e.g. "File > Delete Cache" in the CMake GUI) before proceeding.
3. Disable BUILD_GMOCK and INSTALL_GTEST. This project has googletest embedded, no need to install.
4. Generate the project.
5. Launch Visual Studio and build.

## Regenerate Prebuilt Shaders
This is necessary whenever the DXVK shader hashing library is updated.

NOTE: DX12 unit tests do not support prebuilt shaders.

1. Copy fxc.exe from the Windows SDK (C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64) to the directory where DxvkUnitTestApp.exe is output.
2. Run DxvkUnitTestApp.exe with the `-regenerate-prebuilt-shader-binaries` command line option.

## Packaging dx_tests for use in tests

Bundle the following into a directory named dx_tests:
- Release build of DX12UnitTestApp.exe
- Release build of DxvkUnitTestApp.exe
- `assets` directory from the this source code
- `golden_images` directory from this source code

## Testing the DX test apps in Windows

The executable binary should be in the same directory as the `golden_images` directory or the path to the images should be provided with the `-image-dir=/path/to/images` command-line argument.

GOOGLERS: Virtual Windows machines use the reference DirectX software implementation, which should always be accurate.
