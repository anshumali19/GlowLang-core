@echo off
rem Builds and signs bin\glowlang.exe. All the logic (Visual Studio environment,
rem flex/bison regeneration, static OpenSSL/SQLite, signing) lives in
rem scripts\build-windows.ps1; pass its options through, e.g. build.bat -NoSign
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" %*
exit /b %errorlevel%
