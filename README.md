UEFI:TOGO - UEFI bootloader for dual MBR/UEFI Windows To Go
===========================================================

This bootloader is intended for use with [Rufus](http://rufus.akeo.ie), to
allow dual MBR+UEFI booting of Windows To Go USB drives, regardless of
whether they are fixed or removable, something that no other application
(that I know of) seems to be able to provide.

The problem is that Windows To Go must reside on an NTFS partition to be
bootable, and almost no UEFI firmware includes an NTFS driver. This in turn
constrains the way Windows To Go installations can boot, with
[Microsoft's official solution](http://technet.microsoft.com/en-ie/library/jj592685.aspx#stg_firmware)
mandating the use of a fixed disk (which most USB Flash Drives aren't) due
to Windows' restriction of only ever being able to mount the first partition
of a removable disk.

There however exists a __better__ solution, that can apply to both removable
and fixed USB drives, and which is what UEFI:TOGO is all about. The way it
works, in conjuction with Rufus, is as follows:

* Rufus creates 2 MBR partitions on the target USB disk. The first one is an
  NTFS partition occupying almost all the drive, with the To Go files, and
  the second is a very small FAT partition located at the end, containing
  only an NTFS EFI driver and the UEFI:TOGO EFI bootloader.
* When the USB drive boots on a BIOS machine, or the user chooses to select
  legacy mode on UEFI, the MBR bootloader is executed, which hands over to
  the BIOS-compatible Windows loader on the NTFS partition.
* When the USB drive boots on an UEFI machine (that doesn't have an NTFS EFI
  driver in its firmare), the first NTFS partition gets ignored and the
  bootloader from the second FAT partition, UEFI:TOGO, gets executed
* UEFI:TOGO then loads an NTFS EFI driver that exists on the FAT partition,
  opens the NTFS partition and hands over to the Windows To Go EFI
  bootloader that resides there.

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