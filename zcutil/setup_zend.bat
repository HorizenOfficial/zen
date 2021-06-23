@echo off
set COMPLUS_version=v4.0.30319
PowerShell.exe -ExecutionPolicy Bypass -File .\fetch-params.ps1
EXIT /b %ERRORLEVEL%
