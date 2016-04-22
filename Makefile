ARCH            = x64
SUBSYSTEM       = 10  # 10 = EFI application

# Try to auto-detect the target ARCH
ifeq ($(shell uname -o),Msys)
  IS_MINGW32    = $(findstring MINGW32,$(shell uname -s))
  IS_MINGW64    = $(findstring MINGW64,$(shell uname -s))
  ifeq ($(IS_MINGW32),MINGW32)
    ARCH        = ia32
  endif
  ifeq ($(IS_MINGW64),MINGW64)
    ARCH        = x64
  endif
else
  ifeq ($(shell uname -m),x86_64)
    ARCH        = x64
  else ifeq ($(shell uname -m),arm)
    ARCH        = arm
    CROSS_COMPILE =
  else
    ARCH        = ia32
  endif
endif

# Auto-detect the host arch for MinGW
ifeq ($(shell uname -m),x86_64)
  MINGW_HOST    = w64
else
  MINGW_HOST    = w32
endif

ifeq ($(ARCH),x64)
  GNUEFI_ARCH   = x86_64
  GCC_ARCH      = x86_64
  QEMU_ARCH     = x86_64
  CROSS_COMPILE = $(GCC_ARCH)-$(MINGW_HOST)-mingw32-
  EP_PREFIX     =
  CFLAGS        = -m64 -mno-red-zone
  LDFLAGS       = -Wl,-dll -Wl,--subsystem,$(SUBSYSTEM)
else ifeq ($(ARCH),ia32)
  GNUEFI_ARCH   = ia32
  GCC_ARCH      = i686
  QEMU_ARCH     = i386
  CROSS_COMPILE = $(GCC_ARCH)-$(MINGW_HOST)-mingw32-
  EP_PREFIX     = _
  CFLAGS        = -m32 -mno-red-zone
  LDFLAGS       = -Wl,-dll -Wl,--subsystem,$(SUBSYSTEM)
else ifeq ($(ARCH),arm)
  GNUEFI_ARCH   = arm
  GCC_ARCH      = arm
  QEMU_ARCH     = arm
  QEMU_OPTS     = -M virt -cpu cortex-a15
  CROSS_COMPILE = $(GCC_ARCH)-linux-gnueabihf-
  EP_PREFIX     =
  CFLAGS        = -marm -fpic -fshort-wchar
  LDFLAGS       = -Wl,--no-wchar-size-warning -Wl,--defsym=EFI_SUBSYSTEM=$(SUBSYSTEM)
  CRT0_LIBS     = -lgnuefi
endif
OVMF_ARCH       = $(shell echo $(ARCH) | tr a-z A-Z)
OVMF_ZIP        = OVMF-$(OVMF_ARCH).zip
GNUEFI_DIR      = $(CURDIR)/gnu-efi
GNUEFI_LIBS     = lib

# If the compiler produces an elf binary, we need to fiddle with a PE crt0
ifneq ($(CRT0_LIBS),)
  CRT0_DIR      = $(GNUEFI_DIR)/$(GNUEFI_ARCH)/gnuefi
  LDFLAGS      += -L$(CRT0_DIR) -T $(GNUEFI_DIR)/gnuefi/elf_$(ARCH)_efi.lds $(CRT0_DIR)/crt0-efi-$(ARCH).o
  GNUEFI_LIBS  += gnuefi
endif

# SYSTEMROOT is only defined on Windows systems
ifneq ($(SYSTEMROOT),)
  QEMU = "/c/Program Files/qemu/qemu-system-$(QEMU_ARCH)w.exe"
  # MinGW on Windows doesn't use (tuple)-ar but (tuple)-gcc-ar
  # so we remove the cross compiler tuple altogether
  CROSS_COMPILE =
else
  QEMU = qemu-system-$(QEMU_ARCH) -nographic
endif

CC             := $(CROSS_COMPILE)gcc
OBJCOPY        := $(CROSS_COMPILE)objcopy
CFLAGS         += -fno-stack-protector -Wshadow -Wall -Wunused -Werror-implicit-function-declaration
CFLAGS         += -I$(GNUEFI_DIR)/inc -I$(GNUEFI_DIR)/inc/$(GNUEFI_ARCH) -I$(GNUEFI_DIR)/inc/protocol
CFLAGS         += -DCONFIG_$(GNUEFI_ARCH) -D__MAKEWITH_GNUEFI -DGNU_EFI_USE_MS_ABI
LDFLAGS        += -L$(GNUEFI_DIR)/$(GNUEFI_ARCH)/lib -e $(EP_PREFIX)efi_main
LDFLAGS        += -s -Wl,-Bsymbolic -nostdlib -shared
LIBS            = -lefi $(CRT0_LIBS)

ifeq (, $(shell which $(CC)))
  $(error The selected compiler ($(CC)) was not found)
endif

GCCVERSION     := $(shell $(CC) -dumpversion | cut -f1 -d.)
GCCMINOR       := $(shell $(CC) -dumpversion | cut -f2 -d.)
GCCMACHINE     := $(shell $(CC) -dumpmachine)
GCCNEWENOUGH   := $(shell ( [ $(GCCVERSION) -gt "4" ]        \
                          || ( [ $(GCCVERSION) -eq "4" ]     \
                              && [ $(GCCMINOR) -ge "7" ] ) ) \
                        && echo 1)
ifneq ($(GCCNEWENOUGH),1)
  $(error You need GCC 4.7 or later)
endif

ifneq ($(GCC_ARCH),$(findstring $(GCC_ARCH), $(GCCMACHINE)))
  $(error The selected compiler ($(CC)) is not set for $(TARGET))
endif

.PHONY: all clean superclean
all: $(GNUEFI_DIR)/$(GNUEFI_ARCH)/lib/libefi.a boot.efi

$(GNUEFI_DIR)/$(GNUEFI_ARCH)/lib/libefi.a:
	$(MAKE) -C$(GNUEFI_DIR) CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(GNUEFI_ARCH) $(GNUEFI_LIBS)

%.efi: %.o
	@echo  [LD]  $(notdir $@)
ifeq ($(CRT0_LIBS),)
	@$(CC) $(LDFLAGS) $< -o $@ $(LIBS)
else
	@$(CC) $(LDFLAGS) $< -o $*.elf $(LIBS)
	@$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel* \
	            -j .rela* -j .reloc -j .eh_frame -O binary $*.elf $@
	@rm -f $*.elf
endif

%.o: %.c
	@echo  [CC]  $(notdir $@)
	@$(CC) $(CFLAGS) -ffreestanding -c $<

qemu: CFLAGS += -D_DEBUG
qemu: all OVMF_$(OVMF_ARCH).fd ntfs.vhd image/efi/boot/boot$(ARCH).efi image/efi/rufus/ntfs_$(ARCH).efi
	$(QEMU) $(QEMU_OPTS) -bios ./OVMF_$(OVMF_ARCH).fd -net none -hda fat:image -hdb ntfs.vhd

image/efi/boot/boot$(ARCH).efi: boot.efi
	mkdir -p image/efi/boot
	cp -f $< $@

# NTFS driver
ntfs_$(ARCH).efi:
	wget http://efi.akeo.ie/downloads/efifs-0.9/$(ARCH)/ntfs_$(ARCH).efi

image/efi/rufus/ntfs_$(ARCH).efi: ntfs_$(ARCH).efi
	mkdir -p image/efi/rufus
	cp -f $< $@

# NTFS test image (contains boot[ia32|x64].efi that say "Hello from NTFS!")
ntfs.vhd:
	wget http://efi.akeo.ie/test/ntfs.zip
	unzip ntfs.zip
	rm ntfs.zip

OVMF_$(OVMF_ARCH).fd:
	wget http://efi.akeo.ie/OVMF/$(OVMF_ZIP)
	unzip $(OVMF_ZIP) OVMF.fd
	mv OVMF.fd OVMF_$(OVMF_ARCH).fd
	rm $(OVMF_ZIP)

clean:
	rm -f boot.efi *.o
	rm -rf image

superclean: clean
	$(MAKE) -C$(GNUEFI_DIR) clean
	rm -f *.fd ntfs.vhd ntfs_*.efi
