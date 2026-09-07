<#
.SYNOPSIS
    Build and run the tasfw-perf benchmark suite, then compare against a baseline.

.DESCRIPTION
    Builds the tasfw-perf target in Release (via build.ps1), runs it with JSON output into
    perf\results\, and compares the result with perf\baselines\<computername>.json using
    scripts\perf_compare.py. Exit code is non-zero when a benchmark regresses by more than
    the threshold. See docs/performance.md for the policy.

.PARAMETER Filter
    Google Benchmark regex, e.g. 'Script' or 'BM_Scattershot.*'.

.PARAMETER Baseline
    Baseline JSON to compare against (default: perf\baselines\<computername>.json).

.PARAMETER SaveBaseline
    Store this run as the baseline instead of comparing.

.PARAMETER Threshold
    Regression threshold in percent (default 10).

.PARAMETER Config
    Release (default) or RelWithDebInfo. Debug is refused.

.PARAMETER NoBuild
    Skip the build step.

.PARAMETER Repetitions
    Repetitions per benchmark (default 3). The compare uses the median, which is what keeps
    a single noisy run from reading as a regression.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -SaveBaseline
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -Filter Script -NoBuild
#>
param(
    [string]$Filter = '',
    [string]$Baseline = '',
    [switch]$SaveBaseline,
    [double]$Threshold = 10,
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [switch]$NoBuild,
    [int]$Repetitions = 3
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -Target tasfw-perf
}

$exe = Join-Path $root "build\$Config\out\tasfw-perf.exe"
if (-not (Test-Path $exe)) {
    throw "tasfw-perf.exe not found at $exe. Build with scripts\build.ps1 -Config $Config first."
}

$resultsDir = Join-Path $root 'perf\results'
New-Item -ItemType Directory -Force $resultsDir | Out-Null

$sha = 'nogit'
try {
    $gitSha = (& git -C $root rev-parse --short HEAD 2>$null)
    if ($gitSha) { $sha = $gitSha.Trim() }
} catch {}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $resultsDir "$stamp-$sha.json"

$python = Get-Command python -ErrorAction SilentlyContinue
$compareScript = Join-Path $PSScriptRoot 'perf_compare.py'

# Each benchmark family runs in its own process. Allocation-heavy families leave the heap in
# a state that measurably changes later families (a 2x swing was observed), and a fresh
# process per family removes that coupling.
$families = @(
    '^BM_BinaryStateBin',
    '^BM_Scattershot',
    '^BM_Inputs',
    '^BM_M64',
    '^BM_SlotManager|^BM_Resource',
    '^BM_Script'
)
if ($Filter) { $families = @($Filter) }

Write-Host "Running $exe ($($families.Count) process(es), $Repetitions repetitions each)"
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'   # Google Benchmark writes its banner to stderr
$parts = @()
$index = 0
foreach ($family in $families) {
    $index++
    $part = Join-Path $resultsDir "$stamp-$sha-part$index.json"
    $benchArgs = @(
        "--benchmark_out=$part",
        '--benchmark_out_format=json',
        "--benchmark_repetitions=$Repetitions",
        '--benchmark_report_aggregates_only=true',
        '--benchmark_min_warmup_time=0.1',
        "--benchmark_filter=$family"
    )
    & $exe @benchArgs
    if ($LASTEXITCODE -ne 0) {
        $ErrorActionPreference = $prevEap
        throw "tasfw-perf exited with code $LASTEXITCODE on filter '$family'"
    }
    $parts += $part
}
$ErrorActionPreference = $prevEap

if ($python) {
    & $python.Source $compareScript merge -o $out @parts
    if ($LASTEXITCODE -ne 0) { throw "merge failed" }
    Remove-Item $parts -Force
    Write-Host "Results written to $out"
} else {
    Write-Host "python not found; leaving per-family results in $resultsDir and skipping merge/compare."
    exit 0
}

$baselineDir = Join-Path $root 'perf\baselines'
if (-not $Baseline) {
    $Baseline = Join-Path $baselineDir ("{0}.json" -f $env:COMPUTERNAME.ToLower())
}

if ($SaveBaseline) {
    New-Item -ItemType Directory -Force $baselineDir | Out-Null
    Copy-Item $out $Baseline -Force
    Write-Host "Saved baseline to $Baseline"
    exit 0
}

if (-not (Test-Path $Baseline)) {
    Write-Host "No baseline at $Baseline. Run with -SaveBaseline to create one."
    exit 0
}

& $python.Source $compareScript compare $Baseline $out --threshold $Threshold
exit $LASTEXITCODE
