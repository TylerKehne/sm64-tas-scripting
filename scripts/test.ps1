<#
.SYNOPSIS
    Build and run the correctness tests (tasfw-tests).

.DESCRIPTION
    Builds the tasfw-tests target with build.ps1, then runs it. The DLL-free tests always
    run. The libsm64 smoke test runs when a DLL and a movie are available: pass -Dll and
    -M64, or leave them unset to use res\sm64_jp_0.dll (scripts\unlock_libsm64.py makes it)
    and the committed movies\bitfs-pyramid-jp.m64. Exit code is the test runner's.

.PARAMETER Config
    Debug (default), Release or RelWithDebInfo.

.PARAMETER Compiler
    msvc (default) or clang.

.PARAMETER Dll, M64, Frame
    Inputs for the libsm64 smoke test. -Frame defaults to 3330.

.PARAMETER NoBuild
    Skip the build step.

.PARAMETER Filter
    doctest test-case filter (-tc=...), e.g. 'libsm64*' or '*Script*'.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\test.ps1
    powershell -ExecutionPolicy Bypass -File scripts\test.ps1 -Config Release -Compiler clang
#>
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Debug',
    [ValidateSet('msvc', 'clang')]
    [string]$Compiler = 'msvc',
    [string]$Dll = '',
    [string]$M64 = '',
    [int]$Frame = 3330,
    [switch]$NoBuild,
    [string]$Filter = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -Target tasfw-tests -Compiler $Compiler
}

$buildDir = Join-Path $root "build\$Config"
if ($Compiler -eq 'clang') { $buildDir = "$buildDir-clang" }
$exe = Join-Path $buildDir 'out\tasfw-tests.exe'
if (-not (Test-Path $exe)) { throw "tasfw-tests.exe not found at $exe" }

if (-not $Dll) {
    $candidate = Join-Path $root 'res\sm64_jp_0.dll'
    if (Test-Path $candidate) { $Dll = $candidate }
}
if (-not $M64) {
    $candidate = Join-Path $root 'movies\bitfs-pyramid-jp.m64'
    if (Test-Path $candidate) { $M64 = $candidate }
}

if ($Dll -and $M64) {
    $env:TASFW_LIBSM64 = $Dll
    $env:TASFW_M64 = $M64
    $env:TASFW_FRAME = "$Frame"
    Write-Host "libsm64 smoke test enabled: $Dll, $M64, frame $Frame"
} else {
    Remove-Item Env:TASFW_LIBSM64 -ErrorAction SilentlyContinue
    Remove-Item Env:TASFW_M64 -ErrorAction SilentlyContinue
    Write-Host "libsm64 smoke test skipped (no DLL/movie found; pass -Dll and -M64)"
}

$testArgs = @()
if ($Filter) { $testArgs += "-tc=$Filter" }

$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $exe @testArgs
$exit = $LASTEXITCODE
$ErrorActionPreference = $prevEap
exit $exit
