CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -Os -mno-red-zone -fpic -fshort-wchar -fno-stack-protector -Wshadow -Wall -Wunused -Werror-implicit-function-declaration
CFLAGS += -I$(GNUEFI_PATH)/inc -I$(GNUEFI_PATH)/inc/x86_64 -I$(GNUEFI_PATH)/inc/protocol
qemu: CFLAGS += -D_DEBUG
# Linker option '--subsystem 10' specifies an EFI application.
LDFLAGS = -s -nostdlib -shared -Wl,-dll -Wl,--subsystem,10 -e EfiMain
LIBS    = -L$(GNUEFI_PATH)/lib -lgcc -lefi

GNUEFI_PATH = $(CURDIR)/gnu-efi
# Set parameters according to our platform
ifeq ($(SYSTEMROOT),)
  QEMU = qemu-system-x86_64 -nographic
  CROSS_COMPILE = x86_64-w64-mingw32-
else
  QEMU = "/c/Program Files/qemu/qemu-system-x86_64w.exe"
  CROSS_COMPILE =
endif
OVMF_ZIP = OVMF-X64-r15214.zip

GCCVERSION := $(shell $(CC) -dumpversion | cut -f1 -d.)
GCCMINOR   := $(shell $(CC) -dumpversion | cut -f2 -d.)
GCCNEWENOUGH := $(shell ( [ $(GCCVERSION) -gt "4" ]           \
                          || ( [ $(GCCVERSION) -eq "4" ]      \
                               && [ $(GCCMINOR) -ge "7" ] ) ) \
                        && echo 1)
ifneq ($(GCCNEWENOUGH),1)
  $(error You need GCC 4.7 or later)
endif

.PHONY: all
all: boot.efi

$(GNUEFI_PATH)/lib/libefi.a:
	$(MAKE) -C$(GNUEFI_PATH)/lib/ CROSS_COMPILE=$(CROSS_COMPILE)

%.efi: %.o $(GNUEFI_PATH)/lib/libefi.a
	$(CC) $(LDFLAGS) $< -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -ffreestanding -c $<

qemu: boot.efi OVMF.fd ntfs.vhd image/efi/boot/bootx64.efi image/efi/rufus/ntfs_x64.efi
	$(QEMU) -bios ./OVMF.fd -net none -hda fat:image -hdb ntfs.vhd

image/efi/boot/bootx64.efi: boot.efi
	mkdir -p image/efi/boot
	cp -f $< $@

# NTFS driver
ntfs_x64.efi:
	wget http://efi.akeo.ie/downloads/efifs-0.8/x86_64/ntfs_x64.efi

image/efi/rufus/ntfs_x64.efi: ntfs_x64.efi
	mkdir -p image/efi/rufus
	cp -f $< $@

# NTFS test image (contains a bootx64.efi that says "Hello from NTFS!")
ntfs.vhd:
	wget http://efi.akeo.ie/test/ntfs.zip
	unzip ntfs.zip
	rm ntfs.zip

OVMF.fd:
	# Use an explicit FTP mirror, since SF's HTTP download links are more miss than hit...
	wget http://efi.akeo.ie/OVMF/$(OVMF_ZIP)
	unzip $(OVMF_ZIP) OVMF.fd
	rm $(OVMF_ZIP)

clean:
	rm -f boot.efi *.o
	rm -rf image

superclean: clean
	$(MAKE) -C$(GNUEFI_PATH)/lib/ clean
	rm -f OVMF.fd ntfs.vhd ntfs_x64.efi
