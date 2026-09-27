# x64 WCHLinkDll for OpenOCD

Native Windows x64 compatibility DLL for the `wlinke` adapter in this repository.
It talks directly to the installed WCHLink kernel driver through SetupAPI and
DeviceIoControl. It needs neither the 32-bit vendor DLL nor a helper process,
and does not require replacing the WCH driver with WinUSB.

This is an independent implementation of the five exports used by OpenOCD,
not a rebuild of WCH's proprietary DLL or a replacement for its entire API:

- `CH375OpenDevice`
- `CH375CloseDevice`
- `CH375SetTimeout`
- `CH375ReadEndP`
- `CH375WriteEndP`

The signatures match `src/jtag/drivers/wlinke.c`. In particular, OpenOCD
declares `CH375OpenDevice` as returning `int` and only checks for `-1`.
This implementation returns the device index on success and keeps the native
64-bit handle internally. Callers must use device indices 0 through 15;
passing raw handles or the vendor's special auto-select index is unsupported.
Endpoint numbers are 1 through 8. Like the vendor implementation, a transfer
is capped at 4096 bytes and reads request at least one 64-byte USB packet.
The caller's length is updated with the number of bytes copied/transferred.

## Build and install

From the repository root in PowerShell, using MSYS2 MinGW-w64 GCC:

```powershell
.\contrib\wchlink-dll\build.ps1
Copy-Item .\build-mingw64\wchlink-dll\WCHLinkDll.dll .\build-mingw64\source\src\WCHLinkDll.dll
```

The script defaults to `C:\msys64\mingw64\bin\gcc.exe`. Use `-Compiler` and
`-OutputDirectory` to override those locations. It temporarily adds the compiler
directory to PATH and restores PATH afterward. Compilation rejects 32-bit targets.
For another x64 OpenOCD installation, copy the DLL beside its `openocd.exe`.

## Verification

```powershell
# Loading, required exports, and invalid-argument/closed-device handling:
.\build-mingw64\wchlink-dll\wchlink-smoke.exe "$PWD\build-mingw64\wchlink-dll\WCHLinkDll.dll"

# Also open probe 0, set timeouts, query firmware version, close and reopen:
.\build-mingw64\wchlink-dll\wchlink-smoke.exe "$PWD\build-mingw64\wchlink-dll\WCHLinkDll.dll" --probe

# Initialize and examine the attached target, then exit (does not program flash):
.\build-mingw64\source\src\openocd.exe -s .\tcl -f target/wch-riscv.cfg -c 'init; shutdown'
```

Verified on 2026-09-27 with the installed WCH driver package 1.2.2022.08,
a WCH-LinkE in RISC-V mode, firmware 2.18, and this repository's x64 OpenOCD
0.12.0+dev build. The version query returned `82 0d 04 02 12 12 00`.
OpenOCD reported `wlink_init ok`, examined the RV32 core successfully, and
exited with status 0. Flash programming and other probe/driver versions have
not been tested.

## Driver protocol notes

The interface below was checked against the locally installed
`C:\WCH.CN\WCHLinkDrv\WCHLinkDll.dll` (32-bit, dated 2022-08-23).
No vendor binary is included in this directory.

- Device interface GUID: `{F8D5EDCA-B647-4E9C-9BD3-A5BD2328D55C}`.
- Synchronous, buffered IOCTL: `0x00223CDC`.
- Request header: 32-bit command and 32-bit length, followed by inline data.
  This layout is identical in x86 and x64 callers.
- Read command: `0x10000 | (endpoint - 1)`, 8-byte input header,
  header plus requested data as output.
- Write command: `0x20000 | (endpoint - 1)`, header plus data as input,
  8-byte output header.
- Timeout command: `0x0F`, payload length 8, write and read timeout DWORDs
  in milliseconds. The input/output buffer is 72 bytes, matching the vendor.

The implementation validates returned lengths and serializes operations to
prevent closing a handle while a transfer is using it. Its dependencies are
standard Windows system DLLs; it does not use libusb or a MinGW runtime DLL.
