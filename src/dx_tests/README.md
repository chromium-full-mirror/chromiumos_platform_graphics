# dx_tests

This directory houses a DX unit test suite.

## Minimum build Requirements
- Windows 10
- Visual Studio 2017
- CMake 3.23.2
- Windows 10 SDK version 2104

## Build Instructions
1. Update the git submodules.
2. Generate the project with CMake.
3. Disable BUILD_GMOCK and INSTALL_GTEST. This project has googletest embedded, no need to install.
4. Generate the project.
5. Launch Visual Studio and build.

## Regenerate Prebuilt Shaders
This is necessary whenever the DXVK shader hashing library is updated.

1. Copy fxc.exe from the Windows SDK (C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64) to the directory where DxvkUnitTestApp.exe is output.
2. Run DxvkUnitTestApp.exe with the `-regenerate-prebuilt-shader-binaries` command line option.
