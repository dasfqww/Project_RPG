[CmdletBinding()]
param([string]$EngineRoot='D:\UE_5.8',[ValidateRange(30,600)][int]$TimeoutSeconds=180)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$runRoot=Join-Path $projectRoot ('Saved\Tests\DungeonAbandonment\'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$log=Join-Path $runRoot 'automation.log'
$tests=@('DungeonAbandonmentCodec','DungeonAbandonmentFlow','DungeonAbandonmentRetryPolicy',
    'DungeonAbandonmentTransport','DungeonAbandonmentConfirmation','DungeonAbandonmentResultQueue',
    'DungeonAbandonmentResultConfirmation','DungeonAbandonmentGameplayQueue','DungeonAbandonmentGameplayConfirmation',
    'DungeonResult','DungeonResultWidget','DungeonResultDefeat','DungeonResultAuthentication') |
    ForEach-Object { 'ProjectRPG.Online.'+$_ }
$files=@('Public\Online\RPGDungeonSessionSubsystem.h','Private\Online\RPGDungeonSessionSubsystem.cpp',
    'Public\Online\RPGDungeonAbandonment.h','Private\Online\RPGDungeonAbandonment.cpp',
    'Public\Controller\RPGPlayerController.h','Private\Controller\RPGPlayerController.cpp',
    'Public\UI\Content\RPGDungeonResultActionsWidget.h','Private\UI\Content\RPGDungeonResultActionsWidget.cpp',
    'Public\UI\RPGWindowInputState.h','Private\UI\RPGWindowInputState.cpp',
    'Public\UI\Lobby\RPGOnlineLobbyWidget.h','Private\UI\Lobby\RPGOnlineLobbyWidget.cpp',
    'Public\Manager\HttpWebManager.h','Private\Manager\HttpWebManager.cpp',
    'Public\Online\RPGSteamAuthSubsystem.h','Private\Online\RPGSteamAuthSubsystem.cpp',
    'Private\Tests\RPGDungeonAbandonmentTests.cpp','Private\Tests\RPGDungeonResultActionsTests.cpp',
    'Private\Tests\RPGDungeonEjectionTestFixture.h','Private\Tests\RPGDungeonEjectionTestFixture.cpp') |
    ForEach-Object { Join-Path $projectRoot "Source\Project_RPG\$_" }
$files+=@($PSCommandPath)
$hashes=@($files | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} })
$moduleHash=(Get-FileHash -LiteralPath $module).Hash
foreach($file in $files | Where-Object { $_ -match '\.(cpp|h)$' }) {
    if((Get-Item -LiteralPath $file).LastWriteTimeUtc -gt (Get-Item -LiteralPath $module).LastWriteTimeUtc) { throw 'Build the current Editor module first.' }
}
$taskSdkBefore=$env:UE_SKIP_UBT_SDK_SETUP
$process=$null; $passed=$false; $failure=$null
try {
    $env:UE_SKIP_UBT_SDK_SETUP='1'
    $arguments=@(('"'+(Join-Path $projectRoot 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.Online.DungeonAbandonment+ProjectRPG.Online.DungeonResult"',
        '-TestExit="Automation Test Queue Empty"','-unattended','-nop4','-nosplash','-nosound','-nullrhi','-nosteam',
        '-DisablePython','-NoLiveCoding','-DDC-ForceMemoryCache',('-UserDir="'+(Join-Path $runRoot 'User')+'"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $null=$process.Handle
    $deadline=[DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while(-not $process.HasExited) {
        if([DateTimeOffset]::UtcNow -ge $deadline) { throw 'Focused abandonment automation timed out.' }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    $process.WaitForExit()
    if($process.ExitCode -ne 0) { throw "Owned editor exited $($process.ExitCode)." }
    $body=Get-Content -LiteralPath $log -Raw -Encoding UTF8
    $results=[regex]::Matches($body,'Test Completed\. Result=\{([^}]+)\} Name=\{[^}]+\} Path=\{(ProjectRPG\.[^}]+)\}')
    if($body -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:' -or $results.Count -ne $tests.Count) { throw 'Unexpected focused automation results.' }
    foreach($test in $tests) {
        if(@($results | Where-Object { $_.Groups[1].Value -ceq 'Success' -and $_.Groups[2].Value -ceq $test }).Count -ne 1) { throw "Missing successful result: $test" }
    }
    foreach($entry in $hashes) { if((Get-FileHash -LiteralPath $entry.path).Hash -cne $entry.SHA256) { throw "Selected source changed during native run: $($entry.path)" } }
    if((Get-FileHash -LiteralPath $module).Hash -cne $moduleHash) { throw 'Shared DLL changed during native run.' }
    $passed=$true
} catch { $failure=$_.Exception.Message }
finally {
    if($process) {
        if(-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
    $env:UE_SKIP_UBT_SDK_SETUP=$taskSdkBefore
    [ordered]@{passed=$passed;failure=$failure;tests=$tests;moduleSHA256=$moduleHash;sourceHashes=$hashes;
        log=$log;closedLogSHA256=$(if(Test-Path -LiteralPath $log){(Get-FileHash -LiteralPath $log).Hash}else{$null});
        scope='Native isolated fixtures only; not real HTTP, map travel, network ejection, Steam or Shipping'} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
}
if(-not $passed) { throw "$failure Inspect $runRoot" }
Write-Host "PASS: $($tests.Count) focused abandonment/result native tests. $runRoot"
