<#
.SYNOPSIS
    Reproduce the hang of the thread-scaling benchmark family under CPU pinning.

.DESCRIPTION
    Launches tasfw-perf's ^BM_LibSm64Scaling family repeatedly, once per configuration:
    the current build and the reference build, each pinned to the performance cores'
    logical CPUs (16 on the desktop's i9-13900K) and unpinned, at High priority as
    scripts\perf.ps1 runs them. Reports ok / crashed / hung per configuration. First seen
    2026-09-12: pinned, 1 launch in 10 of either binary hung at the 16-thread FrameAdvance
    row after one thread died with STATUS_RESOURCE_NOT_OWNED (0xC0000264, a lock released by
    a thread that did not hold it; Windows Error Reporting logs it under Application Error);
    unpinned, 0 in 20. docs/performance.md (Tier B) and ROADMAP.md carry the details. Needs
    res\sm64_jp_0.dll .. sm64_jp_16.dll and the movie, like the family itself.

.PARAMETER Runs
    Launches per configuration (default 10).

.PARAMETER TimeoutSec
    Seconds before a launch counts as hung and is killed (default 90; a launch takes 2 s).

.PARAMETER Mask
    Affinity mask for the pinned configurations (default 0xFFFF).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\perf_scaling_hang.ps1 -Runs 20
#>
param(
    [int]$Runs = 10,
    [int]$TimeoutSec = 90,
    [uint64]$Mask = 0xFFFF,
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [ValidateSet('msvc', 'clang')]
    [string]$Compiler = 'msvc'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:TASFW_LIBSM64 = Join-Path $root 'res\sm64_jp_0.dll'
$env:TASFW_M64 = Join-Path $root 'movies\bitfs-pyramid-jp.m64'
$env:TASFW_FRAME = '3330'
foreach ($needed in @($env:TASFW_LIBSM64, $env:TASFW_M64)) {
    if (-not (Test-Path $needed)) { throw "$needed not found; the scaling family needs the DLL copies and the movie" }
}
$suffix = ''
if ($Compiler -eq 'clang') { $suffix = '-clang' }
$current = Join-Path $root "build\$Config$suffix\out\tasfw-perf.exe"
$reference = Join-Path $root ("perf\reference\{0}{1}\tasfw-perf.exe" -f $env:COMPUTERNAME.ToLower(), $suffix)
$tmp = Join-Path $root 'perf\results\scaling-hang'
New-Item -ItemType Directory -Force $tmp | Out-Null

$configs = @()
foreach ($binary in @(@{ Tag = 'current'; Exe = $current }, @{ Tag = 'reference'; Exe = $reference })) {
    if (-not (Test-Path $binary.Exe)) { Write-Host "skipping $($binary.Tag): $($binary.Exe) not found"; continue }
    $configs += @{ Tag = "$($binary.Tag)-pinned"; Exe = $binary.Exe; Mask = $Mask }
    $configs += @{ Tag = "$($binary.Tag)-unpinned"; Exe = $binary.Exe; Mask = [uint64]0 }
}
foreach ($c in $configs) {
    $hangs = 0; $crashes = 0; $ok = 0; $times = @()
    for ($r = 1; $r -le $Runs; $r++) {
        $out = Join-Path $tmp "$($c.Tag)-$r.json"
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $c.Exe
        $psi.Arguments = "--benchmark_filter=^BM_LibSm64Scaling --benchmark_repetitions=3 --benchmark_display_aggregates_only=true --benchmark_out=$out --benchmark_out_format=json"
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = [Diagnostics.Process]::Start($psi)
        try {
            $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High
            if ($c.Mask -ne 0) { $p.ProcessorAffinity = [IntPtr]([int64]$c.Mask) }
        } catch {}
        $null = $p.StandardOutput.ReadToEndAsync()
        $null = $p.StandardError.ReadToEndAsync()
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            $hangs++
            $threads = $p.Threads.Count
            try { $p.Kill() } catch {}
            Write-Host ("{0} run {1}: HANG after {2} s ({3} threads left; Application Error events name the dead one)" -f $c.Tag, $r, $TimeoutSec, $threads)
        } else {
            $p.WaitForExit()
            $t = [math]::Round($sw.Elapsed.TotalSeconds, 1)
            $times += $t
            if ($p.ExitCode -ne 0) { $crashes++; Write-Host ("{0} run {1}: exit {2} after {3} s" -f $c.Tag, $r, $p.ExitCode, $t) } else { $ok++ }
        }
    }
    Write-Host ("== {0}: {1} ok, {2} crashed, {3} hung; seconds per launch {4}" -f $c.Tag, $ok, $crashes, $hangs, ($times -join ' '))
}
