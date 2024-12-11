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

#ifdef __MAKEWITH_GNUEFI

#include <efi.h>
#include <efilib.h>
#include <efistdarg.h>
#include <libsmbios.h>

#else /* EDK2 */

#include <Base.h>
#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/BlockIo2.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DevicePathFromText.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/DiskIo.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/LoadedImage.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <Guid/SmBios.h>

#include <IndustryStandard/SmBios.h>

#endif /* __MAKEWITH_GNUEFI */

/* Maximum size to be used for paths */
#ifndef PATH_MAX
#define PATH_MAX            512
#endif

/* Maximum size for the File Info structure we query */
#define FILE_INFO_SIZE      (PATH_MAX * sizeof(CHAR16))

/* For safety, we set a maximum size that strings shall not outgrow */
#define STRING_MAX          (PATH_MAX + 2)

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

/* Maximum line size for our banner */
#define BANNER_LINE_SIZE     79

/*
 * Console colours we will be using
 */
#define TEXT_DEFAULT         EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK)
#define TEXT_REVERSED        EFI_TEXT_ATTR(EFI_BLACK, EFI_LIGHTGRAY)
#define TEXT_YELLOW          EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK)
#define TEXT_RED             EFI_TEXT_ATTR(EFI_LIGHTRED, EFI_BLACK)
#define TEXT_GREEN           EFI_TEXT_ATTR(EFI_LIGHTGREEN, EFI_BLACK)
#define TEXT_WHITE           EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK)

/*
 * Set and restore the console text colour
 */
#define SetText(attr)        gST->ConOut->SetAttribute(gST->ConOut, (attr))
#define DefText()            gST->ConOut->SetAttribute(gST->ConOut, TEXT_DEFAULT)

/*
 * Convenience macros to print informational, warning or error messages.
 */
#define PrintInfo(fmt, ...)  do { SetText(TEXT_WHITE); Print(L"[INFO]"); DefText(); \
                                     Print(L" " fmt L"\n", ##__VA_ARGS__); } while(0)
#define PrintWarning(fmt, ...)  do { SetText(TEXT_YELLOW); Print(L"[WARN]"); DefText(); \
                                     Print(L" " fmt L"\n", ##__VA_ARGS__); } while(0)
#define PrintError(fmt, ...)    do { SetText(TEXT_RED); Print(L"[FAIL]"); DefText(); \
                                     Print(L" " fmt L": [%d] %r\n", ##__VA_ARGS__, (Status&0x7FFFFFFF), Status); } while (0)

/* Convenience assertion macro */
#define P_ASSERT(f, l, a)   if(!(a)) do { Print(L"*** ASSERT FAILED: %a(%d): %a ***\n", f, l, #a); while(1); } while(0)
#define V_ASSERT(a)         P_ASSERT(__FILE__, __LINE__, a)

/*
 * Secure string length, that asserts if the string is NULL or if
 * the length is larger than a predetermined value (STRING_MAX)
 */
static __inline UINTN _SafeStrLen(CONST CHAR16* String, CONST CHAR8* File, CONST UINTN Line) {
	UINTN Len = 0;
	P_ASSERT(File, Line, String != NULL);
	Len = StrLen(String);
	P_ASSERT(File, Line, Len < STRING_MAX);
	return Len;
}

#define SafeStrLen(s) _SafeStrLen(s, __FILE__, __LINE__)

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

static __inline INTN _StriCmp(CONST CHAR16* s1, CONST CHAR16* s2)
{
	/* NB: SafeStrLen() will already have asserted if these condition are met */
	if ((SafeStrLen(s1) >= STRING_MAX) || (SafeStrLen(s2) >= STRING_MAX))
		return -1;
	while ((*s1 != L'\0') && (_tolower(*s1) == _tolower(*s2)))
		s1++, s2++;
	return (INTN)(*s1 - *s2);
}

/*
 * Secure string copy, that either uses the already secure version from
 * EDK2, or duplicates it for gnu-efi and asserts on any error.
 */
static __inline VOID _SafeStrCpy(CHAR16* Destination, UINTN DestMax,
	CONST CHAR16* Source, CONST CHAR8* File, CONST UINTN Line) {
#ifdef _GNU_EFI
	P_ASSERT(File, Line, Destination != NULL);
	P_ASSERT(File, Line, Source != NULL);
	P_ASSERT(File, Line, DestMax != 0);
	/*
	 * EDK2 would use RSIZE_MAX, but we use the smaller PATH_MAX for
	 * gnu-efi as it can help detect path overflows while debugging.
	 */
	P_ASSERT(File, Line, DestMax <= PATH_MAX);
	P_ASSERT(File, Line, DestMax > StrLen(Source));
	while (*Source != 0)
		*(Destination++) = *(Source++);
	*Destination = 0;
#else
	P_ASSERT(File, Line, StrCpyS(Destination, DestMax, Source) == 0);
#endif
}

#define SafeStrCpy(d, l, s) _SafeStrCpy(d, l, s, __FILE__, __LINE__)

/*
 * Function prototypes
 */
EFI_DEVICE_PATH* GetParentDevice(CONST EFI_DEVICE_PATH* DevicePath);
INTN CompareDevicePaths(CONST EFI_DEVICE_PATH* dp1, CONST EFI_DEVICE_PATH* dp2);
EFI_STATUS SetPathCase(CONST EFI_FILE_HANDLE Root, CHAR16* Path);
CHAR16* DevicePathToString(CONST EFI_DEVICE_PATH* DevicePath);
EFI_STATUS PrintSystemInfo(VOID);
INTN GetSecureBootStatus(VOID);
