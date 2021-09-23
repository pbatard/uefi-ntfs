/*
 * uefi-ntfs: UEFI → NTFS/exFAT chain loader
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

#include <efi.h>
#include <efilib.h>
#include <efistdarg.h>
#include <libsmbios.h>

/* Maximum size for the File Info structure we query */
#define FILE_INFO_SIZE      (512 * sizeof(CHAR16))

/* Number of times we retry opening a volume */
#define NUM_RETRIES         1

/* Delay before retry, in seconds*/
#define DELAY               3

/* Macro used to compute the size of an array */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(Array)   (sizeof(Array) / sizeof((Array)[0]))
#endif

/* FreePool() replacement, that NULLs the freed pointer. */
#define SafeFree(p)          do { FreePool(p); p = NULL;} while(0)

/*
 * Convenience macros to print informational, warning or error messages.
 * NB: In addition to the standard %-based flags, Print() supports the following:
 *   %N       Set output attribute to normal
 *   %H       Set output attribute to highlight
 *   %E       Set output attribute to error
 *   %B       Set output attribute to blue color
 *   %V       Set output attribute to green color
 *   %r       Human readable version of a status code
 */
#define PrintInfo(fmt, ...)     Print(L"[INFO] " fmt L"\n", ##__VA_ARGS__)
#define PrintWarning(fmt, ...)  Print(L"%E[WARN] " fmt L"%N\n", ##__VA_ARGS__)
#define PrintError(fmt, ...)    Print(L"%E[FAIL] " fmt L": [%d] %r%N\n", ##__VA_ARGS__, (Status&0x7FFFFFFF), Status)

/*
 * Some UEFI firmwares have a *BROKEN* Unicode collation implementation
 * so we must provide our own version of StriCmp for ASCII comparison...
 */
static __inline CHAR16 _tolower(CONST CHAR16 c)
{
	if (('A' <= c) && (c <= 'Z'))
		return 'a' + (c - 'A');
	return c;
}

static __inline INTN _StriCmp(CONST CHAR16 * s1, CONST CHAR16 * s2)
{
	while ((*s1 != L'\0') && (_tolower(*s1) == _tolower(*s2)))
		s1++, s2++;
	return (INTN)(*s1 - *s2);
}

/*
 * Path function prototypes
 */
EFI_DEVICE_PATH* GetParentDevice(CONST EFI_DEVICE_PATH* DevicePath);
INTN CompareDevicePaths(CONST EFI_DEVICE_PATH* dp1, CONST EFI_DEVICE_PATH* dp2);
EFI_STATUS SetPathCase(CONST EFI_FILE_HANDLE Root, CHAR16* Path);
