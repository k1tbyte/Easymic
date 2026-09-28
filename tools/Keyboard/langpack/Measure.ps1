function Convert-Words([string]$Exe, [string[]]$Words, [string]$From, [string]$To) {
    $converted = @($Words | & $Exe convert $From $To)
    if ($LASTEXITCODE -ne 0) { throw "langpack convert failed: $LASTEXITCODE" }
    if ($converted.Count -ne $Words.Count) { throw 'Incomplete conversion output' }
    return $converted
}

function Measure-Detection(
    [string]$Exe, [string]$PackDir, [string[]]$InputLines, [string[]]$ExpectedLines,
    [string[]]$Rules, [string]$Frequency = 'on'
) {
    if (-not $InputLines.Count -or $InputLines.Count -ne $ExpectedLines.Count) {
        throw 'Input and expected counts must match and be nonzero'
    }
    $arguments = @('--packs', $PackDir, '--pair', 'en,ru', '--frequency', $Frequency)
    foreach ($rule in $Rules) { $arguments += @('--rules', $rule) }
    $output = @($InputLines | & $Exe @arguments)
    if ($LASTEXITCODE -ne 0) { throw "langpack detection failed: $LASTEXITCODE" }

    $records = [Collections.Generic.List[object]]::new()
    foreach ($line in $output) {
        if ($line -match '^  FIX -> (.*)   \[.*\]$') {
            $records.Add([pscustomobject]@{ Fixed = $Matches[1]; Text = $line; Early = $null })
        } elseif ($line -match '^  (no language matched|ok   \[.*\])$') {
            $records.Add([pscustomobject]@{ Fixed = $InputLines[$records.Count]; Text = $line; Early = $null })
        } elseif ($line -match '^  early at (\d+): (.*) -> (.*)   \[.*\]$') {
            if (-not $records.Count -or $records[-1].Early) { throw 'Misplaced early verdict' }
            $n = [int]$Matches[1]
            $inputLine = $InputLines[$records.Count - 1]
            if ($n -lt 2 -or $n -gt $inputLine.Length -or $Matches[2] -cne $inputLine.Substring(0, $n)) {
                throw "Invalid early prefix: $line"
            }
            $records[-1].Early = [pscustomobject]@{ Count = $n; Fixed = $Matches[3] }
        } elseif ($line -notmatch '^pack ') {
            throw "Unexpected detector output: $line"
        }
    }
    if ($records.Count -ne $InputLines.Count) {
        throw "Expected $($InputLines.Count) verdicts, got $($records.Count)"
    }

    for ($i = 0; $i -lt $records.Count; $i++) {
        $record = $records[$i]
        $expected = $ExpectedLines[$i]
        $endBad = $record.Fixed -cne $expected
        $earlyBad = $false
        if ($record.Early) {
            $earlyBad = $InputLines[$i] -ceq $expected -or $record.Early.Count -gt $expected.Length -or
                $record.Early.Fixed -cne $expected.Substring(0, $record.Early.Count)
        }
        [pscustomobject]@{
            EndBad = [int]$endBad
            MidBad = [int]$(if ($record.Early) { $earlyBad } else { $endBad })
            EarlyBad = [int]$earlyBad
            Text = $record.Text
        }
    }
}
