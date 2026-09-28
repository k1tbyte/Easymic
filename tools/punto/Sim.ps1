# Punto Switcher rule emulator: decodes ps.dat and scores its rules on a corpus.
# .\tools\punto\Sim.ps1 -PsDat <Punto>\Data\ps.dat [-BenchDir <bench.ps1 output dir>]
param(
    [Parameter(Mandatory)] [string]$PsDat,
    [string]$Corpus,
    [string]$BenchDir
)
$ErrorActionPreference = 'Stop'
if (-not $Corpus) { $Corpus = Join-Path $PSScriptRoot 'corpus.txt' }
Add-Type -TypeDefinition (Get-Content (Join-Path $PSScriptRoot 'PuntoSim.cs') -Raw -Encoding UTF8)

# XOR 0xAA on every byte but CR and LF, then CP1251
$bytes = [IO.File]::ReadAllBytes($PsDat)
for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($bytes[$i] -ne 0x0D -and $bytes[$i] -ne 0x0A) { $bytes[$i] = $bytes[$i] -bxor 0xAA }
}
$rules = Join-Path $env:TEMP 'punto-ps.txt'
[IO.File]::WriteAllText($rules, [Text.Encoding]::GetEncoding(1251).GetString($bytes), (New-Object Text.UTF8Encoding($false)))
"rules: $([PuntoSim]::Load($rules))  (decoded to $rules)"

$tokens = [string[]]@((Get-Content $Corpus -Raw -Encoding UTF8) -split '\s+' | Where-Object { $_ -match '\p{L}' })
[PuntoSim]::Eval('corpus, own layout', $tokens, $false, 40)
[PuntoSim]::Eval('corpus, wrong layout', [string[]]@($tokens | ForEach-Object { [PuntoSim]::Convert($_) }), $true, 40)

if ($BenchDir) {
    function Words($name) {
        , [string[]]@([IO.File]::ReadAllLines((Join-Path $BenchDir $name), [Text.Encoding]::UTF8) | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    }
    [PuntoSim]::Eval('bench en', (Words 'bench_en.txt'), $false, 15)
    [PuntoSim]::Eval('bench ru', (Words 'bench_ru.txt'), $false, 15)
    [PuntoSim]::Eval('bench ru typed in en', (Words 'bench_ru_in_en.txt'), $true, 15)
    [PuntoSim]::Eval('bench en typed in ru', (Words 'bench_en_in_ru.txt'), $true, 15)
}
