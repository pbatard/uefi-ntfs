UEFI:NTFS - Boot NTFS or exFAT partitions from UEFI
===================================================

UEFI:NTFS is a generic bootloader, that is designed to allow boot from NTFS or
exFAT partitions, in pure UEFI mode, even if your system does not natively
support it.
This is primarily intended for use with [Rufus](https://rufus.ie), but can also
be used independently.

In other words, UEFI:NTFS is designed to remove the restriction, which most
UEFI systems have, of only providing boot support from a FAT32 partition, and
enable the ability to also boot from NTFS partitions.

This can be used, for instance, to UEFI-boot a Windows NTFS installation media,
containing an `install.wim` that is larger than 4 GB (something FAT32 cannot
support) or to allow dual BIOS + UEFI boot of 'Windows To Go' drives.

As an aside, and because there appears to exist a lot of innacurate information
about this on the Internet, it needs to be stressed out that there is absolutely
nothing in the UEFI specifications that actually forces the use of FAT32 for
UEFI boot. On the contrary, UEFI will happily boot from __ANY__ file system,
as long as your firmware has a driver for it. As such, it is only the choice of
system manufacturers, who tend to only include a driver for FAT32, that limits
the default boot capabilities of UEFI, and that leads many to __erroneously
believe__ that only FAT32 can be used for UEFI boot.

However, as demonstrated in this project, it is very much possible to work
around this limitation and enable any UEFI firmware to boot from non-FAT32
filesystems.

## Overview

The way UEFI:NTFS works, in conjunction with Rufus, is as follows:

* Rufus creates 2 partitions on the target USB disk (these can be MBR or GPT
  partitions). The first one is an NTFS partition occupying almost all the
  drive, that contains the Windows files (for Windows To Go, or for regular
  installation), and the second is a very small FAT partition, located at the
  very end, that contains an NTFS UEFI driver (see https://efi.akeo.ie) as well
  as the UEFI:NTFS bootloader.
* When the USB drive boots in UEFI mode, the first NTFS partition gets ignored
  by the UEFI firmware (unless that firmware already includes an NTFS driver,
  in which case 2 boot options will be available, that perform the same thing)
  and the UEFI:NTFS bootloader from the bootable FAT partition is executed.
* UEFI:NTFS then loads the relevant NTFS UEFI driver, locates the existing NTFS
  partition on the same media, and executes the `/efi/boot/bootia32.efi`,
  `/efi/boot/bootx64.efi`, `/efi/boot/bootarm.efi` or `/efi/boot/bootaa64.efi`
  that resides there. This achieves the exact same outcome as if the UEFI
  firmware had native support for NTFS and could boot straight from it.

## Limitations

__[Secure Boot](https://en.wikipedia.org/wiki/Unified_Extensible_Firmware_Interface#Secure_boot)
must be disabled for UEFI:NTFS to work__.

Now, there are two things to be said about this:

1. If you are using UEFI:NTFS to install Windows, then __temporarily__ disabling
   Secure Boot is not as big deal as you think it is.

   This is because all Secure Boot does, really, is establish trust that the
   files you are booting from have not been maliciously altered... which you
   can pretty much establish yourself if you validated the checksum of the ISO
   and ran your media creation from an environment that you trust.

   For more on this, please see the second part
   from [this entry](https://github.com/pbatard/rufus/wiki/FAQ#Blah_UEFI_Blah_FAT32_therefore_Rufus_should_Blah)
   of the Rufus FAQ.

2. As a developer, I'd like nothing better than be able to sign UEFI:NTFS for
   Secure Boot.

   However, this is not possible because Microsoft have __arbitrarily__
   decided that [they would not sign anything that is GPLv3](https://techcommunity.microsoft.com/t5/Windows-Hardware-Certification/Microsoft-UEFI-CA-Signing-policy-updates/ba-p/364828)
   under the [false pretence](https://www.gnu.org/licenses/gpl-faq.en.html#GiveUpKeys)
   that it would force them to relinquish their private signing keys.

   Of course, this is __hyperbolic nonsense__ since all the GPLv3 mandates is
   that your system cannot lock users out from running their own code if they
   choose so, which, as long as you follow the UEFI guidelines, Secure Boot
   should never do, as it has clear provisions for allowing users to install
   their own keys.

   What this means is that, unfortunately, UEFI:NTFS cannot be submitted to
   Microsoft for Secure Boot signing, as it will be automatically rejected,
   and you currently are left with no choice but to have Secure Boot disabled
   for UEFI:NTFS to run.

   And, because the NTFS driver being used is licensed under the GPLv3 (given
   that its source is derived from GRUB2, which itself is GPLv3, and I am not
   willing to rewrite an NTFS driver from scratch, especially it means giving
   up on the license that I see as best for user rights), it is not possible
   to relicense UEFI:NTFS to anything else but GPLv3.

   Still, if you are unhappy about this situation in any way, I would
   strongly encourage you to contact Microsoft to complain about their blatant
   abuse of power, and their use of using easily refutable "arguments" to
   propagate their [long standing dislike of the GPL license](https://www.theregister.co.uk/2001/06/02/ballmer_linux_is_a_cancer/).

## Prerequisites

* [Visual Studio 2019](https://www.visualstudio.com/vs/community/) or
  [MinGW](http://www.mingw.org/)/[MinGW64](http://mingw-w64.sourceforge.net/)
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
one of `ia32`, `x64`, `arm` or `aa64` and tuple is the one for your cross-compiler
(e.g. `arm-linux-gnueabihf-`).

You can also debug through QEMU by specifying `qemu` to your `make` invocation.
Be mindful however that this turns the special `_DEBUG` mode on, and you should
run make without invoking `qemu` to produce proper release binaries.

## Download and installation

You can find a ready-to-use FAT partition image, containing the x86 and ARM
versions of the UEFI:NTFS loader (both 32 and 64 bit) and driver in the Rufus
project, under [/res/uefi](https://github.com/pbatard/rufus/tree/master/res/uefi).

If you create a partition of the same size at the end of your drive and copy
[`uefi-ntfs.img`](https://github.com/pbatard/rufus/blob/master/res/uefi/uefi-ntfs.img?raw=true)
there (in DD mode of course), then you should have everything you need to make
the first NTFS partition on that drive UEFI bootable.

## Visual Studio 2019 and ARM/ARM64 support

Please be mindful that, to enable ARM or ARM64 compilation support in Visual Studio
2019, you __MUST__ go to the _Individual components_ screen in the setup application
and select the ARM/ARM64 build tools there, as they do __NOT__ appear in the default
_Workloads_ screen:

![VS2019 Individual Components](https://files.akeo.ie/pics/VS2019_Individual_Components.png)
