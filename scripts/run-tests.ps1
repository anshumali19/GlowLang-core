<#
.SYNOPSIS
  Run the GlowLang test suite on Windows.

.DESCRIPTION
  Runs every tests\*.glow file (from the repository root, by relative path)
  with a timeout and checks:
    - the exit code: 0, or the code listed in tests\expected\exit_codes.txt;
    - stdout: must equal tests\expected\<name>.out. In an expected line,
      {{ANY}} matches any text (used for timings and memory figures);
    - stderr: every line of tests\expected\<name>.err must appear in it.
  tests\expected\modes.txt can mark a test "unordered" (lines compared as a
  sorted multiset; for tests whose threads interleave) or "skip-stdout".
  Backslashes are normalised to forward slashes in all output, so expected
  files are the same on Windows and Linux (run-tests.sh shares them).

  -Update rewrites the .out files from the current binary. Only use it after
  checking by hand that the new output is correct.

.EXAMPLE
  .\scripts\run-tests.ps1 -Binary bin\glowlang.exe
#>
param(
    [string] $Binary = 'bin\glowlang.exe',
    [int] $TimeoutSeconds = 60,
    [string] $Filter = '*',
    [switch] $Update,
    [hashtable] $ExtraEnv = @{}
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$exe = (Resolve-Path $Binary).Path
$expectedDir = Join-Path $root 'tests\expected'

function Read-Table([string]$file) {
    $t = @{}
    if (Test-Path $file) {
        foreach ($line in Get-Content $file) {
            if ($line -match '^\s*([^#\s]+)\s+(\S+)') { $t[$Matches[1]] = $Matches[2] }
        }
    }
    return $t
}
$exitCodes = Read-Table (Join-Path $expectedDir 'exit_codes.txt')
$modes = Read-Table (Join-Path $expectedDir 'modes.txt')

function Normalize([string]$text) {
    if ($null -eq $text) { return '' }
    return (($text -replace "`r`n", "`n") -replace '\\', '/').TrimEnd("`n")
}

function Test-Line([string]$want, [string]$got) {
    if ($want -notmatch '\{\{ANY\}\}') { return $want -eq $got }
    $pattern = '^' + (($want -split '\{\{ANY\}\}' | ForEach-Object { [regex]::Escape($_) }) -join '.*') + '$'
    return $got -match $pattern
}

$pass = 0; $fail = 0; $failures = @()
$sw = [Diagnostics.Stopwatch]::StartNew()
foreach ($t in Get-ChildItem (Join-Path $root 'tests') -Filter "$Filter.glow" | Sort-Object Name) {
    $rel = "tests\" + $t.Name
    $psi = New-Object Diagnostics.ProcessStartInfo $exe, ("run `"" + $rel + "`"")
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true   # tests get an empty stdin
    $psi.WorkingDirectory = $root
    foreach ($k in $ExtraEnv.Keys) { $psi.EnvironmentVariables[$k] = [string]$ExtraEnv[$k] }
    $p = [Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        $p.Kill(); $code = 'TIMEOUT'
    } else { $code = $p.ExitCode }
    $out = Normalize $outTask.Result
    $err = Normalize $errTask.Result
    $mode = $modes[$t.Name]

    $wantCode = if ($exitCodes.ContainsKey($t.Name)) { [int]$exitCodes[$t.Name] } else { 0 }
    $expFile = Join-Path $expectedDir ($t.BaseName + '.out')
    $errFile = Join-Path $expectedDir ($t.BaseName + '.err')
    $problems = @()
    if ("$code" -ne "$wantCode") { $problems += "exit code $code (expected $wantCode)" }

    if ($mode -ne 'skip-stdout') {
        if ($Update -and "$code" -eq "$wantCode") {
            [IO.File]::WriteAllText($expFile, $out + "`n")
        } elseif (Test-Path $expFile) {
            $wl = @((Normalize ([IO.File]::ReadAllText($expFile))) -split "`n")
            $ol = @($out -split "`n")
            if ($mode -eq 'unordered') { $wl = @($wl | Sort-Object); $ol = @($ol | Sort-Object) }
            $n = [Math]::Max($wl.Count, $ol.Count)
            for ($i = 0; $i -lt $n; $i++) {
                $w = if ($i -lt $wl.Count) { $wl[$i] } else { $null }
                $g = if ($i -lt $ol.Count) { $ol[$i] } else { $null }
                if ($null -eq $w -or $null -eq $g -or -not (Test-Line $w $g)) {
                    $we = if ($null -eq $w) { '<end of output>' } else { $w }
                    $ge = if ($null -eq $g) { '<end of output>' } else { $g }
                    $problems += "stdout differs at line $($i + 1): expected '$we' got '$ge'"
                    break
                }
            }
        } else {
            $problems += "no expected output file (tests\expected\$($t.BaseName).out)"
        }
    }
    if (Test-Path $errFile) {
        foreach ($need in (Normalize ([IO.File]::ReadAllText($errFile))) -split "`n") {
            if ($need -and -not $err.Contains($need)) { $problems += "stderr lacks '$need'" }
        }
    }

    if ($problems.Count -eq 0) { $pass++ }
    else {
        $fail++
        $tail = if ($err) { ($err -split "`n" | Select-Object -Last 3) -join ' | ' } else { '' }
        $failures += "FAIL $($t.Name): $($problems -join '; ')$(if ($tail) { "  [stderr: $tail]" })"
    }
}
$failures | ForEach-Object { Write-Host $_ }
Write-Host ("Total: {0}  Passed: {1}  Failed: {2}  ({3:N1}s)" -f ($pass + $fail), $pass, $fail, $sw.Elapsed.TotalSeconds)
if ($fail -gt 0) { exit 1 }
