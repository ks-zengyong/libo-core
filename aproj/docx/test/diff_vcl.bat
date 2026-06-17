@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "RENDER_DIFF=%SCRIPT_DIR%..\output\render_diff_debug.exe"
set "REF=%SCRIPT_DIR%lo_vcl.txt"
set "TEST=%SCRIPT_DIR%aproj_vcl.txt"

if not exist "%RENDER_DIFF%" (
    echo [ERROR] render_diff_debug.exe not found: %RENDER_DIFF%
    echo         Please build first: build.bat
    exit /b 1
)
if not exist "%REF%" (
    echo [ERROR] Reference file not found: %REF%
    echo         Please run: python test\gen_lo.py
    exit /b 1
)
if not exist "%TEST%" (
    echo [ERROR] Test file not found: %TEST%
    echo         Please run: python test\gen_aproj.py
    exit /b 1
)

echo ========================================
echo   VCL Layer Comparison
echo ========================================
echo Reference: %REF%
echo Test:      %TEST%
echo.

"%RENDER_DIFF%" "%REF%" "%TEST%" %*

exit /b %ERRORLEVEL%
