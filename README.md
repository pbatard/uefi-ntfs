UEFI:NTFS - Boot NTFS partitions from EFI
=========================================

This generic bootloader, which is primarily intended for use with 
[Rufus](http://rufus.akeo.ie), is meant to allow seamless booting from an EFI
bootloader, that resides on an NTFS partitions. In other words, UEFI:NTFS is
designed to remove the UEFI restriction of being able to natively boot from
FAT32 only, and allow NTFS boot without the need for any user intervention.

This can be used, for instance, for booting an USB Windows NTFS installation
media, in EFI mode, allowing support for files that are larger than 4GB
(something a native EFI FAT32 partition cannot support), or allow
indiscriminate EFI or BIOS boot of Windows To Go drives.

The way this works, in conjuction with Rufus, is as follows:

* Rufus creates 2 partitions on the target USB disk (these can be MBR or GPT
  partitions). The first one is an NTFS partition occupying almost all the
  drive, that contains the Windows files (for Windows To Go, or for regular
  installation), and the second is a very small FAT partition, located at the
  very end, that contains an NTFS EFI driver (see http://efi.akeo.ie) as well
  as the UEFI:NTFS bootloader.
* When the USB drive boots in EFI mode, the first NTFS partition gets ignored
  by the EFI firmware and the UEFI:NTFS bootloader from the bootable FAT partition
  is executed.
* UEFI:NTFS then loads the relevant NTFS EFI driver, locates the existing NTFS
  partition on the same media, and executes the `/efi/boot/bootx64.efi` or 
  `/efi/boot/bootia32.efi` that resides there. This achieves the exact same
  outcome as if the EFI firmware had native NTFS support and could boot 
  straight from NTFS.

## Prerequisites

* [Visual Studio 2013](http://www.visualstudio.com/products/visual-studio-community-vs)
  or [MinGW-w64](http://mingw-w64.sourceforge.net/) (with msys, if using MinGW-w64 on Windows)
* [QEMU](http://www.qemu.org)
* git
* wget, unzip, if not using Visual Studio

## Sub-Module initialization

For convenience, the project relies on the gnu-efi library (but __not__ on
the gnu-efi compiler itself), so you need to initialize the git submodules:
```
git submodule init
git submodule update
```

## Compilation and testing

If using Visual Studio, just press `F5` to have the application compiled and
launched in the QEMU emulator.

If using MinGW-w64, issue: `make qemu`