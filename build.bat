@echo off
rem Builds and signs bin\glowlang.exe. All the logic (Visual Studio environment,
rem flex/bison regeneration, static OpenSSL/SQLite, signing) lives in
rem scripts\build-windows.ps1; pass its options through, e.g. build.bat -NoSign
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" %*
exit /b %errorlevel%
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /O2 /MT /wd4267 /wd4244 /wd4133 /wd4013 /wd4102 /D_CRT_SECURE_NO_WARNINGS /Isrc /IC:\Users\anshu\Desktop\vcpkg\installed\x64-windows-static\include src\glowlang.tab.c src\lex.yy.c src\lsp.c src\arena.c src\jit.c src\opt.c src\compiler.c src\vm.c src\gc.c src\stdlib.c src\bigint.c /Febin\glowlang.exe C:\Users\anshu\Desktop\vcpkg\installed\x64-windows-static\lib\libssl.lib C:\Users\anshu\Desktop\vcpkg\installed\x64-windows-static\lib\libcrypto.lib C:\Users\anshu\Desktop\vcpkg\installed\x64-windows-static\lib\sqlite3.lib ws2_32.lib crypt32.lib advapi32.lib user32.lib shell32.lib /link /STACK:536870912,4194304
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe" sign /a /fd SHA256 bin\glowlang.exe
