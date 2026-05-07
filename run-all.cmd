@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0run-all.ps1"
exit /b %ERRORLEVEL%
