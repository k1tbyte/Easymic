<#
.SYNOPSIS
    Builds EasyLauncher with MSVC + Ninja from any shell.
.DESCRIPTION
    cl.exe never lands in the global PATH, so the MSVC environment is imported
    from vcvars64.bat (located via vswhere) before configuring.
.EXAMPLE
    .\build.ps1                          # MinSizeRel
    .\build.ps1 -Config Debug -Run
    .\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "MinSizeRel", "RelWithDebInfo")]
    [string]$Config = "MinSizeRel",
    [switch]$Clean,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = Join-Path $root "cmake-build-$($Config.ToLower())"

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item $buildDir -Recurse -Force
}

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found - install Visual Studio with the C++ workload" }

    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "No VS installation with MSVC x64 tools found" }

    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }
}

# The slice rule, docs/ARCHITECTURE.md section 3: no file under Features/X may include
# Features/Y. Only Core/ and Platform/ are shared, and anything two features both need is a
# Core/ concern. It is checked here rather than trusted because it is the one rule that keeps
# the design from rotting back into the old catch-all Lib/.
$featuresDir = Join-Path $root "src\Features"
$leaks = Get-ChildItem $featuresDir -Recurse -File -Include *.hpp, *.cpp | ForEach-Object {
    $slice = ($_.FullName.Substring($featuresDir.Length + 1) -split '[\\/]')[0]
    Select-String -Path $_.FullName -Pattern '#include\s+"Features/([^/"]+)/' | ForEach-Object {
        $target = $_.Matches[0].Groups[1].Value
        if ($target -ne $slice) {
            "  $($_.Path):$($_.LineNumber): Features/$slice includes Features/$target"
        }
    }
}

if ($leaks) {
    Write-Host "slice rule violated - a feature may only include Core/ and Platform/:" -ForegroundColor Red
    $leaks | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    throw "slice rule violated"
}

# Quotes are required: PS 5.1 does not expand variables in unquoted "-D...=" arguments
cmake -S "$root" -B "$buildDir" -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$exe = Join-Path $buildDir "EasyLauncher.exe"
Write-Host "`n$exe  ($([math]::Round((Get-Item $exe).Length / 1KB)) KB)" -ForegroundColor Green

if ($Run) { & $exe }
