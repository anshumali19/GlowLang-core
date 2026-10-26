@echo off
rem Kept for existing scripts and docs: runs the test suite, comparing every
rem test's output with tests\expected\ (see scripts\run-tests.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-tests.ps1" %*
exit /b %errorlevel%
setlocal enabledelayedexpansion

set "BIN=.\bin\glowlang.exe"
set PASSED=0
set FAILED=0
set TOTAL=0

for %%f in ("tests\*.glow") do (
    set /a TOTAL+=1
    echo Running %%~nxf...
    "%BIN%" run "%%f" > "%TEMP%\glow_test_out.log" 2> "%TEMP%\glow_test_err.log"
    set RET=!errorlevel!
    set EXPECTED=0
    set "NAME=%%~nxf"
    if /i "!NAME!"=="test_std.glow" set EXPECTED=42
    if /i "!NAME!"=="test_div0_stack.glow" set EXPECTED=1
    if /i "!NAME!"=="test_error_stack.glow" set EXPECTED=1
    if /i "!NAME!"=="test_jit_div0.glow" set EXPECTED=1
    if /i "!NAME!"=="test_throw_stack.glow" set EXPECTED=1
    if /i "!NAME!"=="loop_rec_test.glow" set EXPECTED=1
    if /i "!NAME!"=="test_import.glow" set EXPECTED=1

    if !RET!==!EXPECTED! (
        echo   PASS
        set /a PASSED+=1
    ) else (
        echo   FAIL ^(Code: !RET!, Expected: !EXPECTED!^)
        if exist "%TEMP%\glow_test_err.log" type "%TEMP%\glow_test_err.log"
        if exist "%TEMP%\glow_test_out.log" type "%TEMP%\glow_test_out.log"
        set /a FAILED+=1
    )
)

echo -----------------------
echo Total: !TOTAL!
echo Passed: !PASSED!
echo Failed: !FAILED!

if !FAILED! GTR 0 (
    exit /b 1
) else (
    exit /b 0
)
