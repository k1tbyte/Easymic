$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\tools\Keyboard\langpack\Measure.ps1')

$script:exitCode = 0
$script:reply = @()
function Invoke-TestDetector {
    $global:LASTEXITCODE = $script:exitCode
    $script:reply
}
function Check([bool]$ok, [string]$name) {
    if (-not $ok) { throw "FAILED: $name" }
}
function Measure-Case([string]$expected = 'abc') {
    Measure-Detection 'Invoke-TestDetector' '' @('abc') @($expected) @() 'on'
}
function Must-Fail([scriptblock]$test, [string]$message) {
    try { & $test | Out-Null } catch {
        if ($_.Exception.Message -like $message) { return }
        throw
    }
    throw "Expected failure: $message"
}

$script:reply = @('pack en', '  ok   [en, dict]', '  early at 2: ab -> xy   [dict]')
$result = Measure-Case
Check ($result.EndBad -eq 0 -and $result.MidBad -eq 1 -and $result.EarlyBad -eq 1) 'early false switch'

$script:reply = @('  FIX -> wrong   [dict]', '  early at 2: ab -> xy   [dict]')
$result = Measure-Case 'xyz'
Check ($result.EndBad -eq 1 -and $result.MidBad -eq 0) 'exact early recovery, wrong end recovery'
$script:reply = @('  FIX -> xyz   [dict]')
$result = Measure-Case 'xyz'
Check ($result.EndBad -eq 0 -and $result.MidBad -eq 0) 'exact end recovery'

$script:exitCode = 1
Must-Fail { Measure-Case } 'langpack detection failed:*'
$script:exitCode = 0
$script:reply = @('pack en')
Must-Fail { Measure-Case } 'Expected 1 verdicts, got 0'
$script:reply = @('  ok malformed')
Must-Fail { Measure-Case } 'Unexpected detector output:*'
$script:reply = @('  ok   [dict]', '  early at 4: abcd -> wxyz   [dict]')
Must-Fail { Measure-Case } 'Invalid early prefix:*'
$script:reply = @('  ok   [dict]', '  early at 2: ab -> xy   [dict]', '  early at 2: ab -> xy   [dict]')
Must-Fail { Measure-Case } 'Misplaced early verdict'
Must-Fail { Measure-Detection 'Invoke-TestDetector' '' @('abc') @('abc', 'def') @() } 'Input and expected counts*'
$script:reply = @('abc')
Must-Fail { Convert-Words 'Invoke-TestDetector' @('abc', 'def') en ru } 'Incomplete conversion output'

Write-Host 'detection benchmark checks passed'
