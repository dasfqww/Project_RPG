$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TrainingGuideArtifacts.ps1')
$root=Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'Saved/BuildChecks/TrainingGuideViewport-20261030'
$fixture=Join-Path $root 'CleanupFixture'
$temp=Join-Path $fixture 'Temp'
$protected=Join-Path $fixture 'Protected'
$lock=$null
try {
    New-Item -ItemType Directory -Path $temp,$protected -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $protected 'sentinel'),'keep')
    [IO.File]::WriteAllText((Join-Path $temp 'owned.obj'),'discard')
    New-Item -ItemType Junction -Path (Join-Path $temp 'Content') -Target $protected | Out-Null
    $rejected=$false
    try {Remove-RPGTrainingGuideTemporaryTree -Path $protected -OwnedParent $temp | Out-Null} catch {$rejected=$true}
    if (!$rejected) {throw 'Outside path accepted'}
    $rejected=$false
    try {Remove-RPGTrainingGuideTemporaryTree -Path (Join-Path $temp 'Content/sentinel') -OwnedParent $fixture | Out-Null} catch {$rejected=$true}
    if (!$rejected) {throw 'Reparse ancestor accepted'}
    $lock=[IO.File]::Open((Join-Path $temp 'owned.obj'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    $rejected=$false
    try {Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $fixture | Out-Null} catch {$rejected=$true}
    if (!$rejected -or !(Test-Path -LiteralPath (Join-Path $temp 'owned.obj'))) {throw 'Locked-file preflight failed'}
    $lock.Dispose(); $lock=$null
    $result=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $fixture
    if (!$result.absent -or $result.deletedBytes -ne 7 -or $result.unlinked -ne 1 -or [IO.File]::ReadAllText((Join-Path $protected 'sentinel')) -ne 'keep') {throw 'Cleanup followed a junction or counted wrong bytes'}
    $missing=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $fixture
    if (!$missing.absent -or $missing.deletedBytes -ne 0) {throw 'Missing target not idempotent'}
    Write-Output 'PASS: outside/reparse traversal rejected; locked-file preflight; junction target preserved; exact bytes; idempotent cleanup'
} finally {
    if ($lock) {$lock.Dispose()}
    Remove-RPGTrainingGuideTemporaryTree -Path $fixture -OwnedParent $root | Out-Null
}
