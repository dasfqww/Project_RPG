[CmdletBinding()]
param([Parameter(Mandatory)][string]$RuntimeProjectRoot,[string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N')
$temp=Join-Path $workspace ('Saved/Intro2/native-'+$id)
$proof=Join-Path $workspace ('Saved/BuildChecks/Intro2Opportunity-20261117/native-'+$id)
$null=[IO.Directory]::CreateDirectory($temp);$null=[IO.Directory]::CreateDirectory($proof)
$result=[ordered]@{Passed=$false;Failure=$null;DeletedBytes=0};$process=$null
try{
    $arguments=@(('"'+$RuntimeProjectRoot+'/Project_RPG.uproject"'),'/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ^ProjectRPG.Progression.Intro2."','-TestExit="Automation Test Queue Empty"',
        '-unattended','-nop4','-nosplash','-nullrhi','-nosound','-NoLiveCoding','-nosteam','-DisablePython',
        '-DDC=(InstalledEnginePak,Local)',('-UserDir="'+$temp+'"'),('-abslog="'+$temp+'/Native.log"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    $process=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null=$process.Handle;$until=[DateTime]::UtcNow.AddSeconds(180)
    while(!$process.HasExited-and[DateTime]::UtcNow-lt$until){Start-Sleep -Milliseconds 250}
    if(!$process.HasExited){throw 'Intro2 native test timeout'};$process.WaitForExit()
    $text=[IO.File]::ReadAllText((Join-Path $temp 'Native.log'))
    if($process.ExitCode-ne0 -or $text-notmatch'Result={Success} Name={FailClosed}'){throw 'Intro2 native automation failed'}
    $result.ModuleSHA256=(Get-FileHash (Join-Path $RuntimeProjectRoot 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    $result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally{
    if($process-and!$process.HasExited){$process.Kill();$process.WaitForExit()}
    $log=Join-Path $temp 'Native.log'
    if(Test-Path -LiteralPath $log){Get-Content -LiteralPath $log | Where-Object {$_-match'Automation|Error:|Fatal error'} | Set-Content (Join-Path $proof 'Native.log') -Encoding UTF8}
    $boundary=[IO.Path]::GetFullPath((Join-Path $workspace 'Saved/Intro2'))+'\'
    if(![IO.Path]::GetFullPath($temp).StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe owned cleanup path'}
    $entries=@(Get-ChildItem -LiteralPath $temp -Recurse -Force)
    if(@($entries|Where-Object {$_.Attributes-band[IO.FileAttributes]::ReparsePoint}).Count){throw 'Unexpected owned native link'}
    $result.DeletedBytes=($entries|Where-Object {!$_.PSIsContainer}|Measure-Object Length -Sum).Sum
    Remove-Item -LiteralPath $temp -Recurse -Force
    $result|ConvertTo-Json|Set-Content (Join-Path $proof 'Result.json') -Encoding UTF8
}
if(!$result.Passed){throw $result.Failure}
Write-Output 'PASS Intro2 native fail-closed automation; own runtime user directory removed.'
