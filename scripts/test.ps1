<#
.SYNOPSIS
    Build and run the correctness tests (tasfw-tests).

.DESCRIPTION
    Builds the tasfw-tests target with build.ps1, then runs it. The DLL-free tests always
    run. The libsm64 smoke test runs when a DLL and a movie are available: pass -Dll and
    -M64, or leave them unset to use res\sm64_jp_0.dll (scripts\unlock_libsm64.py makes it)
    and the committed movies\bitfs-pyramid-jp.m64. When -Dll and -M64 are unset and
    res\sm64_us_0.dll exists (unlock_libsm64.py --version us), the libsm64 test group runs a
    second time on it with movies\bitfs-pyramid-us.m64 at frame 3397, the frame of the JP
    movie's 3330 there (docs/libsm64.md). Exit code is non-zero if either run fails.

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

# The US game, when unlocked: the libsm64 group runs on it too, after the main run.
$usDll = ''
$usM64 = ''
if (-not $Dll -and -not $M64) {
    $candidateDll = Join-Path $root 'res\sm64_us_0.dll'
    $candidateM64 = Join-Path $root 'movies\bitfs-pyramid-us.m64'
    if ((Test-Path $candidateDll) -and (Test-Path $candidateM64)) {
        $usDll = $candidateDll
        $usM64 = $candidateM64
    }
}

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

if ($usDll) {
    $env:TASFW_LIBSM64 = $usDll
    $env:TASFW_M64 = $usM64
    $env:TASFW_FRAME = '3397'
    Write-Host ""
    Write-Host "libsm64 test group on the US game: $usDll, $usM64, frame 3397"
    & $exe '-tc=libsm64*'
    if ($LASTEXITCODE -ne 0) { $exit = $LASTEXITCODE }
}
$ErrorActionPreference = $prevEap
exit $exit
