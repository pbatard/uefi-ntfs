UEFI:NTFS - Boot NTFS partitions from UEFI
==========================================

This generic bootloader (which is primarily intended for use with [Rufus](https://rufus.akeo.ie)
but can also be used independently), is meant to allow seamless boot from an
EFI bootloader, that resides on an NTFS partition. In other words, UEFI:NTFS is
designed to remove the UEFI restriction of being able to natively boot from
FAT32 only, and allow NTFS boot without the need for any user intervention.

This can be used, for instance, for booting a Windows NTFS installation media,
in UEFI mode, thus allowing support for files that are larger than 4GB
(something a native UEFI FAT32 partition cannot support), or allow
indiscriminate BIOS+UEFI boot of Windows To Go drives.

The way this works, in conjunction with Rufus, is as follows:

* Rufus creates 2 partitions on the target USB disk (these can be MBR or GPT
  partitions). The first one is an NTFS partition occupying almost all the
  drive, that contains the Windows files (for Windows To Go, or for regular
  installation), and the second is a very small FAT partition, located at the
  very end, that contains an NTFS UEFI driver (see http://efi.akeo.ie) as well
  as the UEFI:NTFS bootloader.
* When the USB drive boots in UEFI mode, the first NTFS partition gets ignored
  by the UEFI firmware (unless that firmware already includes an NTFS driver,
  in which case 2 boot options will be available, that perform the same thing)
  and the UEFI:NTFS bootloader from the bootable FAT partition is executed.
* UEFI:NTFS then loads the relevant NTFS UEFI driver, locates the existing NTFS
  partition on the same media, and executes the `/efi/boot/bootia32.efi`,
  `/efi/boot/bootx64.efi` or `/efi/boot/bootarm.efi` that resides there. This
  achieves the exact same outcome as if the UEFI firmware had native support
  for NTFS and could boot straight from it.

## Prerequisites

* [Visual Studio 2017](https://www.visualstudio.com/vs/community/) or
  or [MinGW](http://www.mingw.org/)/[MinGW64](http://mingw-w64.sourceforge.net/)
  (preferably installed using [msys2](https://sourceforge.net/projects/msys2/)) or gcc
* [QEMU](http://www.qemu.org) __v2.7 or later__
  (NB: You can find QEMU Windows binaries [here](https://qemu.weilnetz.de/w64/))
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

If using gcc, you should be able to simply issue `make`. If needed you can also
issue something like `make ARCH=<arch> CROSS_COMPILE=<tuple>` where `<arch>` is
one of `ia32`, `x64` or `arm` and tuple is the one for your cross-compiler (e.g.
`arm-linux-gnueabihf-`).

You can also debug through QEMU by specifying `qemu` to your `make` invocation.
Be mindful however that this turns the special `_DEBUG` mode on, and you should
run make without invoking `qemu` to produce proper release binaries.

## Download and installation

You can find a ready-to-use FAT partition image, containing the x86 (both 32 and
64 bit) and ARM (32 bit) versions of the UEFI:NTFS loader and driver in the
Rufus project, under [/res/uefi](https://github.com/pbatard/rufus/tree/master/res/uefi).

If you create a partition of the same size at the end of your drive and copy
[`uefi-ntfs.img`](https://github.com/pbatard/rufus/blob/master/res/uefi/uefi-ntfs.img?raw=true)
there (in DD mode of course), then you should have everything you need to make
the first NTFS partition on that drive UEFI bootable.

## Visual Studio 2017 and ARM support

Please be mindful that, to enable ARM compilation support in Visual Studio 2017,
you __MUST__ go to the _Individual components_ screen in the setup application
and select the ARM compilers and libraries there, as they do __NOT__ appear in
the default _Workloads_ screen:

![VS2017 Individual Components](http://files.akeo.ie/pics/VS2017_Individual_Components.png)

While in this section, you may also want to select the installation of _Clang/C2
(experimental)_, so that you can open and compile the Clang solution...

