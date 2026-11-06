@echo off
setlocal
set "BUILD_MODE=%~1"
if not "%BUILD_MODE%"=="dev" if not "%BUILD_MODE%"=="release" exit /b 1
set VSLANG=1033
cd /d "%~dp0.."
set "APP_VERSION="
for /f "delims=" %%V in ('powershell -NoProfile -ExecutionPolicy Bypass -File packaging\read-version.ps1') do set "APP_VERSION=%%V"
if not defined APP_VERSION exit /b 1
set "BUILD_DIR=%CD%\build\%BUILD_MODE%"
set "STAGE_DIR=%CD%\build\package-%BUILD_MODE%"
if not defined VCVARS set "VCVARS=F:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%"
if errorlevel 1 exit /b 1
if not defined QT_ROOT set "QT_ROOT=%CD%\.tools\Qt\6.8.3\msvc2022_64"
set "PATH=F:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\deploy.ps1 -Mode "%BUILD_MODE%" -Prepare
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\make-icon.ps1
if errorlevel 1 exit /b 1
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%"
if errorlevel 1 exit /b 1
rem Clean rebuild avoids stale header dependencies on this toolchain.
cmake --build "%BUILD_DIR%" --target WhatInput --clean-first --parallel
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\stage.ps1 -Mode "%BUILD_MODE%" -QtRoot "%QT_ROOT%" -RedistRoot "%VCToolsRedistDir%."
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\deploy.ps1 -Mode "%BUILD_MODE%"
if errorlevel 1 exit /b 1
echo Build complete: %BUILD_MODE% %APP_VERSION%
exit /b 0
