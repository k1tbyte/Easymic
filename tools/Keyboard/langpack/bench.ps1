[CmdletBinding()]
param(
    [string]$LangpackExe = (Join-Path $PSScriptRoot "..\..\..\cmake-build-minsizerel\langpack.exe"),
    [string]$PackDir = (Join-Path $PSScriptRoot "..\..\..\cmake-build-minsizerel\packs"),
    [string]$EnglishList = (Join-Path $PSScriptRoot "..\..\..\cmake-build-minsizerel\tools\Keyboard\langpack\en_words.txt"),
    [string]$RussianList = (Join-Path $PSScriptRoot "..\..\..\cmake-build-minsizerel\tools\Keyboard\langpack\ru_words.txt"),
    [ValidateRange(1, 2147483647)] [int]$WordCount = 3000,
    [ValidateRange(2, 2147483647)] [int]$ShortCount = 800,
    [int]$Seed = 42,
    [string[]]$Rules,
    [ValidateSet('on', 'off')] [string]$Frequency = 'on'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Measure.ps1')
foreach ($path in @($LangpackExe, $PackDir, $EnglishList, $RussianList)) {
    if (-not (Test-Path $path)) { throw "Not found: $path" }
}
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Text.UTF8Encoding]::new($false)

function Clean-Word([string]$line) {
    $word = $line.Trim() -replace '/.*', ''
    if ($word -match '^\S+$') { return $word }
}

function Sample-From([string[]]$pool, [int]$count, [int]$seed) {
    if (-not $pool.Count) { throw 'Empty sample pool' }
    $rng = [System.Random]::new($seed)
    for ($i = 0; $i -lt $count; $i++) { $pool[$rng.Next($pool.Count)] }
}

function Run-Set([string]$label, [string[]]$inputLines, [string[]]$expectedLines) {
    $results = @(Measure-Detection $LangpackExe $PackDir $inputLines $expectedLines $Rules $Frequency)
    [pscustomobject]@{
        Set = $label
        Lines = $results.Count
        EndBad = ($results | Measure-Object EndBad -Sum).Sum
        MidBad = ($results | Measure-Object MidBad -Sum).Sum
    }
}

$allEn = @(Get-Content $EnglishList -Encoding UTF8 | ForEach-Object { Clean-Word $_ } | Where-Object { $_ })
$allRu = @(Get-Content $RussianList -Encoding UTF8 | ForEach-Object { Clean-Word $_ } | Where-Object { $_ })
Write-Host "Loaded $($allEn.Count) en words, $($allRu.Count) ru words"

$enWords = @(Sample-From $allEn $WordCount $Seed)
$ruWords = @(Sample-From $allRu $WordCount $Seed)

$rng = [System.Random]::new($Seed + 1)
$mixed = @(for ($i = 0; $i -lt $WordCount; $i++) {
    if ($rng.Next(2) -eq 0) { $enWords[$i] } else { $ruWords[$i] }
})

$shortPoolEn = @($allEn | Where-Object { $_.Length -le 2 })
$shortPoolRu = @($allRu | Where-Object { $_.Length -le 2 })
Write-Host "Short pool: $($shortPoolEn.Count) en, $($shortPoolRu.Count) ru"
$half = [int][Math]::Floor($ShortCount / 2)
$shortEn = @(Sample-From $shortPoolEn $half ($Seed + 2))
$shortRu = @(Sample-From $shortPoolRu ($ShortCount - $half) ($Seed + 3))
$rng2 = [System.Random]::new($Seed + 4)
$shortAll = @($shortEn + $shortRu | Sort-Object { $rng2.Next() })

$ruInEnText = @(Convert-Words $LangpackExe $ruWords ru en)
$enInRuText = @(Convert-Words $LangpackExe $enWords en ru)

$results = @(
    Run-Set 'correct en' $enWords $enWords
    Run-Set 'correct ru' $ruWords $ruWords
    Run-Set 'en+ru mixed' $mixed $mixed
    Run-Set 'short tokens mixed' $shortAll $shortAll
    Run-Set 'ru typed in en' $ruInEnText $ruWords
    Run-Set 'en typed in ru' $enInRuText $enWords
)
$results | Format-Table -AutoSize
