/*
 * uefi-togo: Windows To Go UEFI/FAT -> UEFI/NTFS chain loader
 * Copyright © 2014 Pete Batard <pete@akeo.ie>
 * With parts from GRUB © 2006-2014 Free Software Foundation, Inc.
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

EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = SIMPLE_FILE_SYSTEM_PROTOCOL;
EFI_HANDLE EfiImageHandle = NULL;
// NB: FreePool(NULL) is perfectly valid
#define SafeFree(p) do { FreePool(p); p = NULL;} while(0)
// >>> IMPORTANT: THIS MUST BE COMMENTED OUT FOR AN ACTUAL RELEASE <<<
#define QEMU_DEBUG

// We use 'rufus' in the driver path, so that we don't accidentaly latch on a user driver
static CHAR16* DriverPath = L"efi\\rufus\\ntfs_x64.efi";
// As created by bcdboot.exe - CASE SENSITIVE!!
static CHAR16* LoaderPath = L"EFI\\Boot\\bootx64.efi";

// Display a human readable error message
static VOID PrintStatusError(EFI_STATUS Status, const CHAR16 *Format, ...)
{
	CHAR16 StatusString[64];
	va_list ap;

	StatusToString(StatusString, Status);
	va_start(ap, Format);
	VPrint((CHAR16 *)Format, ap);
	va_end(ap);
	Print(L": [%d] %s\n", Status, StatusString); 
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
static int CompareDevicePaths(const EFI_DEVICE_PATH *dp1, const EFI_DEVICE_PATH *dp2)
{
	if (dp1 == NULL || dp2 == NULL)
		return -1;

	while (1) {
		UINT8 type1, type2;
		UINT8 subtype1, subtype2;
		UINT16 len1, len2;
		int ret;

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

// Appplication entrypoint
EFI_STATUS EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	EFI_DEVICE_PATH *DevicePath, *ParentDevicePath = NULL, *USBDiskPath = NULL, *BootPartitionPath = NULL;
	EFI_HANDLE *Handle = NULL, DriverHandle;
	UINTN i, NumHandles;

	EfiImageHandle = ImageHandle;
	InitializeLib(ImageHandle, SystemTable);
	// Store the system table for future use in other functions
	ST = SystemTable;

	Print(L"\n*** Rufus UEFI:TOGO ***\n\n");

#ifdef QEMU_DEBUG
	Print(L"WARNING: QEMU_DEBUG is ENABLED - This should only be set for QEMU testing!\n\n");
#endif

	Print(L"Loading NTFS Driver... ");
	// Enumerate all filesystem handles, to locate our boot partition
	Status = BS->LocateHandleBuffer(ByProtocol, &FileSystemProtocol, NULL, &NumHandles, &Handle);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  Failed to list file systems");
		goto out;
	}

	for (i = 0; i < NumHandles; i++) {
		// Look for our NTFS driver. Note: the path MUST be specified using backslashes!
		DevicePath = FileDevicePath(Handle[i], DriverPath);
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
		BootPartitionPath = DevicePathFromHandle(Handle[i]);
		USBDiskPath = GetParentDevice(BootPartitionPath);

		break;
	}
	SafeFree(Handle);
	
	if (i >= NumHandles) {
		Print(L"\n  Failed to locate driver. Please check that '%s' exists on the FAT partition", DriverPath);
		Status = EFI_NOT_FOUND;
		goto out;
	}
	Print(L"DONE\nLocating NTFS partition on this device... ");

	// Now enumerate all disk handles
	Status = BS->LocateHandleBuffer(ByProtocol, &DiskIoProtocol, NULL, &NumHandles, &Handle);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  Failed to list disks");
		goto out;
	}

	// Go through the partitions and find the one that has the USB Disk we booted from
	// as parent and that isn't the FAT32 boot paritition
	for (i = 0; i < NumHandles; i++) {
		// Note: The Device Path obtained from DevicePathFromHandle() should NOT be freed!
		DevicePath = DevicePathFromHandle(Handle[i]);
		// Eliminate the partition we booted from
		if (CompareDevicePaths(DevicePath, BootPartitionPath) == 0)
			continue;
		ParentDevicePath = GetParentDevice(DevicePath);
#ifdef QEMU_DEBUG
		// We can't easily emulate a multipart device on the fly for testing with QEMU, so we just hardcode things
		if (i == 3) {
			SafeFree(ParentDevicePath);
			break;
		}
#else
		if (CompareDevicePaths(USBDiskPath, ParentDevicePath) == 0) {
			// Bingo!
			SafeFree(ParentDevicePath);
			break;
		}
#endif
	}

	if (i >= NumHandles) {
		Print(L"\n  ERROR: NTFS partition was not found.\n");
		Status = EFI_NOT_FOUND;
		goto out;
	}

	// Calling ConnectController() on a handle starts all the drivers that can service it
	Status = BS->ConnectController(Handle[i], NULL, NULL, TRUE);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"\n  NTFS partition could not be mounted");
		goto out;
	}


	// At this stage, our DevicePath is the partition we are after
	Print(L"DONE\nLaunching NTFS EFI loader '%s'...\n\n", LoaderPath);

	// Now attempt to chain load bootx64.efi on the NTFS partition
	DevicePath = FileDevicePath(Handle[i], LoaderPath);
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
