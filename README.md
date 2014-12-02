uefi-togo - UEFI bootloader for dual MBR/UEFI Windows To Go
===========================================================

This bootloader is used with Rufus, to allow dual MBR+UEFI booting of
Windows To Go USB drives.  

It is meant to reside on an NTFS+FAT dual partitioned drive, on the FAT
partition, and load the NTFS EFI driver before handing over to the
bootx64_efi loader residing there.

## Prerequisites

* MinGW-w64 (with msys, if running on Windows)
* QEMU
* git, wget, unzip

## Sub-Module initialization

For convenience, the project relies on the gnu-efi library (but __not__ on
the gnu-efi compiler itself), so you need to initialize the git submodules:
```
git submodule init
git submodule update
```

## Compilation and testing

Run `make`. You can also perform virtual testing by issuing `make qemu`.