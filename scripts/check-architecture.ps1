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

function Get-Includes([string]$Directory) {
    Get-ChildItem $Directory -Recurse -File -Include *.hpp, *.cpp, *.h | ForEach-Object {
        $file = $_
        Select-String -Path $file.FullName -Pattern '#include\s+(?:"([^"]+)"|<([^>]+)>)' | ForEach-Object {
            $match = $_.Matches[0]
            $quoted = $match.Groups[1].Success
            $included = if ($quoted) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            $landsAt = Resolve-Include $file.Directory.FullName $included $quoted
            if ($landsAt) {
                [pscustomobject]@{ File = $file.FullName; Line = "  $($_.Path):$($_.LineNumber)"; Included = $included; LandsAt = $landsAt }
            }
        }
    }
}

function Get-LeaksAbove([string]$Layer, [string[]]$Above) {
    Get-Includes (Join-Path $sourceDir $Layer) | ForEach-Object {
        foreach ($other in $Above) {
            if (Test-UnderDirectory $_.LandsAt (Join-Path $sourceDir $other)) {
                "$($_.Line): $Layer includes $($_.Included), which lives under src/$other"
            }
        }
    }
}

$leaks = @(
    Get-Includes $featuresDir | ForEach-Object {
        $slice = ($_.File.Substring($featuresDir.Length + 1) -split '[\\/]')[0]
        $where = "$($_.Line): Features/$slice includes $($_.Included)"
        if ((Test-UnderDirectory $_.LandsAt $featuresDir) -and
            -not (Test-UnderDirectory $_.LandsAt (Join-Path $featuresDir $slice))) {
            "$where, which resolves into another slice"
        } elseif (Test-UnderDirectory $_.LandsAt $uiDir) {
            "$where, which lives under src/UI"
        }
    }
    Get-LeaksAbove "Platform" "Core", "Features", "UI"
    Get-LeaksAbove "Core" "Features", "UI"
    Get-LeaksAbove "UI" "Features"
)

if ($leaks.Count -gt 0) {
    Write-Host "layering rule violated - Features include only Core/ and Platform/; UI/ never Features/; Core/ never UI/ or Features/; Platform/ nothing above it:" -ForegroundColor Red
    $leaks | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    throw "layering rule violated"
}
