@echo off
rem Compatibility entry point: always builds the developer copy.
call "%~dp0build-dev.cmd"
exit /b %errorlevel%
