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
