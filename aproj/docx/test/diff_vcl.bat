@echo off
REM diff_vcl.bat - Compare VCL layer rendering differences
REM Usage: diff_vcl.bat

set "SCRIPT_DIR=%~dp0"
set "RENDER_DIFF=%SCRIPT_DIR%..\build\Debug\render_diff.exe"
set "REF=%SCRIPT_DIR%lo_vcl.txt"
set "TEST=%SCRIPT_DIR%aproj_vcl.txt"

if not exist "%RENDER_DIFF%" (
    echo [ERROR] render_diff.exe not found: %RENDER_DIFF%
    echo         Please build first: cmake --build build
    exit /b 1
)
if not exist "%REF%" (
    echo [ERROR] Reference file not found: %REF%
    echo         Please run: python test/gen_lo.py
    exit /b 1
)
if not exist "%TEST%" (
    echo [ERROR] Test file not found: %TEST%
    echo         Please run: python test/gen_aproj.py
    exit /b 1
)

echo ========================================
echo   VCL Layer Comparison
echo ========================================
echo Reference: %REF%
echo Test:      %TEST%
echo.

"%RENDER_DIFF%" "%REF%" "%TEST%"

exit /b %ERRORLEVEL%