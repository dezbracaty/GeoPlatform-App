@echo off
REM RemoteSigned applies only to this process, allowing local project scripts.
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
