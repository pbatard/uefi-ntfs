/*
 * uefi-togo: Windows To Go UEFI/FAT -> UEFI/NTFS chain loader
 * Copyright © 2014 Pete Batard <pete@akeo.ie>
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

VOID PrintStatusError(EFI_STATUS Status, const CHAR16 *Format, ...)
{
	CHAR16 StatusString[64];
	va_list ap;

	StatusToString(StatusString, Status);
	va_start(ap, Format);
	VPrint((CHAR16 *)Format, ap);
	va_end(ap);
	Print(L": [%d] %s\n", Status, StatusString); 
}

EFI_STATUS EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	EFI_DEVICE_PATH *DevicePath;
	EFI_HANDLE *Handle, DriverHandle;
	UINTN i, NumHandles;
	CHAR16 *DevicePathString;

	InitializeLib(ImageHandle, SystemTable);
	// Store the system table for future use in other functions
	ST = SystemTable;

	Print(L"\nUEFI:TOGO\n\n");

	// Enumerate all handles
	Status = BS->LocateHandleBuffer(ByProtocol, &FileSystemProtocol, NULL, &NumHandles, &Handle);
	if (EFI_ERROR(Status)) {
		PrintStatusError(Status, L"LocateHandleBuffer failed\n");
		goto out;
	}
	Print(L"Got %d handles\n", NumHandles);

	for (i = 0; i < NumHandles; i++) {
		// The path MUST use backslashes!
		DevicePath = FileDevicePath(Handle[i], L"efi\\boot\\ntfs_x64.efi");
		if (DevicePath == NULL) {
			Status = EFI_NO_MAPPING;
			PrintStatusError(Status, L"Could not get Device Path");
			goto out;
		}
		DevicePathString = DevicePathToStr(DevicePath);
		if (DevicePathString == NULL) {
			Status = EFI_OUT_OF_RESOURCES;
			PrintStatusError(Status, L"Could not allocate Device Path string");
			goto out;
		}
		Print(L"DevicePath[%d] = '%s'\n", i, DevicePathString);
		Status = BS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, &DriverHandle);
		if (EFI_ERROR(Status)) {
			PrintStatusError(Status, L"Could not load NTFS driver");
			continue;
		}
		Status = BS->StartImage(DriverHandle, NULL, NULL);
		if (EFI_ERROR(Status)) {
			PrintStatusError(Status, L"Could not start NTFS driver");
			break;
		}
		Print(L"SUCCESS!!\n");
		break;
	}

out:
	// TODO: free resources

	// Wait for a keystroke
	ST->ConIn->Reset(ST->ConIn, FALSE);
	while (ST->ConIn->ReadKeyStroke(ST->ConIn, &Key) == EFI_NOT_READY);

	return Status;
}
