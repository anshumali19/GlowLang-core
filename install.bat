@echo off
echo [GlowLang Installer] Fetching source and compiling optimized release for Windows...

if exist "%USERPROFILE%\.glowlang" (
    rmdir /S /Q "%USERPROFILE%\.glowlang"
)

mkdir "%USERPROFILE%\.glowlang"
cd /d "%USERPROFILE%\.glowlang"
git clone https://github.com/anshumali19/GlowLang.git .

echo [GlowLang Installer] Compiling GlowLang VM and GDU Package Manager...
cmake -B build -S .
cmake --build build --config Release

echo [GlowLang Installer] Installation complete!
echo Configuring PATH...

setx PATH "%PATH%;%USERPROFILE%\.glowlang\bin\Release"

echo Successfully added to PATH!
echo Please restart your terminal to start using `glowlang` and `gdu`.
