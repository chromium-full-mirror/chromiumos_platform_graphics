## @file
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
##

[Defines]
 PLATFORM_NAME              = VmPerfEval
 PLATFORM_GUID              = C3226C96-BD4D-40F8-9A81-370281E47CC5
 PLATFORM_VERSION           = 0.1
 DSC_SPECIFICATION          = 0x00010005
 OUTPUT_DIRECTORY           = Build/VmPerfEval
 SUPPORTED_ARCHITECTURES    = X64
 BUILD_TARGETS              = DEBUG|RELEASE|NOOPT
 SKUID_IDENTIFIER           = DEFAULT


[LibraryClasses]
 UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf

 BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
 BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
 UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
 PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
 MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
 UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
 UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
 UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
 DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
 PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
 DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
 DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
 IdentityPtLib|VmPerfEvalPkg/Library/IdentityPtLib/IdentityPtLib.inf
 VmPerfEvalLib|VmPerfEvalPkg/Library/VmPerfEvalLib/VmPerfEvalLib.inf
 LocalApicLib|UefiCpuPkg/Library/BaseXApicLib/BaseXApicLib.inf
 TimerLib|UefiPayloadPkg/Library/AcpiTimerLib/AcpiTimerLib.inf
 IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
 CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
 UefiCpuLib|UefiCpuPkg/Library/BaseUefiCpuLib/BaseUefiCpuLib.inf
 HobLib|UefiPayloadPkg/Library/DxeHobLib/DxeHobLib.inf
 DxeHobListLib|UefiPayloadPkg/Library/DxeHobListLib/DxeHobListLib.inf
 UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf

[LibraryClasses.X64]
 RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf


[Components]
 VmPerfEvalPkg/Application/VmPerfHelloWorld/VmPerfHelloWorld.inf
 VmPerfEvalPkg/Application/VmPerfFullLoad/VmPerfFullLoad.inf
 VmPerfEvalPkg/Application/VmPerfLibTest/VmPerfLibTest.inf
 VmPerfEvalPkg/Application/VmPerfQuantifiedWork/VmPerfQuantifiedWork.inf
 VmPerfEvalPkg/Application/VmPerfApicTimerTest/VmPerfApicTimerTest.inf
