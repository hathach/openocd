param(
    [string]$Compiler = 'C:\msys64\mingw64\bin\gcc.exe',
    [string]$OutputDirectory = "$PSScriptRoot\..\..\build-mingw64\wchlink-dll"
)
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$previousPath = $env:PATH
try {
    # GCC and its child processes need the MSYS2 compiler runtime on PATH.
    $env:PATH = (Split-Path -Parent $compilerPath) + ';' + $previousPath
    & $compilerPath -std=c11 -O2 -Wall -Wextra -Werror -shared -static-libgcc `
        "$PSScriptRoot\wchlink_dll.c" -o "$OutputDirectory\WCHLinkDll.dll" `
        '-Wl,--no-insert-timestamp' -lsetupapi
    if ($LASTEXITCODE -ne 0) { throw "WCHLinkDll build failed ($LASTEXITCODE)" }
    & $compilerPath -std=c11 -O2 -Wall -Wextra -Werror -static-libgcc `
        "$PSScriptRoot\smoke.c" -o "$OutputDirectory\wchlink-smoke.exe" `
        '-Wl,--no-insert-timestamp'
    if ($LASTEXITCODE -ne 0) { throw "Smoke test build failed ($LASTEXITCODE)" }
} finally {
    $env:PATH = $previousPath
}
Write-Output "Built $OutputDirectory\WCHLinkDll.dll"
