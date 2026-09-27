/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native x64 implementation of the five WCHLinkDll entry points used by
 * src/jtag/drivers/wlinke.c. See README.md for the supported ABI and protocol.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <stddef.h>
#include <string.h>

#ifndef _WIN64
#error This DLL is intended for 64-bit Windows OpenOCD
#endif

#define DEVICE_COUNT 16
#define TRANSFER_MAX 4096
#define WCH_IOCTL 0x00223cdcUL
#define API __declspec(dllexport)

static const GUID wch_interface = {
	0xf8d5edca, 0xb647, 0x4e9c, {0x9b, 0xd3, 0xa5, 0xbd, 0x23, 0x28, 0xd5, 0x5c}
};
static HANDLE devices[DEVICE_COUNT];
static SRWLOCK device_lock = SRWLOCK_INIT;

/* The kernel protocol contains DWORDs and inline bytes, never user pointers. */
struct request {
	DWORD command;
	DWORD length;
	BYTE data[TRANSFER_MAX];
};
_Static_assert(offsetof(struct request, data) == 8, "WCH request layout");
_Static_assert(sizeof(ULONG) == 4, "WCH ABI requires 32-bit ULONG");

static BOOL valid_device(ULONG index)
{
	if (index >= DEVICE_COUNT || !devices[index]) {
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	return TRUE;
}

/* OpenOCD declares this as int and only tests for -1. Return an index token;
 * retain the full-width Windows HANDLE internally to avoid x64 truncation.
 */
API int WINAPI CH375OpenDevice(ULONG index)
{
	int result = -1;
	DWORD error = ERROR_SUCCESS;
	SP_DEVICE_INTERFACE_DATA iface = {0};
	SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = NULL;
	DWORD required = 0;
	HDEVINFO info;
	HANDLE handle;

	if (index >= DEVICE_COUNT) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}
	AcquireSRWLockExclusive(&device_lock);
	if (devices[index]) {
		result = (int)index;
		goto done;
	}
	info = SetupDiGetClassDevsW(&wch_interface, NULL, NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (info == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		goto done;
	}
	iface.cbSize = sizeof(iface);
	if (!SetupDiEnumDeviceInterfaces(info, NULL, &wch_interface, index, &iface)) {
		error = GetLastError();
		goto cleanup;
	}
	SetupDiGetDeviceInterfaceDetailW(info, &iface, NULL, 0, &required, NULL);
	if (!required) {
		error = GetLastError();
		goto cleanup;
	}
	detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required);
	if (!detail) {
		error = ERROR_NOT_ENOUGH_MEMORY;
		goto cleanup;
	}
	detail->cbSize = sizeof(*detail);
	if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, NULL, NULL)) {
		error = GetLastError();
		goto cleanup;
	}
	handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		goto cleanup;
	}
	devices[index] = handle;
	result = (int)index;
cleanup:
	if (detail)
		HeapFree(GetProcessHeap(), 0, detail);
	SetupDiDestroyDeviceInfoList(info);
done:
	ReleaseSRWLockExclusive(&device_lock);
	SetLastError(error);
	return result;
}

API void WINAPI CH375CloseDevice(ULONG index)
{
	AcquireSRWLockExclusive(&device_lock);
	if (valid_device(index)) {
		CloseHandle(devices[index]);
		devices[index] = NULL;
	}
	ReleaseSRWLockExclusive(&device_lock);
}

API ULONG WINAPI CH375SetTimeout(ULONG index, ULONG write_ms, ULONG read_ms)
{
	/* The vendor uses a 72-byte buffer with an 8-byte timeout payload. */
	DWORD request[18] = {0x0f, 8, write_ms, read_ms};
	DWORD returned = 0;
	BOOL ok = FALSE;
	DWORD error;
	AcquireSRWLockExclusive(&device_lock);
	if (valid_device(index))
		ok = DeviceIoControl(devices[index], WCH_IOCTL, request, sizeof(request),
			request, sizeof(request), &returned, NULL);
	error = ok ? ERROR_SUCCESS : GetLastError();
	ReleaseSRWLockExclusive(&device_lock);
	SetLastError(error);
	return ok ? 1 : 0;
}

static ULONG transfer(ULONG index, ULONG pipe, void *buffer, ULONG *length, BOOL read)
{
	struct request request = {0};
	DWORD requested, size, returned = 0, error;
	BOOL ok = FALSE;
	if (!length) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	requested = *length;
	*length = 0;
	if (!buffer || pipe < 1 || pipe > 8) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	if (requested > TRANSFER_MAX)
		requested = TRANSFER_MAX;
	request.command = (read ? 0x10000 : 0x20000) | (pipe - 1);
	request.length = read && requested < 64 ? 64 : requested;
	size = 8 + request.length;
	if (!read)
		memcpy(request.data, buffer, requested);
	AcquireSRWLockExclusive(&device_lock);
	if (valid_device(index))
		ok = DeviceIoControl(devices[index], WCH_IOCTL, &request, read ? 8 : size,
			&request, read ? size : 8, &returned, NULL);
	error = ok ? ERROR_SUCCESS : GetLastError();
	if (ok) {
		if (returned < 8 || request.length > size - 8 ||
			(read && request.length > returned - 8)) {
			ok = FALSE;
			error = ERROR_INVALID_DATA;
		} else {
			*length = request.length < requested ? request.length : requested;
			if (read)
				memcpy(buffer, request.data, *length);
		}
	}
	ReleaseSRWLockExclusive(&device_lock);
	SetLastError(error);
	return ok ? 1 : 0;
}

API ULONG WINAPI CH375ReadEndP(ULONG index, ULONG pipe, void *buffer, ULONG *length)
{
	return transfer(index, pipe, buffer, length, TRUE);
}

API ULONG WINAPI CH375WriteEndP(ULONG index, ULONG pipe, void *buffer, ULONG *length)
{
	return transfer(index, pipe, buffer, length, FALSE);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	(void)instance;
	if (reason == DLL_PROCESS_DETACH && !reserved) {
		for (unsigned int i = 0; i < DEVICE_COUNT; ++i) {
			if (devices[i])
				CloseHandle(devices[i]);
		}
	}
	return TRUE;
}
