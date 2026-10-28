<#
.SYNOPSIS
  Builds the GlowLang MSIX package for the Microsoft Store.

.DESCRIPTION
  Puts glowlang.exe, gdu.exe, the standard library (lib\), LICENSE, NOTICE and
  the logos into a package folder, fills in packaging\msix\AppxManifest.xml.in
  and packs it with makeappx from the Windows SDK.

  The package is left unsigned: the Microsoft Store signs MSIX packages itself
  when they are published, which is what lets Windows (SmartScreen, Smart App
  Control) trust them. An unsigned package cannot be installed directly; it is
  meant to be uploaded to Partner Center.

  The identity values must match the app's Product identity page in Partner
  Center. Until the name is reserved, the defaults let the package build.

.EXAMPLE
  .\scripts\build-msix.ps1 -BinDir bin -Version 1.3.2
#>
param(
    [string] $BinDir = 'bin',
    [Parameter(Mandatory)] [string] $Version,
    [string] $IdentityName = 'GlowLang.GlowLang',
    [string] $Publisher = 'CN=00000000-0000-0000-0000-000000000000',
    [string] $PublisherDisplayName = 'Anshumali Sharma and Anamika Mukherjee',
    [string] $OutFile = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Store packages need a four-part version whose last part is 0: 1.3.2 -> 1.3.2.0.
$v = $Version.TrimStart('v') -replace '-.*$', ''
$parts = @($v.Split('.') | Where-Object { $_ -ne '' })
if ($parts.Count -lt 1 -or $parts.Count -gt 3 -or ($parts | Where-Object { $_ -notmatch '^\d+$' })) {
    throw "Version must look like 1.3.2 (got '$Version')"
}
while ($parts.Count -lt 3) { $parts += '0' }
$msixVersion = ($parts + '0') -join '.'

if (-not $OutFile) { $OutFile = Join-Path $root "dist\GlowLang-$($parts -join '.')-x64.msix" }

foreach ($exe in 'glowlang.exe', 'gdu.exe') {
    if (-not (Test-Path (Join-Path $BinDir $exe))) { throw "$exe not found in $BinDir (build first)" }
}

# makeappx from the newest installed Windows 10/11 SDK.
$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*\x64\makeappx.exe" -ErrorAction SilentlyContinue |
    Sort-Object { [version]$_.Directory.Parent.Name } -Descending | Select-Object -First 1
if (-not $makeappx) { throw 'makeappx.exe not found; install the Windows 10/11 SDK' }

$stage = Join-Path ([IO.Path]::GetTempPath()) "glowlang-msix-$PID"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
try {
    Copy-Item (Join-Path $BinDir 'glowlang.exe'), (Join-Path $BinDir 'gdu.exe') $stage
    Copy-Item -Recurse (Join-Path $root 'lib') (Join-Path $stage 'lib')
    Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'NOTICE') $stage
    Copy-Item -Recurse (Join-Path $root 'packaging\msix\Assets') (Join-Path $stage 'Assets')

    $manifest = Get-Content -Raw (Join-Path $root 'packaging\msix\AppxManifest.xml.in')
    $values = @{
        '@IDENTITY_NAME@'          = $IdentityName
        '@PUBLISHER@'              = $Publisher
        '@PUBLISHER_DISPLAY_NAME@' = $PublisherDisplayName
        '@VERSION@'                = $msixVersion
    }
    foreach ($k in $values.Keys) { $manifest = $manifest.Replace($k, [Security.SecurityElement]::Escape($values[$k])) }
    if ($manifest -match '@[A-Z_]+@') { throw "Unfilled manifest value: $($Matches[0])" }
    [IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding $false))

    $outDir = Split-Path -Parent $OutFile
    if ($outDir) { New-Item -ItemType Directory -Force $outDir | Out-Null }
    if (Test-Path $OutFile) { Remove-Item -Force $OutFile }
    # makeappx checks the manifest against the MSIX schema while packing.
    & $makeappx.FullName pack /d $stage /p $OutFile /o /h SHA256
    if ($LASTEXITCODE -ne 0) { throw "makeappx failed with exit code $LASTEXITCODE" }
} finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
}

Write-Output "Built $OutFile (version $msixVersion, $IdentityName, $Publisher)"
