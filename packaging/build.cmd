@echo off
setlocal
set VSLANG=1033
cd /d "%~dp0.."
set "RELEASE_DIR=%CD%\release"
if not defined VCVARS set "VCVARS=F:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%"
if errorlevel 1 exit /b 1
if not defined QT_ROOT set "QT_ROOT=%CD%\.tools\Qt\6.8.3\msvc2022_64"
set "PATH=F:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%"
if errorlevel 1 exit /b 1
cmake --build build/release --target WhatInput WhatInputStats --parallel
if errorlevel 1 exit /b 1
cmake --install build/release --prefix "%RELEASE_DIR%"
if errorlevel 1 exit /b 1
"%QT_ROOT%\bin\windeployqt.exe" --release --no-translations --dir "%RELEASE_DIR%" "%RELEASE_DIR%\WhatInputStats.exe"
if errorlevel 1 exit /b 1
echo Build complete. Executables and runtime dependencies: "%RELEASE_DIR%"
exit /b 0
