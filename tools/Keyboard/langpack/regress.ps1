param(
    [string]$LangpackExe = (Join-Path $PSScriptRoot "..\..\..\cmake-build-minsizerel\langpack.exe"),
    [string]$PackDir = (Join-Path $PSScriptRoot "..\..\..\cmake-build-minsizerel\packs"),
    [string[]]$Rules,
    [ValidateSet('on', 'off')] [string]$Frequency = 'on',
    [string]$CasesTsv = (Join-Path $PSScriptRoot "..\..\..\tests\Keyboard\AutocorrectCases.tsv"),
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Measure.ps1')
foreach ($path in @($LangpackExe, $PackDir, $CasesTsv)) {
    if (-not (Test-Path $path)) { throw "Not found: $path" }
}
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Text.UTF8Encoding]::new($false)
$cases = @(Import-Csv $CasesTsv -Delimiter "`t" -Encoding UTF8)
if (-not $cases.Count -or @($cases | Where-Object { -not $_.Group -or -not $_.Input -or -not $_.Expected }).Count) {
    throw 'Cases require nonempty Group, Input and Expected columns'
}

$cyrillic = '[{0}-{1}]' -f [char]0x400, [char]0x4ff
foreach ($from in @('en', 'ru')) {
    $recovery = @($cases | Where-Object {
        $_.Input -cne $_.Expected -and ($_.Input -match $cyrillic) -eq ($from -eq 'ru')
    })
    if (-not $recovery.Count) { continue }
    $to = if ($from -eq 'en') { 'ru' } else { 'en' }
    $converted = @(Convert-Words $LangpackExe $recovery.Input $from $to)
    for ($i = 0; $i -lt $recovery.Count; $i++) {
        if ($converted[$i] -cne $recovery[$i].Expected) { throw "Invalid recovery fixture: $($recovery[$i].Input)" }
    }
}

$measured = @(Measure-Detection $LangpackExe $PackDir $cases.Input $cases.Expected $Rules $Frequency)
$results = for ($i = 0; $i -lt $cases.Count; $i++) {
    $case = $cases[$i]
    $result = $measured[$i]
    if ($result.EndBad -or $result.EarlyBad) {
        Write-Host "BAD $($case.Group) $($case.Input) end=$($result.EndBad) early=$($result.EarlyBad): $($result.Text)"
    }
    [pscustomobject]@{
        Group = $case.Group
        EndBad = $result.EndBad
        MidBad = $result.MidBad
        EarlyBad = $result.EarlyBad
    }
}
$results | Group-Object Group | ForEach-Object {
    [pscustomobject]@{
        Group = $_.Name
        Count = $_.Count
        EndBad = ($_.Group | Measure-Object EndBad -Sum).Sum
        MidBad = ($_.Group | Measure-Object MidBad -Sum).Sum
        EarlyBad = ($_.Group | Measure-Object EarlyBad -Sum).Sum
    }
} | Format-Table -AutoSize
if ($Strict -and @($results | Where-Object { $_.EndBad -or $_.MidBad }).Count) {
    throw 'Regression corpus has errors'
}
