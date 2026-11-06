@echo off
setlocal
rem USER ONLY. Agents must never execute this script or the release mode below.
echo This creates a clean public distribution. Run this script personally.
call "%~dp0build-common.cmd" release
exit /b %errorlevel%
