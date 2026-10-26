@echo off
rem Kept for existing scripts and docs: runs the test suite, comparing every
rem test's output with tests\expected\ (see scripts\run-tests.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-tests.ps1" %*
exit /b %errorlevel%
