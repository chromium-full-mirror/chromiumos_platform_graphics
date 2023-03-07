# dx_tests

This directory houses a DX unit test suite.

## Minimum build Requirements
- Windows 10
- Visual Studio 2017
- CMake 3.23.2
- Windows 10 SDK version 2104

## Build Instructions
1. Update the git submodules
2. Launch CMake and navigate to the project directory. Set the following environment variables:
- `WindowsSdkDir=<SDK install path>`. For example, `C:/Program Files (x86)/Windows Kits/10/`
- `WindowsSDKLibVersion=\<SDK install version>\`. For example, `\10.0.20348.0\`
3. Generate the project.
4. Disable BUILD_GMOCK and INSTALL_GTEST. This project has googletest embedded, no need to install.
5. Generate the project.
6. Launch Visual Studio and build.
