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
    .\build.ps1 -Test                    # also builds and runs the harnesses
    .\build.ps1 -Tools                   # also builds the langpack tool
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "MinSizeRel", "RelWithDebInfo")]
    [string]$Config = "MinSizeRel",
    [switch]$Clean,
    [switch]$Run,
    [switch]$Test,
    [switch]$Tools
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

& (Join-Path $root "scripts\check-architecture.ps1") -Root $root

# Quotes are required: PS 5.1 does not expand variables in unquoted "-D...=" arguments
cmake -S "$root" -B "$buildDir" -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$exe = Join-Path $buildDir "EasyLauncher.exe"
Write-Host "`n$exe  ($([math]::Round((Get-Item $exe).Length / 1KB)) KB)" -ForegroundColor Green

if ($Test) {
    cmake --build $buildDir --target RouterTest KeyboardConfigTest AutocorrectTest UserRulesTest KeyboardUiTest
    if ($LASTEXITCODE -ne 0) { throw "test build failed" }
    & (Join-Path $buildDir "RouterTest.exe")
    if ($LASTEXITCODE -ne 0) { throw "router tests failed" }
    & (Join-Path $buildDir "KeyboardConfigTest.exe")
    if ($LASTEXITCODE -ne 0) { throw "keyboard config tests failed" }
    & (Join-Path $buildDir "AutocorrectTest.exe")
    if ($LASTEXITCODE -ne 0) { throw "autocorrect tests failed" }
    & (Join-Path $buildDir "UserRulesTest.exe")
    if ($LASTEXITCODE -ne 0) { throw "user rules tests failed" }
    & (Join-Path $buildDir "KeyboardUiTest.exe")
    if ($LASTEXITCODE -ne 0) { throw "keyboard UI tests failed" }
    & (Join-Path $root "tests\Keyboard\DetectionBenchTest.ps1")
}

if ($Tools) {
    cmake --build $buildDir --target langpack
    if ($LASTEXITCODE -ne 0) { throw "langpack build failed" }
    Write-Host "`n$(Join-Path $buildDir 'langpack.exe')  ($([math]::Round((Get-Item (Join-Path $buildDir 'langpack.exe')).Length / 1KB)) KB)" -ForegroundColor Green
}

if ($Run) { & $exe }
