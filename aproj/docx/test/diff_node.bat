@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "NODE_DIFF=%SCRIPT_DIR%..\output\node_diff_debug.exe"
set "REF=%SCRIPT_DIR%lo_nodes.txt"
set "TEST=%SCRIPT_DIR%aproj_nodes.txt"

if not exist "%NODE_DIFF%" (
    echo [ERROR] node_diff_debug.exe not found: %NODE_DIFF%
    echo         Please build first: build.bat
    exit /b 1
)
if not exist "%REF%" (
    echo [ERROR] Reference file not found: %REF%
    exit /b 1
)
if not exist "%TEST%" (
    echo [ERROR] Test file not found: %TEST%
    exit /b 1
)

echo ========================================
echo   Nodes Structure Comparison
echo ========================================
echo Reference: %REF%
echo Test:      %TEST%
echo.

"%NODE_DIFF%" "%REF%" "%TEST%" %*

exit /b %ERRORLEVEL%
