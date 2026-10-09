@echo off
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
