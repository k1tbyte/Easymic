<#
.SYNOPSIS
    Reproduces the puntish README benchmark table for the en/ru pair.
.PARAMETER LangpackExe
    Path to langpack.exe. Default: cmake-build-minsizerel/langpack.exe
.PARAMETER DictDir
    Path to the dictionary directory. Default: D:\Repositories\Clion\puntish\data
.PARAMETER PackDir
    Path to the pack directory. Default: D:\Repositories\Clion\puntish\packs
.PARAMETER WordCount
    Words per set. Default: 3000
.PARAMETER ShortCount
    Short tokens per set. Default: 800
.PARAMETER Seed
    Random seed. Default: 42
#>
[CmdletBinding()]
param(
    [string]$LangpackExe = (Join-Path $PSScriptRoot "..\..\cmake-build-minsizerel\langpack.exe"),
    [string]$DictDir = "D:\Repositories\Clion\puntish\data",
    [string]$PackDir = "D:\Repositories\Clion\puntish\packs",
    [int]$WordCount = 3000,
    [int]$ShortCount = 800,
    [int]$Seed = 42
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $LangpackExe)) { throw "langpack.exe not found: $LangpackExe" }
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$tmp = $env:TEMP

function Clean-Word([string]$line) {
    $w = $line.Trim() -replace '/.*', ''
    if ($w -match '^\S+$') { return $w }
    return $null
}

function Sample-From([string[]]$pool, [int]$count, [int]$seed) {
    $rng = [System.Random]::new($seed)
    $out = @()
    for ($i = 0; $i -lt $count; $i++) {
        $out += $pool[$rng.Next($pool.Count)]
    }
    return $out
}

function Run-Set([string]$label, [string]$inputFile, [string]$expectFix) {
    $lines = Get-Content $inputFile -Encoding UTF8 | Where-Object { $_.Trim().Length -gt 0 }
    $total = $lines.Count

    $result = $lines | & $LangpackExe --packs $PackDir --pair en,ru
    $fixes = @($result | Where-Object { $_ -match '^\s+FIX' }).Count
    $oks = @($result | Where-Object { $_ -match '^\s+ok' }).Count

    if ($expectFix -eq "none") {
        $bad = $fixes
        $metric = "false switches"
    } else {
        $bad = $total - $fixes
        $metric = "missed"
    }
    [PSCustomObject]@{
        Set = $label
        Lines = $total
        Bad = $bad
        Metric = $metric
    }
}

# Load and clean all words
$allEn = @(Get-Content (Join-Path $DictDir "dict_en.txt") -Encoding UTF8 |
    ForEach-Object { Clean-Word $_ } | Where-Object { $_ })
$allRu = @(Get-Content (Join-Path $DictDir "dict_ru.txt") -Encoding UTF8 |
    ForEach-Object { Clean-Word $_ } | Where-Object { $_ })

Write-Host "Loaded $($allEn.Count) en words, $($allRu.Count) ru words" -ForegroundColor Gray

# Sample sets
$enWords = Sample-From $allEn $WordCount $Seed
$ruWords = Sample-From $allRu $WordCount $Seed

$enFile = Join-Path $tmp "bench_en.txt"
$ruFile = Join-Path $tmp "bench_ru.txt"
[System.IO.File]::WriteAllLines($enFile, $enWords, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllLines($ruFile, $ruWords, [System.Text.UTF8Encoding]::new($false))

# Mixed en+ru
$rng = [System.Random]::new($Seed + 1)
$mixed = @()
for ($i = 0; $i -lt $WordCount; $i++) {
    if ($rng.Next(2) -eq 0) { $mixed += $enWords[$i % $enWords.Count] }
    else { $mixed += $ruWords[$i % $ruWords.Count] }
}
$mixedFile = Join-Path $tmp "bench_mixed.txt"
[System.IO.File]::WriteAllLines($mixedFile, $mixed, [System.Text.UTF8Encoding]::new($false))

# Short tokens (1-2 letters) - filter first, then sample
$shortPoolEn = @($allEn | Where-Object { $_.Length -le 2 })
$shortPoolRu = @($allRu | Where-Object { $_.Length -le 2 })
Write-Host "Short pool: $($shortPoolEn.Count) en, $($shortPoolRu.Count) ru" -ForegroundColor Gray

$half = [int]($ShortCount / 2)
$shortEn = Sample-From $shortPoolEn $half ($Seed + 2)
$shortRu = Sample-From $shortPoolRu $half ($Seed + 3)
$shortAll = @()
$shortAll += $shortEn
$shortAll += $shortRu
$rng2 = [System.Random]::new($Seed + 4)
$shortAll = $shortAll | Sort-Object { $rng2.Next() }
$shortFile = Join-Path $tmp "bench_short.txt"
[System.IO.File]::WriteAllLines($shortFile, $shortAll, [System.Text.UTF8Encoding]::new($false))

# ru typed in en layout
$ruInEnFile = Join-Path $tmp "bench_ru_in_en.txt"
$ruInEnText = Get-Content $ruFile -Encoding UTF8 | & $LangpackExe convert ru en
[System.IO.File]::WriteAllLines($ruInEnFile, $ruInEnText, [System.Text.UTF8Encoding]::new($false))

# en typed in ru layout
$enInRuFile = Join-Path $tmp "bench_en_in_ru.txt"
$enInRuText = Get-Content $enFile -Encoding UTF8 | & $LangpackExe convert en ru
[System.IO.File]::WriteAllLines($enInRuFile, $enInRuText, [System.Text.UTF8Encoding]::new($false))

# Run sets
$results = @()
$results += Run-Set "correct en" $enFile "none"
$results += Run-Set "correct ru" $ruFile "none"
$results += Run-Set "en+ru mixed" $mixedFile "none"
$results += Run-Set "short tokens mixed" $shortFile "none"
$results += Run-Set "ru typed in en" $ruInEnFile "fix"
$results += Run-Set "en typed in ru" $enInRuFile "fix"

Write-Host ""
Write-Host ("{0,-30} {1,8} {2,8} {3}" -f "Set", "Lines", "Bad", "Metric") -ForegroundColor Cyan
foreach ($r in $results) {
    $color = if ($r.Bad -eq 0) { "Green" } else { "Yellow" }
    Write-Host ("{0,-30} {1,8} {2,8} {3}" -f $r.Set, $r.Lines, $r.Bad, $r.Metric) -ForegroundColor $color
}
