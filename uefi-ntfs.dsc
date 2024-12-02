## @file
#  UEFI:NTFS Bootloader
#
#  Copyright (c) 2021-2024, Pete Batard <pete@akeo.ie>
#
#  SPDX-License-Identifier: GPL-2.0-or-later
#
##

[Defines]
  PLATFORM_NAME                  = uefi-ntfs
  PLATFORM_GUID                  = 4F8F45AC-93DD-418D-9366-52556EBAC2B4
  PLATFORM_VERSION               = 1.3
  DSC_SPECIFICATION              = 0x00010005
  SUPPORTED_ARCHITECTURES        = IA32|X64|EBC|ARM|AARCH64|RISCV64|LOONGARCH64
  OUTPUT_DIRECTORY               = Build
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  DEFINE FORCE_READONLY          = FALSE

[BuildOptions]
  DEBUG_*_*_CC_FLAGS             = -DENABLE_DEBUG
  RELEASE_*_*_CC_FLAGS           = -DMDEPKG_NDEBUG
  *_*_*_CC_FLAGS                 = -DDISABLE_NEW_DEPRECATED_INTERFACES

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  #
  # Entry Point Libraries
  #
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  #
  # Common Libraries
  #
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

[LibraryClasses.ARM, LibraryClasses.AARCH64, LibraryClasses.RISCV64, LibraryClasses.LOONGARCH64]
  NULL|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

###################################################################################################
#
# Components Section - list of the modules and components that will be processed by compilation
#                      tools and the EDK II tools to generate PE32/PE32+/Coff image files.
#
###################################################################################################

[Components]
  uefi-ntfs.inf
