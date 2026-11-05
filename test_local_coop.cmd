@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0agent\test_local_coop.ps1" -KillExisting %*
endlocal
