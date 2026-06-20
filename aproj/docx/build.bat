@echo off
setlocal

:: -- Parse config ---------------------------------
set "BUILD_CONFIG=Debug"
if not "%~1"=="" (
    if /I "%~1"=="Debug"   set "BUILD_CONFIG=Debug"
    if /I "%~1"=="Release" set "BUILD_CONFIG=Release"
    if /I not "%~1"=="Debug" if /I not "%~1"=="Release" (
        echo ERROR: Invalid config "%~1". Use Debug or Release.
        echo Usage: build.bat [Debug^|Release]
        exit /b 1
    )
)

echo ========================================
echo   aproj/docx Build [%BUILD_CONFIG%]
echo ========================================

:: -- CMake Configure --------------------------
echo [1/2] Configuring...
cmake -B build -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configure failed
    exit /b 1
)

:: -- MSBuild Compile --------------------------
echo [2/2] Building (artifacts go to output\)...
cmake --build build --config %BUILD_CONFIG%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)

echo.
echo ========================================
echo   Build completed: output\
echo ========================================
