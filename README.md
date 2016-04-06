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

The way this works, in conjunction with Rufus, is as follows:

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

* [Visual Studio 2015](http://www.visualstudio.com/products/visual-studio-community-vs)
  or [MinGW](http://www.mingw.org/)/[MinGW64](http://mingw-w64.sourceforge.net/)
  (preferbaly installed using [msys2](https://sourceforge.net/projects/msys2/))
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

If using MinGW, issue: `make TARGET=<arch>` where `<arch>` is one of `ia32`
or `x64`. You can also debug through QEMU with MinGW by adding `qemu` to your
`make` invocation. Be mindful however that this turns the special `_DEBUG`
mode on, and you should run make without invoking `qemu` to produce proper
release binaries.

## Download and installation

You can find a ready-to-use FAT partition image (256KB), containing both the 32
and 64 bit versions of the UEFI:NTFS loader and driver in the Rufus project,
under [/res/uefi](https://github.com/pbatard/rufus/tree/master/res/uefi).

If you create a 256KB partition at the end of your drive and copy
[`uefi-ntfs.img`](https://github.com/pbatard/rufus/blob/master/res/uefi/uefi-ntfs.img?raw=true)
there (in DD mode of course), then you should have everything you need to make
the first NTFS partition on that drive UEFI bootable.

## Visual Studio and ARM support

To enable ARM compilation in Visual Studio 2015, you must perform the following:
* Make sure Visual Studio is fully closed.
* Navigate to `C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\V140\Platforms\ARM` and
  remove the read-only attribute on `Platform.Common.props`.
* With a text editor __running with Administrative privileges__ open:  
  `C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\V140\Platforms\ARM\Platform.Common.props`.
* Under the `<PropertyGroup>` section add the following:  
  `<WindowsSDKDesktopARMSupport>true</WindowsSDKDesktopARMSupport>`
