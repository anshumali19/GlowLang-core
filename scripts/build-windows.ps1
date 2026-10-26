<#
.SYNOPSIS
  Build GlowLang for Windows x64 (and sign it).

.DESCRIPTION
  1. Loads the MSVC build environment (headers, CRT, Windows SDK libs).
  2. Regenerates the lexer/parser with win_flex / win_bison when the .l/.y
     sources are newer than the generated C files.
  3. Compiles with MSVC (cl.exe) by default. -Compiler clang uses clang-cl,
     which supports computed goto, so the VM dispatches with threaded code
     instead of a switch. MSVC is the default because it is the build the
     tests and benchmarks were run on, and because Windows Smart App Control
     (when enabled) has blocked clang-cl builds of this project. OpenSSL and
     SQLite are linked statically from vcpkg (x64-windows-static), so the
     executable needs no DLLs. A .pdb is written next to the executable so
     crash reports can name functions.
  4. Signs the result with scripts\sign-windows.ps1 (skip with -NoSign).

.EXAMPLE
  .\scripts\build-windows.ps1
  .\scripts\build-windows.ps1 -Compiler clang -NoSign
#>
param(
    [ValidateSet('clang', 'msvc')] [string] $Compiler = 'msvc',
    [string] $Output = 'bin\glowlang.exe',
    [string] $VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\Users\anshu\Desktop\vcpkg' }),
    [switch] $NoSign,
    [switch] $DebugBuild,
    [switch] $Asan
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Import-VsEnvironment {
    if ($env:VCToolsInstallDir -and (Get-Command link.exe -ErrorAction SilentlyContinue)) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vsPath) {
        $vsPath = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio'), (Join-Path $env:ProgramFiles 'Microsoft Visual Studio') -Directory -ErrorAction SilentlyContinue |
            Get-ChildItem -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'VC\Auxiliary\Build\vcvarsall.bat') } |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $vsPath) { throw 'Visual Studio C++ build tools not found.' }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
    $lines = & cmd.exe /c "`"$vcvars`" x64 >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

function Update-Generated([string]$src, [string]$out, [scriptblock]$gen) {
    if (-not (Test-Path $out) -or (Get-Item $src).LastWriteTime -gt (Get-Item $out).LastWriteTime) {
        & $gen
        if ($LASTEXITCODE -ne 0) { throw "Generating $out from $src failed" }
        Write-Host "Regenerated $out"
    }
}

Import-VsEnvironment

$flex = (Get-Command win_flex, flex -ErrorAction SilentlyContinue | Select-Object -First 1)
$bison = (Get-Command win_bison, bison -ErrorAction SilentlyContinue | Select-Object -First 1)
if ($flex) { Update-Generated 'src\glowlang.l' 'src\lex.yy.c' { & $flex.Source --nounistd -o src\lex.yy.c src\glowlang.l } }
elseif (-not (Test-Path 'src\lex.yy.c')) { throw 'win_flex not found and src\lex.yy.c is missing (winget install WinFlexBison.win_flex_bison).' }
if ($bison) { Update-Generated 'src\glowlang.y' 'src\glowlang.tab.c' { & $bison.Source -Wno-conflicts-sr -d -o src\glowlang.tab.c src\glowlang.y } }
elseif (-not (Test-Path 'src\glowlang.tab.c')) { throw 'win_bison not found and src\glowlang.tab.c is missing.' }

$deps = Join-Path $VcpkgRoot 'installed\x64-windows-static'
if (-not (Test-Path (Join-Path $deps 'include\openssl\ssl.h'))) {
    throw "Static OpenSSL/SQLite not found under $deps. Run: vcpkg install openssl:x64-windows-static sqlite3:x64-windows-static"
}

$sources = 'glowlang.tab.c', 'lex.yy.c', 'lsp.c', 'arena.c', 'jit.c', 'opt.c', 'compiler.c',
           'vm.c', 'gc.c', 'stdlib.c', 'bigint.c', 'regex.c' | ForEach-Object { "src\$_" }
$libs = "$deps\lib\libssl.lib", "$deps\lib\libcrypto.lib", "$deps\lib\sqlite3.lib",
        'ws2_32.lib', 'crypt32.lib', 'advapi32.lib', 'user32.lib', 'shell32.lib', 'dbghelp.lib'
$objDir = "build_win\obj-$Compiler" + $(if ($DebugBuild) { '-debug' } else { '' }) + $(if ($Asan) { '-asan' } else { '' })
New-Item -ItemType Directory -Force $objDir | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $Output) | Out-Null

$common = @('/nologo', '/MT', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32_LEAN_AND_MEAN', '/Isrc', "/I$deps\include", "/Fo$objDir\", "/Fe$Output")
# /Zi + /DEBUG write a .pdb next to the executable so crash reports name
# functions; /OPT:REF,ICF keep the release binary as small as without it.
$opt = if ($DebugBuild) { @('/Od', '/Zi') } else { @('/O2', '/DNDEBUG', '/Zi') }
$linkOpt = @('/DEBUG', '/OPT:REF', '/OPT:ICF')
if ($Compiler -eq 'clang') {
    $cc = (Get-Command clang-cl.exe -ErrorAction SilentlyContinue).Source
    if (-not $cc) { $cc = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe' }
    if (-not (Test-Path $cc)) { throw 'clang-cl not found (install LLVM, or use -Compiler msvc).' }
    $flags = $common + $opt + @('/w', '-fno-strict-aliasing')
    if ($Asan) { $flags += '-fsanitize=address' }
} else {
    $cc = 'cl.exe'
    $flags = $common + $opt + @('/w')
}

Write-Host "Compiling with $cc"
& $cc @flags @sources '/link' '/STACK:536870912,4194304' @linkOpt @libs
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "Built $Output"

if (-not $NoSign) {
    & (Join-Path $PSScriptRoot 'sign-windows.ps1') -Path $Output
}
