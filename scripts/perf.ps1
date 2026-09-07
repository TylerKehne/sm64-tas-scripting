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
    Repetitions per benchmark within one process (default 3). The compare uses the fastest
    repetition across all processes, which is what keeps background noise from reading as
    a regression.

.PARAMETER Processes
    Fresh processes per benchmark family (default 3). Some allocation-heavy benchmarks are
    bimodal per process (heap layout differs from launch to launch); taking the minimum
    across several launches removes that.

.PARAMETER Affinity
    Processor affinity mask for the benchmark process (default 0x10, one logical CPU). The
    process also runs at High priority. Pass 0 to leave scheduling alone.

.PARAMETER Compiler
    msvc (default) or clang. Uses build\<Config>-clang and the baseline
    perf\baselines\<computername>-clang.json, so the two compilers are tracked separately.

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
    [int]$Repetitions = 3,
    [int]$Processes = 3,
    [ValidateSet('msvc', 'clang')]
    [string]$Compiler = 'msvc',
    [int]$Affinity = 0x10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -Target tasfw-perf -Compiler $Compiler
}

$buildDir = Join-Path $root "build\$Config"
if ($Compiler -eq 'clang') { $buildDir = "$buildDir-clang" }
$exe = Join-Path $buildDir 'out\tasfw-perf.exe'
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

Write-Host "Running $exe ($($families.Count) families x $Processes processes, $Repetitions repetitions each)"
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'   # Google Benchmark writes its banner to stderr

# Throwaway launch. The first launch of a freshly written executable measures differently
# from every later launch on Windows (Execute_ChildEmpty: 2.85 us first, 2.1 us after, and
# the allocation-heavy benchmarks flip mode with it). One short run puts the file into its
# steady state so the measured launches below are all "subsequent" launches.
$warm = Start-Process -FilePath $exe -ArgumentList @('--benchmark_filter=^BM_BinaryStateBin_Pack$', '--benchmark_min_time=0.01s') -NoNewWindow -PassThru -RedirectStandardOutput ([System.IO.Path]::GetTempFileName())
$warm.WaitForExit()
$parts = @()
$index = 0
$runs = @()
foreach ($family in $families) {
    for ($p = 0; $p -lt $Processes; $p++) { $runs += $family }
}
foreach ($family in $runs) {
    $index++
    $part = Join-Path $resultsDir "$stamp-$sha-part$index.json"
    $benchArgs = @(
        "--benchmark_out=$part",
        '--benchmark_out_format=json',
        "--benchmark_repetitions=$Repetitions",
        '--benchmark_display_aggregates_only=true',
        "--benchmark_filter=$family"
    )
    # Launch through ProcessStartInfo so priority and affinity can be set; stdout/stderr inherit.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($benchArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    try {
        $proc.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High
        if ($Affinity -ne 0) { $proc.ProcessorAffinity = [IntPtr]$Affinity }
    } catch {
        Write-Host "note: could not set priority/affinity: $($_.Exception.Message)"
    }
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) {
        $ErrorActionPreference = $prevEap
        throw "tasfw-perf exited with code $($proc.ExitCode) on filter '$family'"
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
    $suffix = ''
    if ($Compiler -eq 'clang') { $suffix = '-clang' }
    $Baseline = Join-Path $baselineDir ("{0}{1}.json" -f $env:COMPUTERNAME.ToLower(), $suffix)
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
