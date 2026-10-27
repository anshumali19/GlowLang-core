<#
.SYNOPSIS
  Authenticode-sign GlowLang binaries with the GlowLang code-signing certificate.

.DESCRIPTION
  Signs each file with SHA-256, using the exact certificate selected by
  thumbprint (or by subject, picking the newest valid one), adds a description,
  and requests an RFC 3161 timestamp so the signature stays valid after the
  certificate expires. Afterwards it re-reads every signature and checks that
  it was made by the expected certificate.

  The default certificate (CN=GlowLangDev) is self-signed. Windows reports such
  signatures as "not trusted" until the certificate is added to the Trusted
  Root and Trusted Publishers stores; certs\GlowLangDev.cer is the public half.
  Smart App Control never treats a self-signed certificate as trusted (it
  needs a CA in Microsoft's Trusted Root Program), so on machines with SAC
  enabled a new build only runs if SAC's cloud check predicts it is safe.

.EXAMPLE
  .\scripts\sign-windows.ps1 -Path bin\glowlang.exe
#>
param(
    [Parameter(Mandatory = $true)] [string[]] $Path,
    [string] $Thumbprint,
    [string] $Subject = 'CN=GlowLangDev',
    [string] $TimestampUrl = 'http://timestamp.digicert.com',
    [string] $Description = 'GlowLang',
    # Off by default: on this machine Smart App Control blocked binaries whose
    # signature carried a /du URL, while the same file signed without it ran.
    [string] $DescriptionUrl = '',
    [switch] $NoTimestamp
)
$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $found = Get-ChildItem $kits -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($found) { return $found }
    }
    throw 'signtool.exe not found. Install the Windows 10/11 SDK (Signing Tools for Desktop Apps).'
}

function Find-Certificate {
    $now = Get-Date
    $certs = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.HasPrivateKey -and $_.NotBefore -le $now -and $_.NotAfter -gt $now }
    if ($Thumbprint) {
        $c = $certs | Where-Object { $_.Thumbprint -eq $Thumbprint.ToUpper() } | Select-Object -First 1
        if (-not $c) { throw "No valid code-signing certificate with thumbprint $Thumbprint and a private key in Cert:\CurrentUser\My." }
        return $c
    }
    $c = $certs | Where-Object { $_.Subject -eq $Subject } | Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $c) { throw "No valid code-signing certificate '$Subject' with a private key in Cert:\CurrentUser\My." }
    return $c
}

$signtool = Find-SignTool
$cert = Find-Certificate
Write-Host "Signing with $($cert.Subject) [$($cert.Thumbprint)], valid until $($cert.NotAfter.ToString('yyyy-MM-dd'))"

foreach ($file in $Path) {
    $full = (Resolve-Path $file).Path
    $common = @('sign', '/sha1', $cert.Thumbprint, '/fd', 'SHA256', '/d', $Description)
    if ($DescriptionUrl) { $common += @('/du', $DescriptionUrl) }
    $stamped = $false
    if (-not $NoTimestamp) {
        & $signtool @common '/tr' $TimestampUrl '/td' 'SHA256' $full | Out-Host
        if ($LASTEXITCODE -eq 0) { $stamped = $true }
        else { Write-Warning "Timestamp server unreachable; signing $file without a timestamp." }
    }
    if (-not $stamped) {
        & $signtool @common $full | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "signtool failed for $file" }
    }

    $sig = Get-AuthenticodeSignature $full
    if (-not $sig.SignerCertificate -or $sig.SignerCertificate.Thumbprint -ne $cert.Thumbprint) {
        throw "Signature check failed for ${file}: signer is not $($cert.Thumbprint)."
    }
    $ts = if ($sig.TimeStamperCertificate) { "timestamped by $($sig.TimeStamperCertificate.Subject)" } else { 'no timestamp' }
    switch ($sig.Status) {
        'Valid'        { Write-Host "OK  $file  (trusted signature, $ts)" }
        'UnknownError' { Write-Host "OK  $file  (signed, $ts; certificate is self-signed and not yet trusted on this machine)" }
        default        { throw "Signature on $file has status $($sig.Status): $($sig.StatusMessage)" }
    }
}
