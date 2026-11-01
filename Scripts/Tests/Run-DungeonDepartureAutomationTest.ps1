[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [ValidateRange(30,600)][int]$TimeoutSeconds=180, [switch]$Persistence)
$ErrorActionPreference='Stop'
$runtimeEnvironment=[Environment]::GetEnvironmentVariables('Process')
$pathKeys=@($runtimeEnvironment.Keys|Where-Object{$_.ToString() -ieq 'Path'})
if($pathKeys.Count -gt 1){
    $pathValue=[string]$runtimeEnvironment[$pathKeys[0]]
    foreach($key in $pathKeys){[Environment]::SetEnvironmentVariable($key.ToString(),$null,'Process')}
    [Environment]::SetEnvironmentVariable('Path',$pathValue,'Process')
}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path $projectRoot 'Project_RPG.uproject'
$module=Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
$root=Join-Path $projectRoot ('Saved/Tests/DungeonDepartureNative/'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Force -Path $root
$log=Join-Path $root 'automation.log'
$paths=@('Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h',
    'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
    'Source/Project_RPG/Public/Manager/HttpWebManager.h',
    'Source/Project_RPG/Private/Tests/RPGDungeonClientFlowTests.cpp',
    'Scripts/Tests/Run-DungeonDepartureAutomationTest.ps1')
if($Persistence){$paths+=@('Source/Project_RPG/Public/Online/RPGDungeonDepartureJournal.h',
    'Source/Project_RPG/Private/Online/RPGDungeonDepartureJournal.cpp',
    'Source/Project_RPG/Private/Online/RPGDungeonDepartureRecovery.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonDepartureJournalTests.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
    'Source/Project_RPG/Private/Online/RPGOnlineLobbySubsystem.cpp')}
$testNames=@('DungeonDepartureRecovery')
if($Persistence){$testNames+=@('DungeonDepartureJournal','DungeonDepartureRestartRecovery')}
$filter=if($Persistence){'ProjectRPG.Online.DungeonDeparture'}else{'ProjectRPG.Online.DungeonDepartureRecovery'}
function Inputs { @($paths|ForEach-Object{[pscustomobject]@{Path=$_;Sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_)).Hash}}) }
$before=Inputs;$moduleBefore=(Get-FileHash -LiteralPath $module).Hash
$proof=[ordered]@{Passed=$false;Scope='One native departure pending-result recovery test; no HTTP/DB/UE network/Steam';Inputs=$before;ModuleSha256=$moduleBefore}
if($Persistence){$proof.Scope='Focused native departure recovery + actual disk journal + recreated GameInstance/authenticated manager; no client-process restart/HTTP/DB/UE network/Steam'}
$process=$null;$passed=$false
try {
    $arguments=@(('"'+$project+'"'),('-ExecCmds="Automation RunTests '+$filter+'"'),
        '-TestExit="Automation Test Queue Empty"','-unattended','-nop4','-nosplash','-nosound','-nullrhi','-nosteam',
        '-DisablePython','-NoLiveCoding','-DDC=(EnginePak,Local)',('-UserDir="'+(Join-Path $root 'User')+'"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $processHandle=$process.Handle
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while(-not $process.HasExited){
        if($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds){throw 'Departure automation timed out.'}
        Start-Sleep -Milliseconds 250
    }
    $process.WaitForExit()
    if($process.ExitCode -ne 0){throw "Editor exited with code $($process.ExitCode)."}
    $body=Get-Content -LiteralPath $log -Raw -Encoding UTF8
    $missing=@($testNames|Where-Object{-not $body.Contains(('Test Completed. Result={Success} Name={'+$_+'} Path={ProjectRPG.Online.'+$_+'}'))})
    if($missing.Count -gt 0 -or $body -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:'){
        throw 'Departure recovery native result failed; inspect preserved automation log.'
    }
    $proof.InputsUnchanged=(($before|ConvertTo-Json -Compress) -ceq ((Inputs)|ConvertTo-Json -Compress))
    $proof.ModuleUnchanged=($moduleBefore -ceq (Get-FileHash -LiteralPath $module).Hash)
    if(-not $proof.InputsUnchanged -or -not $proof.ModuleUnchanged){throw 'Selected native source/module changed during automation.'}
    $proof.TestCount=$testNames.Count;$passed=$true
}
finally {
    if($null -ne $process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}
    $proof.Passed=$passed
    if(Test-Path -LiteralPath $log){$proof.ClosedLogSha256=(Get-FileHash -LiteralPath $log).Hash}
    $proof|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'Verification.proof.json') -Encoding UTF8
    Write-Output "Departure native: Passed=$passed Root=$root"
}
