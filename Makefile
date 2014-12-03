CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -mno-red-zone -fno-stack-protector -Wshadow -Wall -Wunused -Werror-implicit-function-declaration
CFLAGS += -I$(GNUEFI_PATH)/inc -I$(GNUEFI_PATH)/inc/x86_64 -I$(GNUEFI_PATH)/inc/protocol
# Linker option '--subsystem 10' specifies an EFI application. 
LDFLAGS = -nostdlib -shared -Wl,-dll -Wl,--subsystem,10 -e efi_main 
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


.PHONY: all
all: togo.efi

$(GNUEFI_PATH)/lib/libefi.a:
	$(MAKE) -C$(GNUEFI_PATH)/lib/ CROSS_COMPILE=$(CROSS_COMPILE)

%.efi: %.o $(GNUEFI_PATH)/lib/libefi.a
	$(CC) $(LDFLAGS) $< -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -ffreestanding -c $<

qemu: togo.efi OVMF.fd ntfs.vhd image/efi/boot/bootx64.efi image/efi/boot/ntfs_x64.efi
	$(QEMU) -bios ./OVMF.fd -hda fat:image -hdb ntfs.vhd

image/efi/boot/bootx64.efi: togo.efi
	mkdir -p image/efi/boot
	cp -f togo.efi $@

# NTFS driver
image/efi/boot/ntfs_x64.efi:
	wget http://efi.akeo.ie/downloads/efifs-0.6.1/x64/ntfs_x64.efi
	cp -f ntfs_x64.efi $@

# NTFS test image (contains a bootx64.efi that says "Hello from NTFS!")
ntfs.vhd:
	wget http://efi.akeo.ie/test/ntfs.zip
	unzip ntfs.zip
	rm ntfs.zip

OVMF.fd:
	# Use an explicit FTP mirror, since SF's HTTP download links are more miss than hit...
	wget ftp://ftp.heanet.ie/pub/download.sourceforge.net/pub/sourceforge/e/ed/edk2/OVMF/$(OVMF_ZIP)
	unzip $(OVMF_ZIP) OVMF.fd
	rm $(OVMF_ZIP)

clean:
	rm -f togo.efi *.o
	rm -rf image

superclean: clean
	$(MAKE) -C$(GNUEFI_PATH)/lib/ clean
	rm -f OVMF.fd ntfs.vhd ntfs_x64.efi
