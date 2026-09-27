/* SPDX-License-Identifier: GPL-2.0-or-later */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *open_fn)(ULONG);
typedef void (WINAPI *close_fn)(ULONG);
typedef ULONG (WINAPI *timeout_fn)(ULONG, ULONG, ULONG);
typedef ULONG (WINAPI *transfer_fn)(ULONG, ULONG, void *, ULONG *);

/* memcpy avoids a compiler-specific cast between incompatible function types. */
#define RESOLVE(variable, symbol) do { \
	FARPROC address = GetProcAddress(dll, symbol); \
	if (!address) { fprintf(stderr, "Missing %s\n", symbol); return 1; } \
	_Static_assert(sizeof(variable) == sizeof(address), "function pointer size"); \
	memcpy(&variable, &address, sizeof(variable)); \
} while (0)
#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "Failed line %d: %s (Windows error %lu)\n", \
			__LINE__, #condition, GetLastError()); \
		goto cleanup; \
	} \
} while (0)

int main(int argc, char **argv)
{
	open_fn open_device;
	close_fn close_device;
	timeout_fn set_timeout;
	transfer_fn read_data, write_data;
	BYTE data[64] = {0x81, 0x0d, 0x01, 0x01};
	ULONG length;
	int result = 1;
	BOOL opened = FALSE;
	if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--probe"))) {
		fprintf(stderr, "Usage: wchlink-smoke.exe DLL_PATH [--probe]\n");
		return 1;
	}
	HMODULE dll = LoadLibraryA(argv[1]);
	if (!dll) {
		fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
		return 1;
	}
	RESOLVE(open_device, "CH375OpenDevice");
	RESOLVE(close_device, "CH375CloseDevice");
	RESOLVE(set_timeout, "CH375SetTimeout");
	RESOLVE(read_data, "CH375ReadEndP");
	RESOLVE(write_data, "CH375WriteEndP");
	CHECK(open_device(16) == -1);
	CHECK(set_timeout(0, 1000, 1000) == 0);
	length = sizeof(data);
	CHECK(read_data(0, 1, data, &length) == 0 && length == 0);
	CHECK(read_data(0, 1, data, NULL) == 0);
	length = sizeof(data);
	CHECK(write_data(0, 0, data, &length) == 0 && length == 0);
	length = sizeof(data);
	CHECK(write_data(0, 1, NULL, &length) == 0 && length == 0);
	puts("x64 DLL loaded; five exports and invalid-input checks passed.");
	if (argc == 3) {
		CHECK(open_device(0) != -1);
		opened = TRUE;
		CHECK(set_timeout(0, 2000, 2000) != 0);
		length = 4;
		CHECK(write_data(0, 1, data, &length) != 0 && length == 4);
		memset(data, 0, sizeof(data));
		length = 7;
		CHECK(read_data(0, 1, data, &length) != 0 && length == 7);
		CHECK(data[0] == 0x82 && data[1] == 0x0d);
		printf("Probe version response:");
		for (ULONG i = 0; i < length; ++i)
			printf(" %02x", data[i]);
		putchar('\n');
		close_device(0);
		opened = FALSE;
		CHECK(open_device(0) != -1);
		opened = TRUE;
		puts("Probe open, timeouts, endpoint I/O, close and reopen passed.");
	}
	result = 0;
cleanup:
	if (opened)
		close_device(0);
	FreeLibrary(dll);
	return result;
}
