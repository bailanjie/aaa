@echo off
echo [1/3] Setting up VS environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: vcvars64.bat failed with code %errorlevel%
    exit /b 1
)

echo [2/3] Running CMake configure...
cmake -B build -G "NMake Makefiles" 2>&1
if %errorlevel% neq 0 (
    echo ERROR: cmake configure failed
    exit /b 1
)

echo [3/3] Building...
cmake --build build 2>&1
if %errorlevel% neq 0 (
    echo ERROR: build failed
    exit /b 1
)

echo.
echo ============ BUILD SUCCESS ============
echo Executable: build\ClipboardHistory.exe
