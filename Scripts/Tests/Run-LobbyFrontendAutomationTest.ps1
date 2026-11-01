[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$files = @(
    'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyWidget.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyCharacters.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyLayout.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyNextStep.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyActionAvailability.cpp',
    'Source/Project_RPG/Public/Manager/HttpWebManager.h',
    'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
    'Source/Project_RPG/Private/Manager/HttpWebManagerCharacterCreation.cpp',
    'Source/Project_RPG/Private/Online/RPGBackendCharacter.cpp',
    'Source/Project_RPG/Private/Online/RPGOnlineLobbySubsystem.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyNextStepTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyActionAvailabilityTests.cpp',
    'Config/DefaultGame.ini', 'Config/DefaultEngine.ini',
    'Scripts/Tests/Run-LobbyFrontendAutomationTest.ps1'
) | ForEach-Object { Join-Path $projectRoot $_ }
foreach ($required in @($editor, $module) + $files) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing prerequisite: $required" }
}
$moduleTime = (Get-Item -LiteralPath $module).LastWriteTimeUtc
foreach ($file in $files | Where-Object { [IO.Path]::GetExtension($_) -in @('.cpp', '.h') }) {
    if ((Get-Item -LiteralPath $file).LastWriteTimeUtc -gt $moduleTime) { throw "Build the changed source first: $file" }
}
$tests = @('DungeonLobbyPolicy', 'DungeonLobbyCharacterName', 'DungeonLobbyWidget',
    'DungeonCharacterCreationOptions', 'DungeonCharacterSummary', 'DungeonLobbyNextStep', 'DungeonLobbyActionAvailability') |
    ForEach-Object { 'ProjectRPG.Online.' + $_ }
$hashes = @($files | ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_).Hash } })
$moduleHash = (Get-FileHash -LiteralPath $module).Hash
$runRoot = Join-Path $projectRoot ('Saved\Tests\LobbyFrontend\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$process = $null
$passed = $false
$failure = $null
$results = @()
$errorsBefore = $null
$errorsDuring = $null
try {
    $arguments = @(('"' + (Join-Path $projectRoot 'Project_RPG.uproject') + '"'), '/Engine/Maps/Entry',
        ('-ExecCmds="Automation RunTests ' + ($tests -join '+') + '"'), '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-DisablePython', '-NoLiveCoding',
        '-DDC-ForceMemoryCache', ('-UserDir="' + (Join-Path $runRoot 'User') + '"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="' + $logPath + '"'))
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $null = $process.Handle
    Write-Host "Focused lobby frontend tests running: $runRoot"
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTimeOffset]::UtcNow -ge $deadline) { throw 'Focused lobby frontend tests timed out.' }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Owned editor exited $($process.ExitCode)." }
    $body = Get-Content -Encoding UTF8 -LiteralPath $logPath -Raw
    $matches = [regex]::Matches($body, 'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>ProjectRPG\.[^}]+)\}')
    $results = @($matches | ForEach-Object { [ordered]@{ path = $_.Groups['path'].Value; result = $_.Groups['result'].Value } })
    if ($body -match 'Fatal error:|Assertion failed:|Ensure condition failed:' -or $results.Count -ne $tests.Count) {
        throw 'Unexpected focused lobby test coverage or runtime failure.'
    }
    foreach ($test in $tests) {
        if (@($results | Where-Object { $_.path -eq $test -and $_.result -eq 'Success' }).Count -ne 1) { throw "Missing successful test: $test" }
    }
    $boundary = $body.IndexOf('Test Started.')
    if ($boundary -lt 0) { throw 'Test start boundary is missing.' }
    $errorsBefore = [regex]::Matches($body.Substring(0, $boundary), 'Log\w+: Error:').Count
    $errorsDuring = [regex]::Matches($body.Substring($boundary), 'Log\w+: Error:').Count
    if ($errorsDuring -gt 0) { throw 'Runtime Error during focused lobby tests; inspect the retained log.' }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $module).Hash) { throw 'Shared module changed during the run.' }
    foreach ($entry in $hashes) {
        if ($entry.sha256 -ne (Get-FileHash -LiteralPath $entry.path).Hash) { throw "Source changed during the run: $($entry.path)" }
    }
    $passed = $true
    Write-Host "PASS: $($tests.Count) focused lobby tests. Startup Error count: $errorsBefore"
}
catch { $failure = $_.Exception.Message }
finally {
    if ($process) {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(10000) | Out-Null }
        $process.Dispose()
    }
    [ordered]@{ passed = $passed; failure = $failure; expectedTests = $tests; results = $results;
        moduleSha256 = $moduleHash; sourceHashes = $hashes; errorsBeforeFirstTest = $errorsBefore;
        errorsFromFirstTest = $errorsDuring; strictLogAuditPassed = ($null -ne $errorsBefore -and $errorsBefore -eq 0 -and $errorsDuring -eq 0);
        scope = [ordered]@{ nativeFrontend = $true; http = $false; renderedUI = $false; steam = $false; postgres = $false } } |
        ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $runRoot 'Frontend.proof.json')
    Write-Host "Report: $runRoot"
}
if (-not $passed) { throw $failure }
