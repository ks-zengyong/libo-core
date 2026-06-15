@echo off
REM diff_frame.bat - Compare Frame layer rendering differences
REM Usage: diff_frame.bat
REM 使用 output 下的 render_diff_debug.exe 进行差异对比

set "SCRIPT_DIR=%~dp0"
set "RENDER_DIFF=%SCRIPT_DIR%..\output\render_diff_debug.exe"
set "REF=%SCRIPT_DIR%lo_frame.txt"
set "TEST=%SCRIPT_DIR%aproj_frame.txt"

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
echo   Frame Layer Comparison
echo ========================================
echo Reference: %REF%
echo Test:      %TEST%
echo.

"%RENDER_DIFF%" "%REF%" "%TEST%"

exit /b %ERRORLEVEL%