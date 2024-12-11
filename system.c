/*
 * uefi-ntfs: UEFI → NTFS/exFAT chain loader - System Information
 * Copyright © 2014-2021 Pete Batard <pete@akeo.ie>
 * With parts from EDK © 1998  Intel Corporation
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

/*
 * Read a system configuration table from a TableGuid.
 */
static EFI_STATUS GetSystemConfigurationTable(EFI_GUID* TableGuid, VOID** Table)
{
	UINTN Index;
	V_ASSERT(Table != NULL);

	for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
		if (CompareGuid(TableGuid, &(gST->ConfigurationTable[Index].VendorGuid))) {
			*Table = gST->ConfigurationTable[Index].VendorTable;
			return EFI_SUCCESS;
		}
	}
	return EFI_NOT_FOUND;
}

/*
 * Return SMBIOS string given the string number.
 * Arguments:
 *  Smbios          - Pointer to SMBIOS structure
 *  StringNumber    - String number to return. 0xFFFF can be used to skip all
 *                    strings and point to the next SMBIOS structure.
 * Returns:
 *  Pointer to string, or pointer to next SMBIOS structure if StringNumber == 0xFFFF.
 */
static CHAR8* GetSmbiosString(SMBIOS_STRUCTURE_POINTER* Smbios, UINT16 StringNumber)
{
	UINT16 Index;
	CHAR8* String;

	// Skip over formatted section
	String = (CHAR8*)(Smbios->Raw + Smbios->Hdr->Length);

	// Look through unformated section
	for (Index = 1; Index <= StringNumber; Index++) {
		if (StringNumber == Index)
			return String;

		// Skip string
		for (; *String != 0; String++);
		String++;

		if (*String == 0) {
			// If double NUL then we are done.
			// Return pointer to next structure in Smbios.
			// If you pass 0xFFFF for StringNumber you always get here.
			Smbios->Raw = (UINT8*)++String;
			return NULL;
		}
	}
	return NULL;
}

/*
 * Query SMBIOS to display some info about the system hardware and UEFI firmware.
 */
EFI_STATUS PrintSystemInfo(VOID)
{
	EFI_STATUS Status;
	SMBIOS_STRUCTURE_POINTER Smbios;
	SMBIOS_TABLE_ENTRY_POINT* SmbiosTable;
	SMBIOS_TABLE_3_0_ENTRY_POINT* Smbios3Table;
	UINT8 Found = 0, *Raw;
	UINTN MaximumSize, ProcessedSize = 0;

	PrintInfo(L"UEFI v%d.%d (%s, 0x%08X)", gST->Hdr.Revision >> 16, gST->Hdr.Revision & 0xFFFF,
		gST->FirmwareVendor, gST->FirmwareRevision);

	Status = GetSystemConfigurationTable(&gEfiSmbios3TableGuid, (VOID**)&Smbios3Table);
	if (Status == EFI_SUCCESS) {
		Smbios.Hdr = (SMBIOS_STRUCTURE*)(UINTN)Smbios3Table->TableAddress;
		MaximumSize = (UINTN)Smbios3Table->TableMaximumSize;
	} else {
		Status = GetSystemConfigurationTable(&gEfiSmbiosTableGuid, (VOID**)&SmbiosTable);
		if (EFI_ERROR(Status))
			return EFI_NOT_FOUND;
		Smbios.Hdr = (SMBIOS_STRUCTURE*)(UINTN)SmbiosTable->TableAddress;
		MaximumSize = (UINTN)SmbiosTable->TableLength;
	}
	// Sanity check
	if (MaximumSize > 1024 * 1024) {
		PrintWarning(L"Aborting system report due to unexpected SMBIOS table length (0x%08X)", MaximumSize);
		return EFI_ABORTED;
	}

	while ((Smbios.Hdr->Type != 0x7F) && (Found < 2)) {
		Raw = Smbios.Raw;
		if (Smbios.Hdr->Type == 0) {
			PrintInfo(L"%a %a", GetSmbiosString(&Smbios, Smbios.Type0->Vendor),
				GetSmbiosString(&Smbios, Smbios.Type0->BiosVersion));
			Found++;
		}
		if (Smbios.Hdr->Type == 1) {
			PrintInfo(L"%a %a", GetSmbiosString(&Smbios, Smbios.Type1->Manufacturer),
				GetSmbiosString(&Smbios, Smbios.Type1->ProductName));
			Found++;
		}
		GetSmbiosString(&Smbios, 0xFFFF);
		ProcessedSize += (UINTN)Smbios.Raw - (UINTN)Raw;
		if (ProcessedSize > MaximumSize) {
			PrintWarning(L"Aborting system report due to noncompliant SMBIOS");
			return EFI_ABORTED;
		}
	}

	return EFI_SUCCESS;
}

/*
 * Query the Secure Boot related firmware variables.
 * Returns:
 *  >0 if Secure Boot is enabled
 *   0 if Secure Boot is disabled
 *  <0 if the system is in Setup Mode
 */
INTN GetSecureBootStatus(VOID)
{
	UINT8 SecureBoot = 0, SetupMode = 0;
	UINTN Size;
	/* Tri-state status for Secure Boot: -1 = Setup, 0 = Disabled, 1 = Enabled */
	INTN SecureBootStatus = 0;

	// Check if the SecureBoot variable exists
	Size = sizeof(SecureBoot);
	if (gRT->GetVariable(L"SecureBoot", &gEfiGlobalVariableGuid, NULL, &Size, &SecureBoot) == EFI_SUCCESS) {
		// The "SecureBoot" variable indicates whether the platform firmware
		// is operating in Secure Boot mode (1) or not (0).
		SecureBootStatus = (INTN)SecureBoot;

		// The "SetupMode" variable indicates whether the platform firmware
		// is operating in Secure Boot Setup Mode (1) or not (0).
		Size = sizeof(SetupMode);
		if ((gRT->GetVariable(L"SetupMode", &gEfiGlobalVariableGuid, NULL, &Size, &SetupMode) == EFI_SUCCESS) && (SetupMode != 0))
			SecureBootStatus = -1;
	}

	return SecureBootStatus;
}
