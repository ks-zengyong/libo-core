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
echo [1/3] Configuring...
cmake -B build -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configure failed
    exit /b 1
)

:: -- MSBuild Compile --------------------------
echo [2/3] Building...
cmake --build build --config %BUILD_CONFIG%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)

:: -- Copy to output ----------------------------
echo [3/3] Copying artifacts...
if not exist output mkdir output
if /I "%BUILD_CONFIG%"=="Debug" (
    copy /Y build\%BUILD_CONFIG%\docx_e2e_test_debug.exe output\ >nul
    copy /Y build\%BUILD_CONFIG%\render_diff_debug.exe   output\ >nul
    copy /Y build\%BUILD_CONFIG%\node_diff_debug.exe     output\ >nul
    echo   docx_e2e_test_debug.exe
    echo   render_diff_debug.exe
    echo   node_diff_debug.exe
) else (
    copy /Y build\%BUILD_CONFIG%\docx_e2e_test.exe output\ >nul
    copy /Y build\%BUILD_CONFIG%\render_diff.exe   output\ >nul
    copy /Y build\%BUILD_CONFIG%\node_diff.exe     output\ >nul
    echo   docx_e2e_test.exe
    echo   render_diff.exe
    echo   node_diff.exe
)

echo.
echo ========================================
echo   Build completed: output\
echo ========================================