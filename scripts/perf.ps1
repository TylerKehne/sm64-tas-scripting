<#
.SYNOPSIS
    Build and run the tasfw-perf benchmark suite, then compare against a baseline.

.DESCRIPTION
    Builds the tasfw-perf target in Release (via build.ps1), runs it with JSON output into
    perf\results\, and compares the result with perf\baselines\<computername>.json using
    scripts\perf_compare.py. Exit code is non-zero when a benchmark regresses by more than
    the threshold. See docs/performance.md for the policy.

    Time is gated relative, not absolute. The baseline commit's binaries (the "reference",
    which -SaveBaseline keeps under perf\reference\<computername>[-clang]\) run interleaved
    with the current ones in the same session, and the compare gates the current binary
    against the reference: both saw the same machine state, so the day's drift cancels. The
    committed baseline anchors the exact counts and shows the drift as a machine factor.
    Without a reference the compare falls back to the committed baseline and says so.

    Before measuring, the script refuses to run next to a virtual machine or a busy CPU,
    reports when Defender's real-time scanning still covers the build tree, switches the
    power plan to High performance for the duration (restored afterwards), and runs a short
    calibration (the reference's frame advance against the baseline's) that catches a
    throttled or busy machine. The deterministic Tier D run is pinned one thread per
    performance core; nothing is ever pinned to the efficiency cores.

.PARAMETER Filter
    Google Benchmark regex, e.g. 'Script' or 'BM_Scattershot.*'.

.PARAMETER Baseline
    Baseline JSON to compare against (default: perf\baselines\<computername>.json).

.PARAMETER SaveBaseline
    Store this run as the baseline instead of comparing, and the current binaries as the
    reference for it.

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
    Fresh processes per benchmark family and binary (default 3). Some allocation-heavy
    benchmarks are bimodal per process (heap layout differs from launch to launch); taking
    the minimum across several launches removes that.

.PARAMETER Affinity
    Processor affinity mask for the single-threaded benchmark process (default 0x10, one
    logical CPU of a performance core). The process also runs at High priority. Pass 0 to
    leave scheduling alone. The thread-scaling family (^BM_LibSm64Scaling) is never pinned;
    it measures parallelism, and packing it onto the performance cores hung it (see docs).

.PARAMETER Compiler
    msvc (default) or clang. Uses build\<Config>-clang and the baseline
    perf\baselines\<computername>-clang.json, so the two compilers are tracked separately.

.PARAMETER Dll, M64, Frame
    Inputs for the Tier B and C (libsm64) families. Default to res\sm64_jp_0.dll,
    the committed movies\bitfs-pyramid-jp.m64 and frame 3330 when those files exist; those
    benchmarks are skipped otherwise. The thread-scaling family also needs the copies
    sm64_jp_1.dll .. sm64_jp_16.dll next to the DLL and is skipped without them.

.PARAMETER NoTierD
    Skip Tier D (scattershot end to end). Tier D runs bitfs-turn on perf\tierd-*.json,
    needs res\sm64_jp_0.dll .. sm64_jp_15.dll, takes about five minutes per binary, and is
    skipped automatically when -Filter is given or the DLL copies are missing.

.PARAMETER TierDOnly
    Run only Tier D (no benchmark families); the compare then lists every other row as
    MISSING, which is not a failure.

.PARAMETER Reference
    Directory holding the baseline commit's tasfw-perf.exe and bitfs-turn.exe (default
    perf\reference\<computername>[-clang]). -SaveBaseline fills it from the current build;
    for a baseline saved elsewhere, build its commit and copy the two executables there.

.PARAMETER NoReference
    Compare absolute against the committed baseline even when a reference exists.

.PARAMETER Alternations
    Reference/current pairs per Tier D workload (default 1); the fastest run of each binary
    is used.

.PARAMETER SkipPreflight
    Run even next to a VM, a busy CPU or a calibration outside its band (the checks still
    print what they see).

.PARAMETER PowerPlan
    Power scheme to activate for the run, alias or GUID (default SCHEME_MIN, High
    performance); the previous scheme is restored afterwards. '' leaves the plan alone.

.PARAMETER SetupDefender
    Add Defender real-time scanning exclusions for build\, res\, perf\reference\ and
    perf\results\ (asks for elevation) and exit. One-time; the pre-flight check reports
    when they are missing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -SaveBaseline
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -Filter Script -NoBuild
    powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -SetupDefender
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
    [switch]$TierDOnly,
    [string]$Reference = '',
    [switch]$NoReference,
    [int]$Alternations = 1,
    [switch]$SkipPreflight,
    [string]$PowerPlan = 'SCHEME_MIN',
    [switch]$SetupDefender
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# ---------------------------------------------------------------------------- helpers

# Physical cores with their efficiency class from GetLogicalProcessorInformationEx (Windows
# 10 20H1+). Hybrid Intel parts report the performance cores as the highest class; uniform
# parts report one class, so every core is a "performance core" there. Processor group 0.
function Get-CpuTopology {
    if (-not ('TasfwCpuTopology' -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TasfwCpuTopology
{
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetLogicalProcessorInformationEx(int relationship, IntPtr buffer, ref uint length);

    // One entry per physical core: [efficiencyClass, logical CPU mask in group 0].
    public static List<ulong[]> Cores()
    {
        var cores = new List<ulong[]>();
        uint length = 0;
        GetLogicalProcessorInformationEx(0, IntPtr.Zero, ref length); // 0 = RelationProcessorCore
        IntPtr buffer = Marshal.AllocHGlobal((int)length);
        try
        {
            if (!GetLogicalProcessorInformationEx(0, buffer, ref length))
                throw new InvalidOperationException("GetLogicalProcessorInformationEx failed: " + Marshal.GetLastWin32Error());
            int offset = 0;
            while (offset < length)
            {
                IntPtr entry = buffer + offset;
                int size = Marshal.ReadInt32(entry, 4);
                // PROCESSOR_RELATIONSHIP at +8: Flags (byte), EfficiencyClass (byte), Reserved[20],
                // GroupCount (ushort) at +30, GROUP_AFFINITY[] at +32 (Mask ulong, Group ushort).
                byte efficiencyClass = Marshal.ReadByte(entry, 9);
                ushort groupCount = (ushort)Marshal.ReadInt16(entry, 30);
                if (groupCount >= 1)
                {
                    ulong mask = (ulong)Marshal.ReadInt64(entry, 32);
                    ushort group = (ushort)Marshal.ReadInt16(entry, 40);
                    if (group == 0)
                        cores.Add(new ulong[] { efficiencyClass, mask });
                }
                offset += size;
            }
        }
        finally { Marshal.FreeHGlobal(buffer); }
        return cores;
    }
}
"@
    }
    $cores = [TasfwCpuTopology]::Cores()
    $maxClass = ($cores | ForEach-Object { $_[0] } | Measure-Object -Maximum).Maximum
    $perf = @($cores | Where-Object { $_[0] -eq $maxClass })
    [uint64]$all = 0
    [uint64]$one = 0
    foreach ($c in $perf) {
        [uint64]$mask = $c[1]
        $all = $all -bor $mask
        $one = $one -bor ($mask -band ((-bnot $mask) + 1))   # lowest set bit: one logical CPU per core
    }
    return @{
        Cores = $cores.Count
        PerformanceCores = $perf.Count
        Hybrid = ($perf.Count -lt $cores.Count)
        AllMask = $all          # every logical CPU of a performance core
        OnePerCoreMask = $one   # one logical CPU per performance core (no SMT sibling sharing)
    }
}

function Get-BitCount([uint64]$Mask) {
    $n = 0
    while ($Mask -ne 0) { $n += [int]($Mask -band 1); $Mask = $Mask -shr 1 }
    return $n
}

function Format-Mask([uint64]$Mask) { return ('0x{0:X}' -f $Mask) }

# Affinity for a Tier D workload of $Threads threads: one logical CPU per performance core
# when the workload asks for that and there are enough cores (no SMT sibling sharing, no
# efficiency cores), else unpinned (0). Packing 16 threads onto the performance cores' 16
# logical CPUs is never done: it hung the scaling family in 2 of 20 launches, both binaries
# (docs/performance.md, Tier B; ROADMAP).
function Get-TierDAffinity($Topology, [int]$Threads, [bool]$OnePerCore) {
    if ($OnePerCore -and $Topology.PerformanceCores -ge $Threads) { return $Topology.OnePerCoreMask }
    return [uint64]0
}

function Get-ActivePowerScheme {
    $text = (& powercfg /getactivescheme 2>&1) | Out-String
    if ($text -match 'GUID:\s*([0-9a-fA-F-]+)') { return $Matches[1] }
    return $null
}

function Get-PowerSchemeName([string]$Guid) {
    $line = (& powercfg /list 2>&1) | Where-Object { $_ -match [regex]::Escape($Guid) } | Select-Object -First 1
    if ($line -and $line -match '\(([^)]+)\)') { return $Matches[1] }
    return $Guid
}

# Run one tasfw-perf process (family filter, JSON part) at High priority, pinned to $Mask
# (0 = unpinned). Google Benchmark writes its banner to stderr; the process inherits the
# console, so that never reaches PowerShell's error stream.
function Invoke-Bench([string]$Exe, [string]$Family, [string]$Part, [uint64]$Mask) {
    $benchArgs = @(
        "--benchmark_out=$Part",
        '--benchmark_out_format=json',
        "--benchmark_repetitions=$Repetitions",
        '--benchmark_display_aggregates_only=true',
        "--benchmark_filter=$Family"
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.Arguments = ($benchArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    try {
        $proc.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High
        if ($Mask -ne 0) { $proc.ProcessorAffinity = [IntPtr]([int64]$Mask) }
    } catch {
        Write-Host "note: could not set priority/affinity: $($_.Exception.Message)"
    }
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) {
        throw "$Exe exited with code $($proc.ExitCode) on filter '$Family'"
    }
}

# Fastest repetition of one benchmark in a result file, with its unit; $null when absent.
function Get-MinRealTime([string]$Path, [string]$Name) {
    $data = Get-Content $Path -Raw | ConvertFrom-Json
    $rows = @($data.benchmarks | Where-Object {
        (-not $_.error_occurred) -and ($_.run_type -ne 'aggregate') -and
        (($_.run_name -eq $Name) -or ($_.name -eq $Name))
    })
    if ($rows.Count -eq 0) { return $null }
    $min = ($rows | ForEach-Object { [double]$_.real_time } | Measure-Object -Minimum).Minimum
    return @{ Min = $min; Unit = $rows[0].time_unit }
}

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
    # CPU cycles over every thread (bitfs-turn prints them where the platform counts them).
    $cycles = $Lines | Where-Object { $_ -match '^\s*process cycles (\d+)' } | Select-Object -Last 1
    if ($cycles) {
        $null = $cycles -match 'process cycles (\d+)'
        $row.cycles = [double]$Matches[1]
    }
    return $row
}

# One bitfs-turn run, at High priority pinned to $Mask (0 = unpinned), stdout to $Log; the
# peak working set is sampled while it runs (it is not readable after exit).
function Invoke-TierD([string]$Exe, [string]$ConfigPath, [string]$Log, [uint64]$Mask) {
    $proc = Start-Process -FilePath $Exe -ArgumentList @('--config', "`"$ConfigPath`"") -NoNewWindow -PassThru -RedirectStandardOutput $Log
    $null = $proc.Handle   # PowerShell 5.1: ExitCode reads empty after exit unless the handle was cached first
    try {
        $proc.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High
        if ($Mask -ne 0) { $proc.ProcessorAffinity = [IntPtr]([int64]$Mask) }
    } catch {
        Write-Host "note: could not set priority/affinity: $($_.Exception.Message)"
    }
    $peakBytes = 0
    while (-not $proc.HasExited) {
        try { $proc.Refresh(); $peakBytes = [math]::Max($peakBytes, $proc.PeakWorkingSet64) } catch {}
        Start-Sleep -Milliseconds 500
    }
    $proc.WaitForExit()
    $lines = @(Get-Content $Log)
    $lines | ForEach-Object { Write-Host $_ }
    if ($proc.ExitCode -ne 0) { throw "bitfs-turn exited with code $($proc.ExitCode) on $ConfigPath (see $Log)" }
    return @{ Lines = $lines; PeakMB = [math]::Round($peakBytes / 1MB, 1) }
}

# ------------------------------------------------------------------ Defender set-up

$defenderPaths = @("$root\build", "$root\res", "$root\perf\reference", "$root\perf\results")

if ($SetupDefender) {
    $elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $command = "Add-MpPreference -ExclusionPath '" + ($defenderPaths -join "','") + "'"
    if ($elevated) {
        Invoke-Expression $command
    } else {
        Write-Host "Asking for elevation to run: $command"
        Start-Process powershell -Verb RunAs -Wait -ArgumentList @('-NoProfile', '-Command', $command)
    }
    $now = @((Get-MpPreference).ExclusionPath)
    Write-Host "Defender exclusions now:"
    $now | ForEach-Object { Write-Host "  $_" }
    exit 0
}

# ------------------------------------------------------------------------ build

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
$turn = Join-Path $buildDir 'out\bitfs-turn.exe'

$suffix = ''
if ($Compiler -eq 'clang') { $suffix = '-clang' }
$machine = $env:COMPUTERNAME.ToLower()
$baselineDir = Join-Path $root 'perf\baselines'
if (-not $Baseline) { $Baseline = Join-Path $baselineDir ("{0}{1}.json" -f $machine, $suffix) }
if (-not $Reference) { $Reference = Join-Path $root ("perf\reference\{0}{1}" -f $machine, $suffix) }

# The reference: the baseline commit's binaries, run interleaved with the current ones.
$refExe = Join-Path $Reference 'tasfw-perf.exe'
$refTurn = Join-Path $Reference 'bitfs-turn.exe'
$useReference = (-not $NoReference) -and (-not $SaveBaseline) -and (Test-Path $refExe)
$referenceSha = ''
if ($useReference) {
    $refInfo = Join-Path $Reference 'reference.json'
    if (Test-Path $refInfo) { $referenceSha = (Get-Content $refInfo -Raw | ConvertFrom-Json).sha }
    Write-Host "Reference: $Reference (commit $referenceSha); time gates against it, interleaved with the current build"
} elseif ($SaveBaseline) {
    Write-Host "Saving a baseline: no reference runs; the current binaries become the reference"
} elseif ($NoReference) {
    Write-Host "Reference disabled (-NoReference): time gates absolute against the committed baseline"
} else {
    Write-Host "No reference at $Reference`: time gates absolute against the committed baseline (noisier)."
    Write-Host "  Build the baseline commit and copy tasfw-perf.exe and bitfs-turn.exe there, or -SaveBaseline."
}

# Tiers B and C need the game; same discovery as scripts\test.ps1.
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
    # A baseline is usually saved before its change is committed; say so in the name.
    $dirty = @(& git -C $root status --porcelain --untracked-files=no 2>$null)
    if ($dirty.Count -gt 0) { $sha += '-dirty' }
} catch {}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $resultsDir "$stamp-$sha.json"
$refOut = Join-Path $resultsDir "$stamp-$sha-reference.json"

$python = Get-Command python -ErrorAction SilentlyContinue
$compareScript = Join-Path $PSScriptRoot 'perf_compare.py'

# -------------------------------------------------------------------- topology

$topology = Get-CpuTopology
if ($topology.Hybrid) {
    Write-Host ("CPU: {0} cores, {1} performance cores (logical {2}); efficiency cores are never used for measuring" -f $topology.Cores, $topology.PerformanceCores, (Format-Mask $topology.AllMask))
} else {
    Write-Host ("CPU: {0} cores, uniform" -f $topology.Cores)
}
[uint64]$affinityMask = [uint64]$Affinity
if ($affinityMask -ne 0 -and ($affinityMask -band $topology.AllMask) -eq 0) {
    # The default CPU is not a performance core on this machine (or does not exist): take
    # the first CPU of the second performance core, away from CPU 0 where interrupts land.
    [uint64]$m = $topology.OnePerCoreMask
    $m = $m -band ($m - 1)
    if ($m -eq 0) { $m = $topology.OnePerCoreMask }
    $affinityMask = $m -band ((-bnot $m) + 1)
    Write-Host ("Affinity {0} is not a performance-core CPU here; using {1}" -f (Format-Mask ([uint64]$Affinity)), (Format-Mask $affinityMask))
}

# -------------------------------------------------------------------- pre-flight

$preflightFailed = @()

# A virtual machine on the host (Docker Desktop's alone added about 11% to Tier D).
$vmNames = @('vmmem', 'vmmemWSL', 'Docker Desktop', 'vmwp', 'VirtualBoxVM', 'vmware-vmx')
$vms = @(Get-Process -Name $vmNames -ErrorAction SilentlyContinue | Select-Object -ExpandProperty ProcessName -Unique)
if ($vms.Count -gt 0) {
    $preflightFailed += "a virtual machine is running ($($vms -join ', ')); stop it (Docker Desktop: quit it, then wsl --shutdown)"
}

# CPU load over three seconds, and who is causing it. The raw idle-time counter differenced
# over this window; the cooked PercentProcessorTime reports the previous interval on its
# first call (which is this script's own start-up) and zero on the next ones.
$before = @{}
Get-Process | ForEach-Object { try { $before[$_.Id] = @{ Name = $_.ProcessName; Cpu = $_.TotalProcessorTime.TotalMilliseconds } } catch {} }
$raw0 = Get-CimInstance Win32_PerfRawData_PerfOS_Processor -Filter "Name='_Total'"
Start-Sleep -Seconds 3
$raw1 = Get-CimInstance Win32_PerfRawData_PerfOS_Processor -Filter "Name='_Total'"
$load = 100.0 * (1.0 - ([double]($raw1.PercentProcessorTime - $raw0.PercentProcessorTime) / [double]($raw1.Timestamp_Sys100NS - $raw0.Timestamp_Sys100NS)))
$busy = @()
Get-Process | ForEach-Object {
    try {
        if ($before.ContainsKey($_.Id)) {
            $ms = $_.TotalProcessorTime.TotalMilliseconds - $before[$_.Id].Cpu
            if ($ms -gt 100) { $busy += @{ Name = $_.ProcessName; Cores = [math]::Round($ms / 3000.0, 2) } }
        }
    } catch {}
}
$busy = @($busy | Sort-Object { -$_.Cores } | Select-Object -First 5)
$busyText = ($busy | ForEach-Object { "{0} {1} core(s)" -f $_.Name, $_.Cores }) -join ', '
Write-Host ("CPU load: {0:N1}% over 3 s{1}" -f $load, $(if ($busyText) { " ($busyText)" } else { '' }))
if ($load -gt 8.0) {
    # 8%: the desktop idles at 3 to 4% (vendor services); a build, a scan or a stray run is far above.
    $preflightFailed += ("the CPU is {0:N1}% busy; a build, a scan or another program is running ({1})" -f $load, $busyText)
}

# Defender scans every file the build and the runs write; exclusions for the tree are a
# one-time set-up (-SetupDefender). Reported, not fatal.
try {
    $status = Get-MpComputerStatus -ErrorAction Stop
    if ($status.RealTimeProtectionEnabled) {
        $excl = @((Get-MpPreference -ErrorAction Stop).ExclusionPath | Where-Object { $_ })
        $missing = @($defenderPaths | Where-Object {
            $p = $_
            -not ($excl | Where-Object { $p.StartsWith($_.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) })
        })
        if ($missing.Count -gt 0) {
            Write-Host "note: Defender real-time scanning covers $($missing -join ', '); run scripts\perf.ps1 -SetupDefender once to exclude them"
        } else {
            Write-Host "Defender: the build tree is excluded from real-time scanning"
        }
    }
} catch {
    Write-Host "Defender status not readable ($($_.Exception.Message)); skipping that check"
}

if ($preflightFailed.Count -gt 0) {
    $preflightFailed | ForEach-Object { Write-Host "pre-flight: $_" }
    if (-not $SkipPreflight) { throw "pre-flight failed; fix the above or pass -SkipPreflight" }
    Write-Host "continuing anyway (-SkipPreflight)"
}

# ------------------------------------------------------------------- power plan

$previousScheme = Get-ActivePowerScheme
$switchedScheme = $false
if ($PowerPlan) {
    & powercfg /setactive $PowerPlan 2>&1 | Out-Null
    $active = Get-ActivePowerScheme
    if ($active -and $active -ne $previousScheme) {
        $switchedScheme = $true
        Write-Host "Power plan: $(Get-PowerSchemeName $active) for the run (restored afterwards)"
    } elseif ($active) {
        Write-Host "Power plan: $(Get-PowerSchemeName $active) (already active, or '$PowerPlan' is not available: powercfg /list)"
    }
}

$parts = @()
$refParts = @()
$contextArgs = @("--context", "tasfw_sha=$sha", "--context", "compiler=$Compiler", "--context", "power_plan=$(Get-PowerSchemeName (Get-ActivePowerScheme))")
if ($referenceSha) { $contextArgs += @("--context", "reference_sha=$referenceSha") }
$prevEap = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'   # Google Benchmark writes its banner to stderr

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
        '^BM_LibSm64Fixed',
        '^BM_LibSm64Dirty',
        '^BM_LibSm64Scaling',
        '^BM_Framework'
    )
    if ($Filter) { $families = @($Filter) }
    if ($TierDOnly) { $families = @() }

    # Throwaway launches. The first launch of a freshly written executable measures differently
    # from every later launch on Windows (Execute_ChildEmpty: 2.85 us first, 2.1 us after, and
    # the allocation-heavy benchmarks flip mode with it). One short run puts the file into its
    # steady state so the measured launches below are all "subsequent" launches.
    $warmExes = @($exe)
    if ($useReference) { $warmExes += $refExe }
    foreach ($warmExe in $warmExes) {
        $warm = Start-Process -FilePath $warmExe -ArgumentList @('--benchmark_filter=^BM_BinaryStateBin_Pack$', '--benchmark_min_time=0.01s') -NoNewWindow -PassThru -RedirectStandardOutput ([System.IO.Path]::GetTempFileName())
        $warm.WaitForExit()
    }

    # Calibration: one short, fixed row from the reference (the same binary the baseline was
    # saved from) against the baseline's reading of it. The ratio is the machine's state
    # today. Outside the band something is wrong with the machine, not the code: wait and
    # retry, then refuse.
    $machineFactor = ''
    if (Test-Path $Baseline) {
        if ($Dll -and $M64) { $calFilter = '^BM_LibSm64Fixed_FrameAdvance'; $calName = 'BM_LibSm64Fixed_FrameAdvance/iterations:3000' }
        else { $calFilter = '^BM_BinaryStateBin_Pack$'; $calName = 'BM_BinaryStateBin_Pack' }
        $calBase = Get-MinRealTime $Baseline $calName
        if ($calBase) {
            $calExe = $exe
            if ($useReference) { $calExe = $refExe }
            for ($attempt = 1; $attempt -le 3; $attempt++) {
                $calPart = Join-Path $resultsDir "$stamp-$sha-calibration.json"
                Invoke-Bench $calExe $calFilter $calPart $affinityMask
                $calNow = Get-MinRealTime $calPart $calName
                Remove-Item $calPart -Force
                if (-not $calNow) { break }
                $factor = $calNow.Min / $calBase.Min
                $machineFactor = $factor.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
                Write-Host ("Calibration: {0} {1:N2} {2} now vs {3:N2} {2} at the baseline (machine factor {4:N2}; {5})" -f $calName, $calNow.Min, $calNow.Unit, $calBase.Min, $factor, $(if ($useReference) { 'reference binary' } else { 'current binary' }))
                if ($factor -le 1.25 -and $factor -ge 0.8) { break }
                if ($SkipPreflight) { Write-Host "  outside 0.80..1.25; continuing anyway (-SkipPreflight)"; break }
                if ($attempt -lt 3) {
                    Write-Host "  outside 0.80..1.25: something is still running or the CPU is throttled; waiting 30 s and retrying"
                    Start-Sleep -Seconds 30
                } else {
                    throw "the machine reads $machineFactor x the baseline on $calName after three tries; let it settle and rerun, or pass -SkipPreflight"
                }
            }
        }
    }
    if ($machineFactor) { $contextArgs += @("--context", "machine_factor=$machineFactor") }

    $launches = $families.Count * $Processes * $(if ($useReference) { 2 } else { 1 })
    Write-Host "Running $($families.Count) families x $Processes processes$(if ($useReference) { ' x 2 binaries (reference, current)' }), $Repetitions repetitions each ($launches launches), pinned to $(Format-Mask $affinityMask) (scaling family unpinned)"
    $index = 0
    foreach ($family in $families) {
        [uint64]$mask = $affinityMask
        # The scaling family measures parallelism up to 16 threads; one CPU would measure
        # nothing, and packing its 16 threads onto the performance cores' 16 logical CPUs
        # hung it in 2 of 20 launches, both binaries (docs/performance.md, Tier B).
        if ($family -match 'Scaling') { $mask = 0 }
        for ($p = 0; $p -lt $Processes; $p++) {
            if ($useReference) {
                $index++
                $part = Join-Path $resultsDir "$stamp-$sha-ref-part$index.json"
                Invoke-Bench $refExe $family $part $mask
                $refParts += $part
            }
            $index++
            $part = Join-Path $resultsDir "$stamp-$sha-part$index.json"
            Invoke-Bench $exe $family $part $mask
            $parts += $part
        }
    }

    # Tier D: scattershot end to end through bitfs-turn (docs/performance.md), pinned to the
    # performance cores (one thread per core for the deterministic run) so that the hybrid
    # scheduler cannot hand a run a different mix of cores each time. Output directories are
    # under perf\results.
    if (-not $NoTierD -and -not $Filter -and $Dll -and $M64) {
        if (-not (Test-Path $turn)) { throw "bitfs-turn.exe not found at $turn (needed for Tier D; pass -NoTierD to skip)" }
        $useRefTurn = $useReference -and (Test-Path $refTurn)
        if ($useReference -and -not $useRefTurn) { Write-Host "note: no bitfs-turn.exe in $Reference; Tier D gates against the committed baseline" }
        $tierD = @()
        $tierDRef = @()
        $specs = @(
            @{ Name = 'TierD_Deterministic'; Config = 'tierd-deterministic.json'; Exact = $true; OnePerCore = $true },
            @{ Name = 'TierD_Throughput'; Config = 'tierd-throughput.json'; Exact = $false; OnePerCore = $false }
        )
        foreach ($spec in $specs) {
            $configPath = Join-Path $root ("perf\" + $spec.Config)
            $threads = [int](Get-Content $configPath -Raw | ConvertFrom-Json).resources.threads
            $missing = @(0..($threads - 1) | Where-Object { -not (Test-Path (Join-Path $root ("res\sm64_jp_{0}.dll" -f $_))) })
            if ($missing.Count -gt 0) {
                Write-Host "Tier D $($spec.Name) skipped: res\sm64_jp_N.dll missing for N = $($missing -join ', ')"
                continue
            }
            [uint64]$mask = Get-TierDAffinity $topology $threads $spec.OnePerCore
            $pinText = 'unpinned'
            if ($mask -ne 0) { $pinText = "pinned to $(Format-Mask $mask) (one thread per performance core)" }
            $contextArgs += @("--context", "$($spec.Name)_affinity=$(Format-Mask $mask)")
            $binaries = @()
            if ($useRefTurn) { $binaries += @{ Exe = $refTurn; Tag = 'ref' } }
            $binaries += @{ Exe = $turn; Tag = 'cur' }
            $best = @{}
            for ($a = 1; $a -le $Alternations; $a++) {
                foreach ($binary in $binaries) {
                    Write-Host "Tier D $($spec.Name) [$($binary.Tag) $a/$Alternations]: $($binary.Exe) --config $configPath, $threads threads, $pinText"
                    $log = Join-Path $resultsDir "$stamp-$sha-$($spec.Name)-$($binary.Tag)$a.log"
                    $run = Invoke-TierD $binary.Exe $configPath $log $mask
                    $row = ConvertFrom-TierDOutput -Name $spec.Name -Lines $run.Lines -Exact $spec.Exact
                    $row.peakResidentMB = $run.PeakMB
                    if (-not $best.ContainsKey($binary.Tag) -or $row.real_time -lt $best[$binary.Tag].real_time) {
                        $best[$binary.Tag] = $row
                    } else {
                        $best[$binary.Tag].peakResidentMB = [math]::Max($best[$binary.Tag].peakResidentMB, $run.PeakMB)
                    }
                }
            }
            $tierD += $best['cur']
            if ($best.ContainsKey('ref')) { $tierDRef += $best['ref'] }
        }
        if ($tierD.Count -gt 0) {
            $index++
            $part = Join-Path $resultsDir "$stamp-$sha-part$index.json"
            $doc = [ordered]@{ context = [ordered]@{ tier = 'D' }; benchmarks = @($tierD) }
            $doc | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $part
            $parts += $part
        }
        if ($tierDRef.Count -gt 0) {
            $index++
            $part = Join-Path $resultsDir "$stamp-$sha-ref-part$index.json"
            $doc = [ordered]@{ context = [ordered]@{ tier = 'D' }; benchmarks = @($tierDRef) }
            $doc | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $part
            $refParts += $part
        }
    } elseif (-not $NoTierD -and -not $Filter) {
        Write-Host "Tier D skipped (no DLL/movie found)"
    }
} finally {
    $ErrorActionPreference = $prevEap
    if ($switchedScheme -and $previousScheme) {
        & powercfg /setactive $previousScheme 2>&1 | Out-Null
        Write-Host "Power plan restored: $(Get-PowerSchemeName $previousScheme)"
    }
}

if (-not $python) {
    Write-Host "python not found; leaving per-family results in $resultsDir and skipping merge/compare."
    exit 0
}

& $python.Source $compareScript merge -o $out @contextArgs @parts
if ($LASTEXITCODE -ne 0) { throw "merge failed" }
Remove-Item $parts -Force
Write-Host "Results written to $out"
$haveRef = $false
if ($refParts.Count -gt 0) {
    & $python.Source $compareScript merge -o $refOut --context "tasfw_sha=$referenceSha" @refParts
    if ($LASTEXITCODE -ne 0) { throw "merge of the reference results failed" }
    Remove-Item $refParts -Force
    Write-Host "Reference results written to $refOut"
    $haveRef = $true
}

if ($SaveBaseline) {
    New-Item -ItemType Directory -Force $baselineDir | Out-Null
    Copy-Item $out $Baseline -Force
    Write-Host "Saved baseline to $Baseline"
    New-Item -ItemType Directory -Force $Reference | Out-Null
    Copy-Item $exe $Reference -Force
    if (Test-Path $turn) { Copy-Item $turn $Reference -Force }
    @{ sha = $sha; date = (Get-Date -Format s); compiler = $Compiler; source = (Join-Path $buildDir 'out') } | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $Reference 'reference.json')
    Write-Host "Saved the current binaries as the reference in $Reference"
    exit 0
}

if (-not (Test-Path $Baseline)) {
    Write-Host "No baseline at $Baseline. Run with -SaveBaseline to create one."
    exit 0
}

$compareArgs = @('compare', $Baseline, $out, '--threshold', $Threshold)
if ($haveRef) { $compareArgs += @('--reference', $refOut) }
& $python.Source $compareScript @compareArgs
exit $LASTEXITCODE
