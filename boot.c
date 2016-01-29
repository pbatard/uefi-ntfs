/*
 * uefi-ntfs: UEFI/NTFS chain loader
 * Copyright © 2014-2016 Pete Batard <pete@akeo.ie>
 * With parts from GRUB © 2006-2015 Free Software Foundation, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <efi.h>
#include <efilib.h>
#include <efistdarg.h>

#define FILE_INFO_SIZE (512 * sizeof(CHAR16))

EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = SIMPLE_FILE_SYSTEM_PROTOCOL;
EFI_HANDLE EfiImageHandle = NULL;
// NB: FreePool(NULL) is perfectly valid
#define SafeFree(p) do { FreePool(p); p = NULL;} while(0)

// Use 'rufus' in the driver path, so that we don't accidentally latch onto a user driver
#if defined(_M_X64) || defined(__x86_64__)
  static CHAR16* DriverPath = L"\\efi\\rufus\\ntfs_x64.efi";
#else
  static CHAR16* DriverPath = L"\\efi\\rufus\\ntfs_x32.efi";
#endif
// We'll need to fix the casing as our target is a case sensitive file system and Microsoft
// indiscriminately seems to uses "EFI\Boot" or "efi\boot"
#if defined(_M_X64) || defined(__x86_64__)
  static CHAR16* LoaderPath = L"\\efi\\boot\\bootx64.efi";
#else
  static CHAR16* LoaderPath = L"\\efi\\boot\\bootia32.efi";
#endif
// Always good to know if we're actually running 32 or 64 bit
#if defined(_M_X64) || defined(__x86_64__)
  static CHAR16* Arch = L"64";
#else
  static CHAR16* Arch = L"32";
#endif

// Display a human readable error message
static VOID PrintStatusError(EFI_STATUS Status, const CHAR16 *Format, ...)
{
	CHAR16 StatusString[64];
	va_list ap;

	StatusToString(StatusString, Status);
	va_start(ap, Format);
	VPrint((CHAR16 *)Format, ap);
	va_end(ap);
	Print(L": [%d] %s\n", (Status & 0x7FFFFFFF), StatusString);
}

// Return the device path node right before the end node
static EFI_DEVICE_PATH* GetLastDevicePath(const EFI_DEVICE_PATH* dp)
{
	EFI_DEVICE_PATH *next, *p;

	if (IsDevicePathEnd(dp))
		return NULL;

	for (p = (EFI_DEVICE_PATH *) dp, next = NextDevicePathNode(p);
		!IsDevicePathEnd(next);
		p = next, next = NextDevicePathNode(next));

	return p;
}

// Get the parent device in an EFI_DEVICE_PATH
// Note: the returned device path is allocated and must be freed
static EFI_DEVICE_PATH* GetParentDevice(const EFI_DEVICE_PATH* DevicePath)
{
	EFI_DEVICE_PATH *dp, *ldp;

	dp = DuplicateDevicePath((EFI_DEVICE_PATH*)DevicePath);
	if (dp == NULL)
		return NULL;

	ldp = GetLastDevicePath(dp);
	if (ldp == NULL)
		return NULL;

	ldp->Type = END_DEVICE_PATH_TYPE;
	ldp->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;

	SetDevicePathNodeLength(ldp, sizeof (*ldp));

	return dp;
}

// Compare device paths
static INTN CompareDevicePaths(const EFI_DEVICE_PATH *dp1, const EFI_DEVICE_PATH *dp2)
{
	if (dp1 == NULL || dp2 == NULL)
		return -1;

	while (1) {
		UINT8 type1, type2;
		UINT8 subtype1, subtype2;
		UINT16 len1, len2;
		INTN ret;

		type1 = DevicePathType(dp1);
		type2 = DevicePathType(dp2);

		if (type1 != type2)
			return (int) type2 - (int) type1;

		subtype1 = DevicePathSubType(dp1);
		subtype2 = DevicePathSubType(dp2);

		if (subtype1 != subtype2)
			return (int) subtype1 - (int) subtype2;

		len1 = DevicePathNodeLength(dp1);
		len2 = DevicePathNodeLength(dp2);
		if (len1 != len2)
			return (int) len1 - (int) len2;

		ret = CompareMem(dp1, dp2, len1);
		if (ret != 0)
			return ret;

		if (IsDevicePathEnd(dp1))
			break;

		dp1 = (EFI_DEVICE_PATH*) ((char *)dp1 + len1);
		dp2 = (EFI_DEVICE_PATH*) ((char *)dp2 + len2);
	}

	return 0;
}

// Some UEFI firmwares have a *BROKEN* Unicode collation implementation
// so we must provide our own version of StriCmp for ASCII comparison...
static CHAR16 _tolower(CHAR16 c)
{
	if(('A' <= c) && (c <= 'Z'))
		return 'a' + (c - 'A');
	return c;
}

static int _StriCmp(CONST CHAR16 *s1, CONST CHAR16 *s2)
{
	while ((*s1 != L'\0') && (_tolower(*s1) == _tolower(*s2)))
		s1++, s2++;
	return (int)(*s1 - *s2);
}

// Fix the case of a path by looking it up on the file system
static EFI_STATUS SetPathCase(EFI_FILE_HANDLE Root, CHAR16* Path)
{
	EFI_FILE_HANDLE FileHandle = NULL;
	EFI_FILE_INFO* FileInfo;
	UINTN i, Len;
	UINTN Size;
	EFI_STATUS Status;

	if ((Root == NULL) || (Path == NULL) || (Path[0] != L'\\'))
		return EFI_INVALID_PARAMETER;

	FileInfo = (EFI_FILE_INFO*)AllocatePool(FILE_INFO_SIZE);
	if (FileInfo == NULL)
		return EFI_OUT_OF_RESOURCES;

	Len = StrLen(Path);
	// Find the last backslash in the path
	for (i = Len-1; (i != 0) && (Path[i] != L'\\'); i--);

	if (i != 0) {
		Path[i] = 0;
		// Recursively fix the case
		Status = SetPathCase(Root, Path);
		if (EFI_ERROR(Status))
			goto out;
	}

	Status = Root->Open(Root, &FileHandle, (i==0)?L"\\":Path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(Status))
		goto out;

	do {
		Size = FILE_INFO_SIZE;
		Status = FileHandle->Read(FileHandle, &Size, (VOID*)FileInfo);
		if (EFI_ERROR(Status))
			goto out;
		if (_StriCmp(&Path[i+1], FileInfo->FileName) == 0) {
			StrCpy(&Path[i+1], FileInfo->FileName);
			Status = EFI_SUCCESS;
			goto out;
		}
		Status = EFI_NOT_FOUND;
	} while (FileInfo->FileName[0] != 0);

out:
	Path[i] = L'\\';
	if (FileHandle != NULL)
		FileHandle->Close(FileHandle);
	FreePool((VOID*)FileInfo);
	return Status;
}

// Application entrypoint
EFI_STATUS EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	EFI_DEVICE_PATH *DevicePath, *ParentDevicePath = NULL, *USBDiskPath = NULL, *BootPartitionPath = NULL;
	EFI_HANDLE *Handle = NULL, DriverHandle;
	EFI_FILE_IO_INTERFACE* Volume;
	EFI_FILE_HANDLE Root;
	EFI_BLOCK_IO* BlockIo;
	CHAR8 *Buffer, NTFSMagic[] = { 'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
	UINTN h, NumHandles = 0;
	BOOLEAN SameDevice, NTFSPartition;

	EfiImageHandle = ImageHandle;
	InitializeLib(ImageHandle, SystemTable);
	// Store the system table for future use in other functions
	ST = SystemTable;

	Print(L"\n*** UEFI:NTFS (%s-bit) ***\n\n", Arch);

	Print(L"Loading NTFS Driver... ");
	// Enumerate all file system handles, to locate our boot partition
	Status = BS->LocateHandleBuffer(ByProtocol, &FileSystemProtocol, NULL, &NumHandles, &Handle);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  Failed to list file systems");
		goto out;
	}

	for (h = 0; h < NumHandles; h++) {
		// Look for our NTFS driver. Note: the path MUST be specified using backslashes!
		DevicePath = FileDevicePath(Handle[h], DriverPath);
		if (DevicePath == NULL)
			continue;

		// Attempt to load the driver. If that fails, it means we weren't on the right partition
		Status = BS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, &DriverHandle);
		SafeFree(DevicePath);
		if (EFI_ERROR(Status))
			continue;

		// Load was a success - attempt to start the driver
		Status = BS->StartImage(DriverHandle, NULL, NULL);
		if (EFI_ERROR(Status)) {
			PrintStatusError(Status, L"\n  Driver did not start");
			goto out;
		}

		// Keep track of our boot partition as well as the parent disk as we need these
		// to locate the NTFS partition on the same device
		BootPartitionPath = DevicePathFromHandle(Handle[h]);
		USBDiskPath = GetParentDevice(BootPartitionPath);

		break;
	}
	SafeFree(Handle);

	if (h >= NumHandles) {
		Print(L"\n  Failed to locate driver. Please check that '%s' exists on the FAT partition", DriverPath);
		Status = EFI_NOT_FOUND;
		goto out;
	}
	Print(L"DONE\nLocating the first NTFS partition on this device... ");

	// Now enumerate all disk handles
	Status = BS->LocateHandleBuffer(ByProtocol, &DiskIoProtocol, NULL, &NumHandles, &Handle);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  Failed to list disks");
		goto out;
	}

	// Go through the partitions and find the one that has the USB Disk we booted from
	// as parent and that isn't the FAT32 boot partition
	for (h = 0; h < NumHandles; h++) {
		// Note: The Device Path obtained from DevicePathFromHandle() should NOT be freed!
		DevicePath = DevicePathFromHandle(Handle[h]);
		// Eliminate the partition we booted from
		if (CompareDevicePaths(DevicePath, BootPartitionPath) == 0)
			continue;
		// Ensure that we look for the NTFS partition on the same device.
		ParentDevicePath = GetParentDevice(DevicePath);
		SameDevice = (CompareDevicePaths(USBDiskPath, ParentDevicePath) == 0);
		SafeFree(ParentDevicePath);
		// The check breaks QEMU testing (since we can't easily emulate
		// a multipart device on the fly) so only do it for release.
#if !defined(_DEBUG)
		if (!SameDevice)
			continue;
#endif
		// Read the first block of the partition and look for the NTFS magic in the OEM ID
		Status = BS->OpenProtocol(Handle[h], &BlockIoProtocol, (VOID**) &BlockIo,
			EfiImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (EFI_ERROR(Status))
			continue;
		Buffer = (CHAR8*)AllocatePool(BlockIo->Media->BlockSize);
		if (Buffer == NULL)
			continue;
		Status = BlockIo->ReadBlocks(BlockIo, BlockIo->Media->MediaId, 0, BlockIo->Media->BlockSize, Buffer);
		NTFSPartition = (CompareMem(&Buffer[3], NTFSMagic, sizeof(NTFSMagic)) == 0);
		FreePool(Buffer);
		if (EFI_ERROR(Status))
			continue;
		if (NTFSPartition)
			break;
	}

	if (h >= NumHandles) {
		Print(L"\n  ERROR: NTFS partition was not found.\n");
		Status = EFI_NOT_FOUND;
		goto out;
	}

	Print(L"DONE\nFind if partition is already serviced by an NTFS driver... ");
	// Test for presence of file system protocol (to see if there already is
	// an NTFS driver servicing this partition)
	Status = BS->OpenProtocol(Handle[h], &FileSystemProtocol, (VOID**)&Volume,
		EfiImageHandle, NULL, EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
	if (Status == EFI_SUCCESS) {
		// An NTFS driver is already set => no need to start ours
		Print(L"YES\n");
	} else if (Status == EFI_UNSUPPORTED) {
		// Partition is not being serviced by a file system driver yet => start ours
		Print(L"NO\nStarting NTFS service for partition... ");
		// Calling ConnectController() on a handle starts all the drivers that can service it
		Status = BS->ConnectController(Handle[h], NULL, NULL, TRUE);
		if (EFI_ERROR(Status)) {
			PrintStatusError(Status, L"\n  ERROR: Could not start NTFS service");
			goto out;
		}
		Print(L"DONE\n");
	} else {
		PrintStatusError(Status, L"\n  ERROR: Could not check for NTFS service");
		goto out;
	}

	// Our target file system is case sensitive, so we need to figure out the
	// case sensitive version of LoaderPath
	Print(L"Looking for NTFS EFI loader... ");

	// Add a one second delay before we start poking at the NTFS content, in
	// case the system is slow to start our service...
	BS->Stall(1000000);

	// Open the the volume
	Status = BS->OpenProtocol(Handle[h], &FileSystemProtocol, (VOID**)&Volume,
		EfiImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  ERROR: Could not open NTFS volume");
		goto out;
	}

	// Open the root directory
	Root = NULL;
	Status = Volume->OpenVolume(Volume, &Root);
	if ((EFI_ERROR(Status)) || (Root == NULL)) {
		PrintStatusError(Status, L"\n  Could not open Root directory");
		goto out;
	}

	// This next call will correct the casing to the required one
	Status = SetPathCase(Root, LoaderPath);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  ERROR: Could not locate '%s'", LoaderPath);
		goto out;
	}

	// At this stage, our DevicePath is the partition we are after
	Print(L"DONE\nLaunching NTFS EFI loader '%s'...\n\n", &LoaderPath[1]);

	// Now attempt to chain load bootx64.efi on the NTFS partition
	DevicePath = FileDevicePath(Handle[h], LoaderPath);
	if (DevicePath == NULL) {
		Print(L"  ERROR: Failed to create path\n");
		goto out;
	}
	Status = BS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, &DriverHandle);
	SafeFree(DevicePath);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"  Load failure");
		goto out;
	}

	Status = BS->StartImage(DriverHandle, NULL, NULL);
	if (EFI_ERROR(Status))
		PrintStatusError(Status, L"  Start failure");

out:
	SafeFree(ParentDevicePath);
	SafeFree(USBDiskPath);
	SafeFree(Handle);

	// Wait for a keystroke on error
	if (EFI_ERROR(Status)) {
		Print(L"\nPress any key to exit.\n");
		ST->ConIn->Reset(ST->ConIn, FALSE);
		while (ST->ConIn->ReadKeyStroke(ST->ConIn, &Key) == EFI_NOT_READY);
	}

	return Status;
}
