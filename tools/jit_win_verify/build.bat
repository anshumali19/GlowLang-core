@echo off
REM Standalone Windows JIT codegen verifier build (no OpenSSL/SQLite needed).
set SRC=..\..\src
set CC=clang-cl
if not exist build mkdir build
%CC% /c %SRC%\jit.c /I%SRC% /D_CRT_SECURE_NO_WARNINGS /Fobuild\jit.obj 2>&1
if errorlevel 1 exit /b 1
%CC% jit_verify.c build\jit.obj /I%SRC% /D_CRT_SECURE_NO_WARNINGS /Febuild\jit_verify.exe 2>&1
if errorlevel 1 exit /b 1
build\jit_verify.exe
REM Wrap the dumped bytes into COFF objects so llvm-objdump can disassemble them.
for %%b in (add method self) do (
    clang -c wrap_%%b.s -o wrap_%%b.o 2>&1
    echo --- wrap_%%b.o ---
    llvm-objdump -D -M intel wrap_%%b.o 2>&1
)
echo.
echo Artifacts: jit_dump_add.bin / jit_dump_method.bin / jit_dump_self.bin
