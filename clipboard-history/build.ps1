$ErrorActionPreference = "Stop"

Write-Host "[1/3] Setting up Visual Studio environment..." -ForegroundColor Cyan
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -DevCmdArguments "-arch=amd64" -SkipAutomaticLocation

$CMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

Write-Host "[2/3] Running CMake configure..." -ForegroundColor Cyan
& $CMake -B build -G "Visual Studio 17 2022" -A x64 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configure failed" -ForegroundColor Red
    pause
    exit 1
}

Write-Host "[3/3] Building..." -ForegroundColor Cyan
& $CMake --build build --config Release 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    pause
    exit 1
}

Write-Host ""
Write-Host "============ BUILD SUCCESS ============" -ForegroundColor Green
Write-Host "Executable: $PWD\build\Release\ClipboardHistory.exe" -ForegroundColor Green
pause
