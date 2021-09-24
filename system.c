/*
 * uefi-ntfs: UEFI → NTFS/exFAT chain loader - System Information
 * Copyright © 2014-2021 Pete Batard <pete@akeo.ie>
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
