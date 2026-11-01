[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$testSource = Join-Path $projectRoot 'Source\Project_RPG\Private\Tests\RPGDungeonPartyCombatTests.cpp'
if (-not (Test-Path -LiteralPath $editor) -or -not (Test-Path -LiteralPath $module)) {
    throw 'Build Project_RPGEditor Win64 Development before this focused test.'
}
$tests = @([regex]::Matches((Get-Content -LiteralPath $testSource -Raw -Encoding UTF8),
    'IMPLEMENT_SIMPLE_AUTOMATION_TEST\([^,]+,\s*"(?<path>ProjectRPG\.Online\.DungeonPartyCombat\.[^"]+)"') |
    ForEach-Object { $_.Groups['path'].Value })
if ($tests.Count -eq 0 -or @($tests | Select-Object -Unique).Count -ne $tests.Count) {
    throw 'Missing or duplicate party combat test names.'
}
$files = @(
    'Public\Online\RPGDungeonPartyCombatTypes.h', 'Private\Online\RPGDungeonPartyCombatTypes.cpp',
    'Public\Component\RPGDungeonPartyCombatComponent.h', 'Private\Component\RPGDungeonPartyCombatComponent.cpp',
    'Public\UI\Content\RPGDungeonPartyCombatWidget.h', 'Private\UI\Content\RPGDungeonPartyCombatWidget.cpp',
    'Public\Controller\RPGPlayerController.h', 'Private\Controller\RPGPlayerController.cpp',
    'Private\Controller\RPGPlayerControllerPartyCombat.cpp',
    'Public\GameMode\RPGGameModeBase.h', 'Private\GameMode\RPGGameModeBase.cpp',
    'Private\GameMode\RPGGameModePartyPresentation.cpp',
    'Public\Online\RPGDungeonMembershipSnapshot.h', 'Private\Online\RPGDungeonMembershipSnapshot.cpp',
    'Public\Player\RPGPlayerState.h', 'Private\Player\RPGPlayerState.cpp',
    'Public\Security\RPGDungeonGameplayAuthority.h', 'Private\Security\RPGDungeonGameplayAuthority.cpp',
    'Private\Tests\RPGDungeonPartyCombatTests.cpp'
) | ForEach-Object { Join-Path $projectRoot "Source\Project_RPG\$_" }
$moduleTime = (Get-Item -LiteralPath $module).LastWriteTimeUtc
foreach ($file in $files) {
    if ((Get-Item -LiteralPath $file).LastWriteTimeUtc -gt $moduleTime) { throw "Build the changed source first: $file" }
}
$files += $PSCommandPath
$hashes = @($files | ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_).Hash } })
$moduleHash = (Get-FileHash -LiteralPath $module).Hash
$runRoot = Join-Path $projectRoot ('Saved\Tests\DungeonPartyCombat\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$process = $null
$passed = $false
$failure = $null
$results = @()
try {
    $arguments = @(
        ('"' + (Join-Path $projectRoot 'Project_RPG.uproject') + '"'), '/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.Online.DungeonPartyCombat"',
        '-TestExit="Automation Test Queue Empty"', '-unattended', '-nop4', '-nosplash', '-nosound',
        '-nullrhi', '-nosteam', '-DisablePython', '-NoLiveCoding', '-DDC-ForceMemoryCache',
        ('-UserDir="' + (Join-Path $runRoot 'User') + '"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $null = $process.Handle
    Write-Host "Started focused party HUD tests (owned PID $($process.Id)): $logPath"
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTimeOffset]::UtcNow -ge $deadline) { throw 'Focused party HUD automation timed out.' }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Owned editor exited $($process.ExitCode)." }
    $body = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $matches = [regex]::Matches($body, 'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>ProjectRPG\.[^}]+)\}')
    $results = @($matches | ForEach-Object { [ordered]@{ path = $_.Groups['path'].Value; result = $_.Groups['result'].Value } })
    if ($body -match 'Fatal error:|Assertion failed:|Ensure condition failed:' -or $results.Count -ne $tests.Count) {
        throw 'Unexpected focused party HUD coverage or runtime failure.'
    }
    foreach ($test in $tests) {
        if (@($results | Where-Object { $_.path -eq $test -and $_.result -eq 'Success' }).Count -ne 1) {
            throw "Missing successful party HUD test: $test"
        }
    }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $module).Hash) { throw 'Shared module changed during this run; results are not current-binary proof.' }
    foreach ($entry in $hashes) {
        if ($entry.sha256 -ne (Get-FileHash -LiteralPath $entry.path).Hash) { throw "Source changed during this run: $($entry.path)" }
    }
    $passed = $true
    Write-Host "PASS: $($tests.Count) focused party HUD tests; editor exit=0."
}
catch { $failure = $_.Exception.Message }
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
    [ordered]@{
        schemaVersion = 1; passed = $passed; failure = $failure; editorModuleSHA256 = $moduleHash
        expectedTests = $tests; results = $results; sourceHashes = $hashes; logPath = $logPath
        scope = [ordered]@{ nativePartyPresentation = $true; actualWbpAsset = $false; iris = $false; http = $false; steam = $false }
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'PartyCombat.proof.json') -Encoding UTF8
    Write-Host "Report: $runRoot"
}
if (-not $passed) { throw $failure }
