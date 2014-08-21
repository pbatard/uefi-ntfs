uefi-simple
===========

A simple 64bit UEFI application of Hello World! without using any UEFI toolkit.

## Preparation

If you use Fedora, you first need to install the mingw cross compiler by the following command.

`yum install mingw64-gcc`

If you use a distribution other than Fedora, find a 64bit mingw cross compiler,
and set its name to "CC" in the Makefile.

## Compile & Run

Just typing the following command will compile and run the UEFI application on QEMU.

`make qemu`

The Makefile will download the current latest version of UEFI firmware (OVMF-X64-r15214.zip).
If you can't download it, find the latest version from http://tianocore.sourceforge.net/wiki/OVMF
