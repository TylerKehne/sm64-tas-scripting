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
    process also runs at High priority. Pass 0 to leave scheduling alone. The thread-scaling
    family (^BM_LibSm64Scaling) is never pinned; it measures parallelism.

.PARAMETER Compiler
    msvc (default) or clang. Uses build\<Config>-clang and the baseline
    perf\baselines\<computername>-clang.json, so the two compilers are tracked separately.

.PARAMETER Dll, M64, Frame
    Inputs for the Tier B and C (libsm64) families. Default to res\sm64_jp_0.dll,
    res\comissonPyra2-Fanart_x-Z.m64 and frame 3330 when those files exist; those
    benchmarks are skipped otherwise. The thread-scaling family also needs the copies
    sm64_jp_1.dll .. sm64_jp_16.dll next to the DLL and is skipped without them.

.PARAMETER NoTierD
    Skip Tier D (scattershot end to end). Tier D runs bitfs-turn on perf\tierd-*.json,
    needs res\sm64_jp_0.dll .. sm64_jp_15.dll, takes about five minutes, and is skipped
    automatically when -Filter is given or the DLL copies are missing.

.PARAMETER TierDOnly
    Run only Tier D (no benchmark families); the compare then lists every other row as
    MISSING, which is not a failure.

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
    [int]$Affinity = 0x10,
    [string]$Dll = '',
    [string]$M64 = '',
    [int]$Frame = 3330,
    [switch]$NoTierD,
    [switch]$TierDOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Tier D: one bitfs-turn run per config, turned into a benchmark row that the compare script
# understands. The deterministic run carries exact counts; the throughput run only rates.
function ConvertFrom-TierDOutput([string]$Name, [string[]]$Lines, [bool]$Exact) {
    $found = $Lines | Where-Object { $_ -match '^Found (\d+) solutions in (\d+) shots, (\d+) blocks, (\d+) scripts \((\d+) base-block validation failures\)' } | Select-Object -Last 1
    if (-not $found) { throw "Tier D $Name`: no 'Found ...' summary in the output" }
    $null = $found -match '^Found (\d+) solutions in (\d+) shots, (\d+) blocks, (\d+) scripts \((\d+) base-block validation failures\)'
    $solutions = [double]$Matches[1]; $shots = [double]$Matches[2]; $blocks = [double]$Matches[3]; $scripts = [double]$Matches[4]; $failures = [double]$Matches[5]
    $stage = $Lines | Where-Object { $_ -match '^=== stage \S+: \d+ solution\(s\) in ([\d.]+) s' } | Select-Object -Last 1
    if (-not $stage) { throw "Tier D $Name`: no stage summary in the output" }
    $null = $stage -match 'in ([\d.]+) s'
    $seconds = [double]$Matches[1]
    $work = $Lines | Where-Object { $_ -match 'frame advances (\d+), saves (\d+), loads (\d+)' } | Select-Object -Last 1
    if (-not $work) { throw "Tier D $Name`: no resource counters in the output" }
    $null = $work -match 'frame advances (\d+), saves (\d+), loads (\d+)'
    $advances = [double]$Matches[1]; $saves = [double]$Matches[2]; $loads = [double]$Matches[3]

    $row = [ordered]@{
        name = $Name; run_name = $Name; run_type = 'iteration'; repetitions = 1; repetition_index = 0
        threads = 1; iterations = 1; real_time = $seconds * 1000.0; cpu_time = $seconds * 1000.0; time_unit = 'ms'
        validationFailures = $failures
    }
    if ($Exact) {
        $row.shots = $shots; $row.scripts = $scripts; $row.blocks = $blocks; $row.solutions = $solutions
        $row.frameAdvances = $advances; $row.saves = $saves; $row.loads = $loads
    } else {
        $row.shotsPerSecond = [math]::Round($shots / $seconds, 2)
        $row.scriptsPerSecond = [math]::Round($scripts / $seconds, 2)
        $row.frameAdvancesPerSecond = [math]::Round($advances / $seconds, 2)
    }
    return $row
}

if (-not $NoBuild) {
    if (-not $TierDOnly) {
        & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -Target tasfw-perf -Compiler $Compiler
    }
    if (-not $NoTierD -and -not $Filter) {
        & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -Target tasfw-bitfs-turnaround -Compiler $Compiler
    }
}

$buildDir = Join-Path $root "build\$Config"
if ($Compiler -eq 'clang') { $buildDir = "$buildDir-clang" }
$exe = Join-Path $buildDir 'out\tasfw-perf.exe'
if (-not (Test-Path $exe)) {
    throw "tasfw-perf.exe not found at $exe. Build with scripts\build.ps1 -Config $Config first."
}

# Tiers B and C need the game; same discovery as scripts\test.ps1.
if (-not $Dll) {
    $candidate = Join-Path $root 'res\sm64_jp_0.dll'
    if (Test-Path $candidate) { $Dll = $candidate }
}
if (-not $M64) {
    $candidate = Join-Path $root 'res\comissonPyra2-Fanart_x-Z.m64'
    if (Test-Path $candidate) { $M64 = $candidate }
}
if ($Dll -and $M64) {
    $env:TASFW_LIBSM64 = $Dll
    $env:TASFW_M64 = $M64
    $env:TASFW_FRAME = "$Frame"
    Write-Host "Tiers B and C (libsm64) enabled: $Dll, $M64, frame $Frame"
} else {
    Remove-Item Env:TASFW_LIBSM64 -ErrorAction SilentlyContinue
    Remove-Item Env:TASFW_M64 -ErrorAction SilentlyContinue
    Write-Host "Tiers B and C (libsm64) skipped (no DLL/movie found; pass -Dll and -M64)"
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
    '^BM_Script',
    '^BM_LibSm64Full',
    '^BM_LibSm64Light',
    '^BM_LibSm64Scaling',
    '^BM_Framework'
)
if ($Filter) { $families = @($Filter) }
if ($TierDOnly) { $families = @() }

Write-Host "Running $exe ($($families.Count) families x $Processes processes, $Repetitions repetitions each)"
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'   # Google Benchmark writes its banner to stderr

# Throwaway launch. The first launch of a freshly written executable measures differently
# from every later launch on Windows (Execute_ChildEmpty: 2.85 us first, 2.1 us after, and
# the allocation-heavy benchmarks flip mode with it). One short run puts the file into its
# steady state so the measured launches below are all "subsequent" launches.
if ($families.Count -gt 0) {
    $warm = Start-Process -FilePath $exe -ArgumentList @('--benchmark_filter=^BM_BinaryStateBin_Pack$', '--benchmark_min_time=0.01s') -NoNewWindow -PassThru -RedirectStandardOutput ([System.IO.Path]::GetTempFileName())
    $warm.WaitForExit()
}
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
        # The thread-scaling family measures parallelism; pinned to one CPU it would measure nothing.
        if ($Affinity -ne 0 -and $family -notmatch 'Scaling') { $proc.ProcessorAffinity = [IntPtr]$Affinity }
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

# Tier D: scattershot end to end through bitfs-turn (docs/performance.md). Not pinned to one
# CPU: the point is the multithreaded search. Output directories are under perf\results.
if (-not $NoTierD -and -not $Filter -and $Dll -and $M64) {
    $turn = Join-Path $buildDir 'out\bitfs-turn.exe'
    if (-not (Test-Path $turn)) { throw "bitfs-turn.exe not found at $turn (needed for Tier D; pass -NoTierD to skip)" }
    $tierD = @()
    $specs = @(
        @{ Name = 'TierD_Deterministic'; Config = 'tierd-deterministic.json'; Threads = 8; Exact = $true },
        @{ Name = 'TierD_Throughput'; Config = 'tierd-throughput.json'; Threads = 16; Exact = $false }
    )
    foreach ($spec in $specs) {
        $missing = @(0..($spec.Threads - 1) | Where-Object { -not (Test-Path (Join-Path $root ("res\sm64_jp_{0}.dll" -f $_))) })
        if ($missing.Count -gt 0) {
            Write-Host "Tier D $($spec.Name) skipped: res\sm64_jp_N.dll missing for N = $($missing -join ', ')"
            continue
        }
        $configPath = Join-Path $root ("perf\" + $spec.Config)
        Write-Host "Tier D $($spec.Name): $turn --config $configPath"
        # Launched as a Process so the peak working set can be sampled while it runs (it is
        # not readable after exit); stdout goes to a log that is echoed afterwards.
        $log = Join-Path $resultsDir "$stamp-$sha-$($spec.Name).log"
        $proc = Start-Process -FilePath $turn -ArgumentList @('--config', "`"$configPath`"") -NoNewWindow -PassThru -RedirectStandardOutput $log
        $null = $proc.Handle   # PowerShell 5.1: ExitCode reads empty after exit unless the handle was cached first
        $peakBytes = 0
        while (-not $proc.HasExited) {
            try { $proc.Refresh(); $peakBytes = [math]::Max($peakBytes, $proc.PeakWorkingSet64) } catch {}
            Start-Sleep -Milliseconds 500
        }
        $proc.WaitForExit()
        $lines = @(Get-Content $log)
        $lines | ForEach-Object { Write-Host $_ }
        if ($proc.ExitCode -ne 0) { throw "bitfs-turn exited with code $($proc.ExitCode) on $configPath (see $log)" }
        $row = ConvertFrom-TierDOutput -Name $spec.Name -Lines $lines -Exact $spec.Exact
        $row.peakResidentMB = [math]::Round($peakBytes / 1MB, 1)
        $tierD += $row
    }
    if ($tierD.Count -gt 0) {
        $index++
        $part = Join-Path $resultsDir "$stamp-$sha-part$index.json"
        $doc = [ordered]@{ context = [ordered]@{ tier = 'D' }; benchmarks = @($tierD) }
        $doc | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $part
        $parts += $part
    }
} elseif (-not $NoTierD -and -not $Filter) {
    Write-Host "Tier D skipped (no DLL/movie found)"
}

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
