/*
 * uefi-ntfs: UEFI → NTFS/exFAT chain loader - Path related functions
 * Copyright © 2014-2021 Pete Batard <pete@akeo.ie>
 * With parts from GRUB © 2006 Free Software Foundation, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#include "boot.h"

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
EFI_DEVICE_PATH* GetParentDevice(CONST EFI_DEVICE_PATH* DevicePath)
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

/*
 * Compare two device paths for equality.
 *
 * Note that this code was derived from the the GPLv2+ version of compare_device_paths() found at:
 * https://git.savannah.gnu.org/cgit/grub.git/tree/disk/efi/efidisk.c?id=3572d015fdd9bbd1d17bc45a1f0f0b749e7a3d38#n92
 */
INTN CompareDevicePaths(CONST EFI_DEVICE_PATH *dp1, CONST EFI_DEVICE_PATH *dp2)
{
	if (dp1 == NULL || dp2 == NULL)
		return -1;

	while (1) {
		UINT8 type1, type2;
		UINT8 subtype1, subtype2;
		UINTN len1, len2;
		INTN ret;

		type1 = DevicePathType(dp1);
		type2 = DevicePathType(dp2);

		if (type1 != type2)
			return (INTN) type2 - (INTN) type1;

		subtype1 = DevicePathSubType(dp1);
		subtype2 = DevicePathSubType(dp2);

		if (subtype1 != subtype2)
			return (INTN) subtype1 - (INTN) subtype2;

		len1 = DevicePathNodeLength(dp1);
		len2 = DevicePathNodeLength(dp2);
		if (len1 != len2)
			return (INTN) len1 - (INTN) len2;

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

/* Fix the case of a path by looking it up on the file system */
EFI_STATUS SetPathCase(CONST EFI_FILE_HANDLE Root, CHAR16* Path)
{
	CONST UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + PATH_MAX * sizeof(CHAR16);
	EFI_FILE_HANDLE FileHandle = NULL;
	EFI_FILE_INFO* FileInfo;
	UINTN i, Len;
	UINTN Size;
	EFI_STATUS Status;

	if ((Root == NULL) || (Path == NULL) || (Path[0] != L'\\'))
		return EFI_INVALID_PARAMETER;

	FileInfo = (EFI_FILE_INFO*)AllocatePool(FileInfoSize);
	if (FileInfo == NULL)
		return EFI_OUT_OF_RESOURCES;

	Len = SafeStrLen(Path);
	/* The checks above ensure that Len is always >= 1, but just in case... */
	P_ASSERT(__FILE__, __LINE__, Len >= 1);

	// Find the last backslash in the path
	for (i = Len - 1; (i != 0) && (Path[i] != L'\\'); i--);

	if (i != 0) {
		Path[i] = 0;
		// Recursively fix the case
		Status = SetPathCase(Root, Path);
		if (EFI_ERROR(Status))
			goto out;
	}

	Status = Root->Open(Root, &FileHandle, (i == 0) ? L"\\" : Path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(Status))
		goto out;

	do {
		Size = FileInfoSize;
		ZeroMem(FileInfo, Size);
		Status = FileHandle->Read(FileHandle, &Size, (VOID*)FileInfo);
		if (EFI_ERROR(Status))
			goto out;
		if (_StriCmp(&Path[i + 1], FileInfo->FileName) == 0) {
			SafeStrCpy(&Path[i + 1], PATH_MAX, FileInfo->FileName);
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

/*
 * Poor man's Device Path to string conversion, where we
 * simply convert the path buffer to hexascii.
 * This is needed to support old Dell UEFI platforms, that
 * don't have the Device Path to Text protocol...
 */
CHAR16* DevicePathToHex(CONST EFI_DEVICE_PATH* DevicePath)
{
	UINTN Len = 0, i;
	CHAR16* DevicePathString = NULL;
	UINT8* dp = (UINT8*)DevicePath;

	if (DevicePath == NULL)
		return NULL;

	while (!IsDevicePathEnd(DevicePath)) {
		UINTN NodeLen = DevicePathNodeLength(DevicePath);
		/* Device Paths are provided by UEFI and are sanitized. */
		/* coverity[var_assign_var] */
		Len += NodeLen;
		DevicePath = (EFI_DEVICE_PATH*)((UINT8*)DevicePath + NodeLen);
	}

	DevicePathString = AllocatePool((2 * Len + 1) * sizeof(CHAR16));
	for (i = 0; i < Len; i++) {
		DevicePathString[2 * i] = ((dp[i] >> 4) < 10) ?
			((dp[i] >> 4) + '0') : ((dp[i] >> 4) - 0xa + 'A');
		DevicePathString[2 * i + 1] = ((dp[i] & 15) < 10) ?
			((dp[i] & 15) + '0') : ((dp[i] & 15) - 0xa + 'A');
	}
	DevicePathString[2 * Len] = 0;

	return DevicePathString;
}

/*
 * Convert a Device Path to a string.
 * The returned value Must be freed with FreePool().
 */
CHAR16* DevicePathToString(CONST EFI_DEVICE_PATH* DevicePath)
{
	CHAR16* DevicePathString = NULL;
	EFI_STATUS Status;
	EFI_DEVICE_PATH_TO_TEXT_PROTOCOL* DevicePathToText;

	/* On most platforms, the DevicePathToText protocol should be available */
	Status = gBS->LocateProtocol(&gEfiDevicePathToTextProtocolGuid, NULL, (VOID**)&DevicePathToText);
	if (Status == EFI_SUCCESS)
		DevicePathString = DevicePathToText->ConvertDevicePathToText(DevicePath, FALSE, FALSE);
	else
#if defined(_GNU_EFI)
		DevicePathString = DevicePathToStr((EFI_DEVICE_PATH*)DevicePath);
#else
		DevicePathString = DevicePathToHex(DevicePath);
#endif
	return DevicePathString;
}
