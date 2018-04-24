/*
 * uefi-ntfs: UEFI/NTFS chain loader
 * Copyright © 2014-2018 Pete Batard <pete@akeo.ie>
 * With parts from GRUB © 2006-2015 Free Software Foundation, Inc.
 * With parts from rEFInd © 2012-2016 Roderick W. Smith
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

#define FILE_INFO_SIZE  (512 * sizeof(CHAR16))
#define NUM_RETRIES     1
#define DELAY           3	// delay before retry, in seconds

EFI_HANDLE MainImageHandle = NULL;
// NB: FreePool(NULL) is perfectly valid
#define SafeFree(p) do { FreePool(p); p = NULL;} while(0)

#if defined(_M_X64) || defined(__x86_64__)
// Use 'rufus' in the driver path, so that we don't accidentally latch onto a user driver
  static CHAR16* DriverPath = L"\\efi\\rufus\\ntfs_x64.efi";
// We'll need to fix the casing as our target is a case sensitive file system and Microsoft
// indiscriminately seems to uses "EFI\Boot" or "efi\boot"
  static CHAR16* LoaderPath = L"\\efi\\boot\\bootx64.efi";
// Always good to know the arch we're running
  static CHAR16* Arch = L"x64";
#elif defined(_M_IX86) || defined(__i386__)
  static CHAR16* DriverPath = L"\\efi\\rufus\\ntfs_ia32.efi";
  static CHAR16* LoaderPath = L"\\efi\\boot\\bootia32.efi";
  static CHAR16* Arch = L"ia32";
#elif defined (_M_ARM64) || defined(__aarch64__)
  static CHAR16* DriverPath = L"\\efi\\rufus\\ntfs_aa64.efi";
  static CHAR16* LoaderPath = L"\\efi\\boot\\bootaa64.efi";
  static CHAR16* Arch = L"aa64";
#elif defined (_M_ARM) || defined(__arm__)
  static CHAR16* DriverPath = L"\\efi\\rufus\\ntfs_arm.efi";
  static CHAR16* LoaderPath = L"\\efi\\boot\\bootarm.efi";
  static CHAR16* Arch = L"arm";
#else
#  error Unsupported architecture
#endif

/*
 * In addition to the standard %-based flags, Print() supports the following:
 *   %N       Set output attribute to normal
 *   %H       Set output attribute to highlight
 *   %E       Set output attribute to error
 *   %B       Set output attribute to blue color
 *   %V       Set output attribute to green color
 *   %r       Human readable version of a status code
 */
#define PrintInfo(fmt, ...) Print(L"[INFO] " fmt L"\n", ##__VA_ARGS__);
#define PrintWarning(fmt, ...) Print(L"%E[WARN] " fmt L"%N\n", ##__VA_ARGS__);
#define PrintError(fmt, ...) Print(L"%E[FAIL] " fmt L": [%d] %r%N\n", ##__VA_ARGS__, (Status&0x7FFFFFFF), Status);

/* Return the device path node right before the end node */
static EFI_DEVICE_PATH* GetLastDevicePath(CONST EFI_DEVICE_PATH_PROTOCOL* dp)
{
	EFI_DEVICE_PATH *next, *p;

	if (IsDevicePathEnd(dp))
		return NULL;

	for (p = (EFI_DEVICE_PATH *) dp, next = NextDevicePathNode(p);
		!IsDevicePathEnd(next);
		p = next, next = NextDevicePathNode(next));

	return p;
}

/*
 * Get the parent device in an EFI_DEVICE_PATH
 * Note: the returned device path is allocated and must be freed
 */
static EFI_DEVICE_PATH* GetParentDevice(CONST EFI_DEVICE_PATH* DevicePath)
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

/* Compare device paths */
static INTN CompareDevicePaths(CONST EFI_DEVICE_PATH *dp1, CONST EFI_DEVICE_PATH *dp2)
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

/*
 * Some UEFI firmwares have a *BROKEN* Unicode collation implementation
 * so we must provide our own version of StriCmp for ASCII comparison...
 */
static CHAR16 _tolower(CONST CHAR16 c)
{
	if(('A' <= c) && (c <= 'Z'))
		return 'a' + (c - 'A');
	return c;
}

static INTN _StriCmp(CONST CHAR16 *s1, CONST CHAR16 *s2)
{
	while ((*s1 != L'\0') && (_tolower(*s1) == _tolower(*s2)))
		s1++, s2++;
	return (INTN)(*s1 - *s2);
}

/* Fix the case of a path by looking it up on the file system */
static EFI_STATUS SetPathCase(CONST EFI_FILE_HANDLE Root, CHAR16* Path)
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
		ZeroMem(FileInfo, Size);
		Status = FileHandle->Read(FileHandle, &Size, (VOID*)FileInfo);
		if (EFI_ERROR(Status))
			goto out;
		if (_StriCmp(&Path[i+1], FileInfo->FileName) == 0) {
			StrCpy(&Path[i+1], FileInfo->FileName);
			Status = EFI_SUCCESS;
			goto out;
		}
		Status = EFI_NOT_FOUND;
	} while (Size != 0);

out:
	Path[i] = L'\\';
	if (FileHandle != NULL)
		FileHandle->Close(FileHandle);
	FreePool((VOID*)FileInfo);
	return Status;
}

/* Get the driver name from a driver handle */
static CHAR16* GetDriverName(CONST EFI_HANDLE DriverHandle)
{
	CHAR16 *DriverName;
	EFI_COMPONENT_NAME_PROTOCOL *ComponentName;
	EFI_COMPONENT_NAME2_PROTOCOL *ComponentName2;

	// Try EFI_COMPONENT_NAME2 protocol first
	if ( (BS->OpenProtocol(DriverHandle, &ComponentName2Protocol, (VOID**)&ComponentName2,
			MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL) == EFI_SUCCESS) &&
		 (ComponentName2->GetDriverName(ComponentName2, (CHAR8*)"", &DriverName) == EFI_SUCCESS) )
		return DriverName;

	// Fallback to EFI_COMPONENT_NAME if that didn't work
	if ( (BS->OpenProtocol(DriverHandle, &ComponentNameProtocol, (VOID**)&ComponentName,
			MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL) == EFI_SUCCESS) &&
		 (ComponentName->GetDriverName(ComponentName, (CHAR8*)"", &DriverName) == EFI_SUCCESS) )
		return DriverName;

	return L"(unknown driver)";
}

/*
 * Some UEFI firmwares (like HPQ EFI from HP notebooks) have DiskIo protocols
 * opened BY_DRIVER (by Partition driver in HP's case) even when no file system
 * is produced from this DiskIo. This then blocks our FS driver from connecting
 * and producing file systems.
 * To fix it we disconnect drivers that connected to DiskIo BY_DRIVER if this
 * is a partition volume and if those drivers did not produce file system.
 */
static VOID DisconnectBlockingDrivers(VOID) {
	EFI_STATUS Status;
	UINTN HandleCount = 0, Index, OpenInfoIndex, OpenInfoCount;
	EFI_HANDLE *Handles = NULL;
	CHAR16 *DevicePathString;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
	EFI_BLOCK_IO_PROTOCOL *BlockIo;
	EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *OpenInfo;

	// Get all DiskIo handles
	Status = BS->LocateHandleBuffer(ByProtocol, &gEfiDiskIoProtocolGuid, NULL, &HandleCount, &Handles);
	if (EFI_ERROR(Status) || (HandleCount == 0))
		return;

	// Check every DiskIo handle
	for (Index = 0; Index < HandleCount; Index++) {
		// If this is not partition - skip it.
		// This is then whole disk and DiskIo
		// should be opened here BY_DRIVER by Partition driver
		// to produce partition volumes.
		Status = BS->OpenProtocol(Handles[Index], &gEfiBlockIoProtocolGuid, (VOID**)&BlockIo,
			MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

		if (EFI_ERROR(Status))
			continue;
		if ((BlockIo->Media == NULL) || (!BlockIo->Media->LogicalPartition))
			continue;

		// If SimpleFileSystem is already produced - skip it, this is ok
		Status = BS->OpenProtocol(Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID**)&Volume,
			MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (Status == EFI_SUCCESS)
			continue;

		DevicePathString = DevicePathToStr(DevicePathFromHandle(Handles[Index]));

		// If no SimpleFileSystem on this handle but DiskIo is opened BY_DRIVER
		// then disconnect this connection
		Status = BS->OpenProtocolInformation(Handles[Index], &gEfiDiskIoProtocolGuid, &OpenInfo, &OpenInfoCount);
		if (EFI_ERROR(Status)) {
			PrintWarning(L"Could not get DiskIo protocol for %s: %r", DevicePathString, Status);
			FreePool(DevicePathString);
			continue;
		}

		for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {
			if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {
				Status = BS->DisconnectController(Handles[Index], OpenInfo[OpenInfoIndex].AgentHandle, NULL);
				if (EFI_ERROR(Status)) {
					PrintError(L"Could not disconnect '%s' on %s",
						GetDriverName(OpenInfo[OpenInfoIndex].AgentHandle), DevicePathString);
				} else {
					PrintWarning(L"Disconnected '%s' on %s ",
						GetDriverName(OpenInfo[OpenInfoIndex].AgentHandle), DevicePathString);
				}
			}
		}
		FreePool(DevicePathString);
		FreePool(OpenInfo);
	}
	FreePool(Handles);
}

/*
 * Application entry-point
 * NB: This must be set to 'efi_main' for gnu-efi crt0 compatibility
 */
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
	EFI_STATUS Status;
	EFI_DEVICE_PATH *DevicePath, *ParentDevicePath = NULL, *BootDiskPath = NULL;
	EFI_DEVICE_PATH *BootPartitionPath = NULL;
	EFI_HANDLE *Handles = NULL, DriverHandle, DriverHandleList[2];
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Volume;
	EFI_FILE_HANDLE Root;
	EFI_BLOCK_IO_PROTOCOL *BlockIo;
	CHAR8 *Buffer, NTFSMagic[] = { 'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
	CHAR16 *DevicePathString;
	UINTN Index, Try, Event, HandleCount = 0;
	BOOLEAN SameDevice, NTFSPartition;

	MainImageHandle = ImageHandle;
	InitializeLib(ImageHandle, SystemTable);

	Print(L"\n%H*** UEFI:NTFS (%s) ***%N\n\n", Arch);

	Status = BS->OpenProtocol(MainImageHandle, &gEfiLoadedImageProtocolGuid,
		(VOID**)&LoadedImage, MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to access boot image interface");
		goto out;
	}

	// Identify our boot partition and disk
	BootPartitionPath = DevicePathFromHandle(LoadedImage->DeviceHandle);
	BootDiskPath = GetParentDevice(BootPartitionPath);

	DevicePathString = DevicePathToStr(BootDiskPath);
	PrintInfo(L"Boot disk: %s", DevicePathString);
	FreePool(DevicePathString);

	PrintInfo(L"Disconnecting possible blocking drivers");
	DisconnectBlockingDrivers();

	PrintInfo(L"Starting NTFS driver");
	DevicePath = FileDevicePath(LoadedImage->DeviceHandle, DriverPath);
	if (DevicePath == NULL) {
		Status = EFI_DEVICE_ERROR;
		PrintError(L"Unable to set path for '%s'", DriverPath);
		goto out;
	}

	// Attempt to load the driver.
	Status = BS->LoadImage(FALSE, MainImageHandle, DevicePath, NULL, 0, &DriverHandle);
	SafeFree(DevicePath);
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to load driver '%s'", DriverPath);
		goto out;
	}

	// NB: Some HP firmwares refuse to start drivers that are not of type 'EFI Boot
	// System Driver'. For instance, a driver of type 'EFI Runtime Driver' produces
	// a 'Load Error' on StartImage() with these firmwares => check the type.
	Status = BS->OpenProtocol(DriverHandle, &gEfiLoadedImageProtocolGuid,
		(VOID**)&LoadedImage, MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to access driver interface");
		goto out;
	}
	if (LoadedImage->ImageCodeType != EfiBootServicesCode) {
		Status = EFI_LOAD_ERROR;
		PrintError(L"'%s' is not a Boot System Driver", DriverPath);
		goto out;
	}

	// Load was a success - attempt to start the driver
	Status = BS->StartImage(DriverHandle, NULL, NULL);
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to start driver");
		goto out;
	}
	PrintInfo(L"Started driver: %s", GetDriverName(DriverHandle));

	PrintInfo(L"Locating the first NTFS partition on the boot device");
	// Now enumerate all disk handles
	Status = BS->LocateHandleBuffer(ByProtocol, &gEfiDiskIoProtocolGuid,
		NULL, &HandleCount, &Handles);
	if (EFI_ERROR(Status)) {
		PrintError(L"Failed to list disks");
		goto out;
	}

	// Go through the partitions and find the one that has the USB Disk we booted from
	// as parent and that isn't the FAT32 boot partition
	for (Index = 0; Index < HandleCount; Index++) {
		// Note: The Device Path obtained from DevicePathFromHandle() should NOT be freed!
		DevicePath = DevicePathFromHandle(Handles[Index]);
		// Eliminate the partition we booted from
		if (CompareDevicePaths(DevicePath, BootPartitionPath) == 0)
			continue;
		// Ensure that we look for the NTFS partition on the same device.
		ParentDevicePath = GetParentDevice(DevicePath);
		SameDevice = (CompareDevicePaths(BootDiskPath, ParentDevicePath) == 0);
		SafeFree(ParentDevicePath);
		// The check breaks QEMU testing (since we can't easily emulate
		// a multipart device on the fly) so only do it for release.
#if !defined(_DEBUG)
		if (!SameDevice)
			continue;
#else
		(VOID)SameDevice;	// Silence a MinGW warning
#endif
		// Read the first block of the partition and look for the NTFS magic in the OEM ID
		Status = BS->OpenProtocol(Handles[Index], &gEfiBlockIoProtocolGuid,
			(VOID**) &BlockIo, MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
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

	if (Index >= HandleCount) {
		Status = EFI_NOT_FOUND;
		PrintError(L"Could not locate NTFS partition");
		goto out;
	}

	PrintInfo(L"Checking if partition needs the NTFS service");
	// Test for presence of file system protocol (to see if there already is
	// an NTFS driver servicing this partition)
	Status = BS->OpenProtocol(Handles[Index], &gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&Volume, MainImageHandle, NULL, EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
	if (Status == EFI_SUCCESS) {
		// An NTFS driver is already set => no need to start ours
		PrintWarning(L"An NTFS service is already loaded");
	} else if (Status == EFI_UNSUPPORTED) {
		// Partition is not being serviced by a file system driver yet => start ours
		PrintInfo(L"Starting NTFS partition service");
		// Calling ConnectController() on a handle, with a NULL-terminated list of
		// drivers will start all the drivers from the list that can service it
		DriverHandleList[0] = DriverHandle;
		DriverHandleList[1] = NULL;
		Status = BS->ConnectController(Handles[Index], DriverHandleList, NULL, TRUE);
		if (EFI_ERROR(Status)) {
			PrintError(L"Could not start NTFS partition service");
			goto out;
		}
	} else {
		PrintError(L"Could not check for NTFS service");
		goto out;
	}

	// Our target file system is case sensitive, so we need to figure out the
	// case sensitive version of LoaderPath

	// Open the the volume, with retry, as we may need to wait before poking
	// at the NTFS content, in case the system is slow to start our service...
	for (Try = 0; ; Try++) {
		PrintInfo(L"Looking for NTFS EFI loader");
		Status = BS->OpenProtocol(Handles[Index], &gEfiSimpleFileSystemProtocolGuid,
			(VOID**)&Volume, MainImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
		if (!EFI_ERROR(Status))
			break;
		PrintError(L"Could not open NTFS volume");
		if (Try >= NUM_RETRIES)
			goto out;
		PrintWarning(L"Waiting %d seconds before retrying...", DELAY);
		BS->Stall(DELAY * 1000000);
	}

	// Open the root directory
	Root = NULL;
	Status = Volume->OpenVolume(Volume, &Root);
	if ((EFI_ERROR(Status)) || (Root == NULL)) {
		PrintError(L"Could not open Root directory");
		goto out;
	}

	// This next call will correct the casing to the required one
	Status = SetPathCase(Root, LoaderPath);
	if (EFI_ERROR(Status)) {
		PrintError(L"Could not locate '%s'", LoaderPath);
		goto out;
	}

	// At this stage, our DevicePath is the partition we are after
	PrintInfo(L"Launching NTFS EFI loader '%s'", &LoaderPath[1]);

	// Now attempt to chain load bootx64.efi on the NTFS partition
	DevicePath = FileDevicePath(Handles[Index], LoaderPath);
	if (DevicePath == NULL) {
		Status = EFI_DEVICE_ERROR;
		PrintError(L"Could not create path");
		goto out;
	}
	Status = BS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, &DriverHandle);
	SafeFree(DevicePath);
	if (EFI_ERROR(Status)) {
		PrintError(L"Load failure");
		goto out;
	}

	Status = BS->StartImage(DriverHandle, NULL, NULL);
	if (EFI_ERROR(Status))
		PrintError(L"Start failure");

out:
	SafeFree(ParentDevicePath);
	SafeFree(BootDiskPath);
	SafeFree(Handles);

	// Wait for a keystroke on error
	if (EFI_ERROR(Status)) {
		Print(L"%H\nPress any key to exit.%N\n");
		ST->ConIn->Reset(ST->ConIn, FALSE);
		ST->BootServices->WaitForEvent(1, &ST->ConIn->WaitForKey, &Event);
	}

	return Status;
}
