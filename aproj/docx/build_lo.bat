@echo off
REM Build LibreOffice module
REM Usage: build_lo.bat [module]
REM Example: build_lo.bat sw    (build Writer module)
REM          build_lo.bat        (full build)

setlocal enabledelayedexpansion

set "START_TIME=%time%"
for /f "tokens=1-4 delims=:." %%a in ("%START_TIME%") do (
    set "START_TIMESTAMP=%%a%%b%%c%%d"
)

set "SCRIPT_DIR=%~dp0"
for %%i in ("%SCRIPT_DIR%..\..") do set "LIBO_ROOT=%%~fi"
REM Find bash.exe from git installation directory (via where git)
set "GIT_BASH="
for /f "delims=" %%g in ('where git.exe 2^>nul') do (
    if not defined GIT_BASH (
        for %%r in ("%%~dpg..") do set "GIT_ROOT=%%~fr"
        if exist "!GIT_ROOT!\bin\bash.exe" set "GIT_BASH=!GIT_ROOT!\bin\bash.exe"
    )
)
if not defined GIT_BASH (
    echo [ERROR] bash.exe not found under git installation directory
    exit /b 1
)
echo [INFO] Using bash: %GIT_BASH%

if not exist "%LIBO_ROOT%\aproj\build" mkdir "%LIBO_ROOT%\aproj\build"
set "BUILD_LOG=%LIBO_ROOT%\aproj\build\build_%date:~0,4%_%date:~5,2%_%date:~8,2%.log"
set "TARGET_EXE=%LIBO_ROOT%\instdir\program\soffice.exe"

if exist "%TARGET_EXE%" (
    for %%f in ("%TARGET_EXE%") do set "OLD_TIMESTAMP=%%~tf"
    echo [INFO] Existing target: %TARGET_EXE%
    echo [INFO] Last modified: !OLD_TIMESTAMP!
) else (
    set "OLD_TIMESTAMP="
    echo [INFO] No existing target found
)

echo [INFO] Killing existing soffice processes...
taskkill /f /im soffice.exe /im soffice.bin 2>nul
taskkill /f /im swriter.exe /im scalc.exe /im simpress.exe /im sdraw.exe /im smath.exe /im sbase.exe 2>nul

if "%~1"=="" (
    set "MAKE_CMD=make"
    echo [INFO] Full build...
) else (
    set "MAKE_CMD=make %1"
    echo [INFO] Building module: %1
)

echo [INFO] LIBO_ROOT=%LIBO_ROOT%
echo [INFO] Command: %MAKE_CMD%
echo [INFO] Build start time: %START_TIME%
echo [INFO] Log file: %BUILD_LOG%

REM Convert Windows paths to Unix format for bash (e.g., E:\path -> E:/path)
set "LIBO_ROOT_UNIX=%LIBO_ROOT:\=/%"
set "BUILD_LOG_UNIX=%BUILD_LOG:\=/%"

"%GIT_BASH%" -c "cd '%LIBO_ROOT_UNIX%' && %MAKE_CMD% 2>&1 | tee '%BUILD_LOG_UNIX%'"

set "EXIT_CODE=%errorlevel%"
echo [INFO] Make exit code: %EXIT_CODE%

set "BUILD_COMPLETED=0"
set "TARGET_UPDATED=0"

if exist "%BUILD_LOG%" (
    findstr /i "Built target" "%BUILD_LOG%" >nul && set "BUILD_COMPLETED=1"
    findstr /i "100%.*Built" "%BUILD_LOG%" >nul && set "BUILD_COMPLETED=1"
    findstr /i "\[3/3\]" "%BUILD_LOG%" >nul && set "BUILD_COMPLETED=1"
    findstr /i "Build succeeded" "%BUILD_LOG%" >nul && set "BUILD_COMPLETED=1"
)

if exist "%TARGET_EXE%" (
    echo [INFO] Target exists: %TARGET_EXE%
    for %%f in ("%TARGET_EXE%") do set "NEW_TIMESTAMP=%%~tf"
    echo [INFO] New modified time: !NEW_TIMESTAMP!
    
    if defined OLD_TIMESTAMP (
        for /f "tokens=1-4 delims=/: " %%a in ("!OLD_TIMESTAMP!") do set "OLD_T=%%a%%b%%c%%d"
        for /f "tokens=1-4 delims=/: " %%a in ("!NEW_TIMESTAMP!") do set "NEW_T=%%a%%b%%c%%d"
        if !NEW_T! gtr !OLD_T! (
            set "TARGET_UPDATED=1"
            echo [INFO] Target timestamp updated
        ) else (
            echo [WARNING] Target timestamp NOT updated - may be using old build
        )
    ) else (
        set "TARGET_UPDATED=1"
        echo [INFO] New target created
    )
) else (
    echo [ERROR] Target not found: %TARGET_EXE%
)

echo [INFO] Build completed flag: !BUILD_COMPLETED!
echo [INFO] Target updated flag: !TARGET_UPDATED!

if !BUILD_COMPLETED! equ 1 if !TARGET_UPDATED! equ 1 (
    echo [OK] Build completed successfully
    exit /b 0
) else (
    if !EXIT_CODE! neq 0 (
        echo [ERROR] Build failed with exit code: !EXIT_CODE!
    ) else (
        echo [ERROR] Build incomplete - missing completion markers or target not updated
        if !BUILD_COMPLETED! equ 0 (
            echo [ERROR] - Build completion marker not found in log
        )
        if !TARGET_UPDATED! equ 0 (
            echo [ERROR] - Target file not updated or not found
        )
    )
    exit /b 1
)