<#
.SYNOPSIS
    Configure and build sm64-tas-scripting with Ninja + MSVC.

.DESCRIPTION
    Locates Visual Studio via vswhere, imports the x64 developer environment, and uses the
    cmake/ninja bundled with Visual Studio when they are not already on PATH. Builds into
    build\<Config>. Reuses FetchContent sources from build\_deps when present so the build
    works offline.

.PARAMETER Config
    Debug (default), Release or RelWithDebInfo.

.PARAMETER Clean
    Delete build\<Config> before configuring.

.PARAMETER Target
    Optional CMake target to build instead of everything.

.PARAMETER Compiler
    msvc (default) or clang. 'clang' uses the clang-cl shipped with Visual Studio's
    "C++ Clang tools for Windows" component and builds into build\<Config>-clang.
    Both compilers must build clean; see docs/compilers.md.

.PARAMETER KeepGoing
    Pass -k 0 to ninja so every error in the tree is reported, not just the first.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release -Clean
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release -Compiler clang
#>
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Debug',
    [switch]$Clean,
    [string]$Target = '',
    [ValidateSet('msvc', 'clang')]
    [string]$Compiler = 'msvc',
    [switch]$KeepGoing
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $root "build\$Config"
if ($Compiler -eq 'clang') { $binDir = "$binDir-clang" }

# --- Locate Visual Studio ------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio 2022 with the 'Desktop development with C++' workload."
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'No Visual Studio installation with the C++ toolset was found.'
}

# --- Import the developer environment into this process -----------------------------------
$vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at '$vsDevCmd'."
}
$envDump = cmd.exe /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 -no_logo && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
    }
}

# --- Prefer the cmake/ninja bundled with Visual Studio when none are on PATH ---------------
$vsCMakeDir = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $env:PATH = (Join-Path $vsCMakeDir 'CMake\bin') + ';' + $env:PATH
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $env:PATH = (Join-Path $vsCMakeDir 'Ninja') + ';' + $env:PATH
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'cmake not found on PATH and not bundled with this Visual Studio install.'
}

# --- Compiler selection ---------------------------------------------------------------------
$compilerArgs = @()
if ($Compiler -eq 'clang') {
    $llvmBin = Join-Path $vsPath 'VC\Tools\Llvm\x64\bin'
    if (-not (Test-Path (Join-Path $llvmBin 'clang-cl.exe'))) {
        throw "clang-cl.exe not found under $llvmBin. Install the 'C++ Clang tools for Windows' component in the Visual Studio Installer."
    }
    $env:PATH = $llvmBin + ';' + $env:PATH
    $compilerArgs = @('-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl')
}

# --- Configure ------------------------------------------------------------------------------
if ($Clean -and (Test-Path $binDir)) {
    Write-Host "Removing $binDir"
    Remove-Item -Recurse -Force $binDir
}

$configureArgs = @('-S', $root, '-B', $binDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Config") + $compilerArgs

# Reuse already-downloaded dependency sources (offline builds). Harmless if absent.
# Any build dir under build\ that has already fetched a dependency is a valid source.
foreach ($dep in @(@('json', 'JSON'), @('ranges-v3', 'RANGES-V3'), @('benchmark', 'BENCHMARK'))) {
    $srcName = $dep[0] + '-src'
    $candidates = @(Get-ChildItem -Path (Join-Path $root 'build') -Directory -Filter $srcName -Recurse -Depth 2 -ErrorAction SilentlyContinue)
    if ($candidates.Count -gt 0) {
        $configureArgs += ('-DFETCHCONTENT_SOURCE_DIR_' + $dep[1] + '=' + $candidates[0].FullName)
    }
}

Write-Host "Configuring ($Config, $Compiler) in $binDir"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

# --- Build ----------------------------------------------------------------------------------
$buildArgs = @('--build', $binDir)
if ($Target) { $buildArgs += @('--target', $Target) }
if ($KeepGoing) { $buildArgs += @('--', '-k', '0') }

& cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

$exe = Join-Path $binDir 'out\bitfs-turn.exe'
if (Test-Path $exe) {
    Write-Host "Built $exe"
} else {
    Write-Host "Build finished (target '$Target')."
}
