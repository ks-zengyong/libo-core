@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "FRAME_DIFF=%SCRIPT_DIR%..\output\frame_diff_debug.exe"
set "REF=%SCRIPT_DIR%lo_frame.txt"
set "TEST=%SCRIPT_DIR%aproj_frame.txt"

if not exist "%FRAME_DIFF%" (
    echo [ERROR] frame_diff_debug.exe not found: %FRAME_DIFF%
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
echo   Frame Layer Comparison
echo ========================================
echo Reference: %REF%
echo Test:      %TEST%
echo.

"%FRAME_DIFF%" "%REF%" "%TEST%" %*

exit /b %ERRORLEVEL%
