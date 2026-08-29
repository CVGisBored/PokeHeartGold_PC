$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Build = Join-Path $Root 'build-windows'
$Bin = Join-Path $Root 'bin'
$Compiler = Get-Command x86_64-w64-mingw32-clang++.exe -ErrorAction SilentlyContinue
if (-not $Compiler) { $Compiler = Get-Command x86_64-w64-mingw32-g++.exe -ErrorAction SilentlyContinue }
if (-not $Compiler) {
    Write-Host 'Windows compiler not found.' -ForegroundColor Yellow
    Write-Host 'Install LLVM-MinGW (UCRT) or MinGW-w64, then rerun this script.'
    Write-Host 'LLVM-MinGW package name: MartinStorsjo.LLVM-MinGW.UCRT'
    exit 2
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    Write-Host 'CMake is required. Install it with: winget install Kitware.CMake' -ForegroundColor Yellow
    exit 3
}
Remove-Item -Recurse -Force $Build -ErrorAction SilentlyContinue
cmake -S $Root -B $Build -DCMAKE_TOOLCHAIN_FILE="$Root/cmake/toolchains/mingw64.cmake" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build $Build --config Release
New-Item -ItemType Directory -Force $Bin | Out-Null
Copy-Item "$Build/heartgold_native.exe" "$Bin/HeartGoldNative-v0.37-Windows-x86_64.exe" -Force
Copy-Item "$Build/heartgold_native.exe" "$Root/HeartGoldNative-v0.37-Windows-x86_64.exe" -Force
Write-Host "Built $Root/HeartGoldNative-v0.37-Windows-x86_64.exe" -ForegroundColor Green
Write-Host "Mirror: $Bin/HeartGoldNative-v0.37-Windows-x86_64.exe" -ForegroundColor Green
