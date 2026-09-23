[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Root
)

$ErrorActionPreference = "Stop"
$root = [System.IO.Path]::GetFullPath($Root)
$sourceDir = Join-Path $root "src"
$featuresDir = Join-Path $sourceDir "Features"
$uiDir = Join-Path $sourceDir "UI"
$includePaths = @(
    $sourceDir,
    (Join-Path $sourceDir "Core"),
    (Join-Path $sourceDir "Platform"),
    $uiDir
)

function Test-UnderDirectory([string]$Path, [string]$Directory) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullDirectory = [System.IO.Path]::GetFullPath($Directory)
    return $fullPath.Equals($fullDirectory, [System.StringComparison]::OrdinalIgnoreCase) -or
           $fullPath.StartsWith($fullDirectory + [System.IO.Path]::DirectorySeparatorChar,
                                [System.StringComparison]::OrdinalIgnoreCase)
}

function Resolve-Include([string]$IncluderDirectory, [string]$Included, [bool]$Quoted) {
    $normalized = $Included -replace '/', '\'
    $searchPaths = @()
    if ($Quoted) {
        $searchPaths += $IncluderDirectory
    }
    $searchPaths += $includePaths

    foreach ($searchPath in $searchPaths) {
        $candidate = [System.IO.Path]::GetFullPath((Join-Path $searchPath $normalized))
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    return $null
}

$leaks = @(
    Get-ChildItem $featuresDir -Recurse -File -Include *.hpp, *.cpp | ForEach-Object {
        $relative = $_.FullName.Substring($featuresDir.Length + 1)
        $slice = ($relative -split '[\\/]')[0]
        $sliceDir = Join-Path $featuresDir $slice
        $includerDir = $_.Directory.FullName

        Select-String -Path $_.FullName -Pattern '#include\s+(?:"([^"]+)"|<([^>]+)>)' | ForEach-Object {
            $match = $_.Matches[0]
            $quoted = $match.Groups[1].Success
            $included = if ($quoted) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            $landsAt = Resolve-Include $includerDir $included $quoted
            if (-not $landsAt) {
                return
            }

            $where = "  $($_.Path):$($_.LineNumber): Features/$slice includes $included"
            $landsInFeatures = Test-UnderDirectory $landsAt $featuresDir
            $landsInOwnSlice = Test-UnderDirectory $landsAt $sliceDir
            if ($landsInFeatures -and -not $landsInOwnSlice) {
                "$where, which resolves into another slice"
            } elseif (Test-UnderDirectory $landsAt $uiDir) {
                "$where, which lives under src/UI"
            }
        }
    }
)

if ($leaks.Count -gt 0) {
    Write-Host "layering rule violated - a feature may only include Core/ and Platform/:" -ForegroundColor Red
    $leaks | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    throw "layering rule violated"
}
