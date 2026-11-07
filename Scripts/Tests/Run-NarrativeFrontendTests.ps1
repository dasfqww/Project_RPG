[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [string]$ExpectedModuleSHA256='',
    [ValidateSet('native','complete','restore','dungeon','dungeonrestore')][string]$Phase='native',
    [string]$BackendUrl='', [Guid]$CharacterId=[Guid]::Empty, [string]$SteamId='',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$TaskId='narrative-dialogue-20261104',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ProofName='NarrativeDialogue-20261104', [switch]$IncludeInformation,
    [string]$RuntimeProjectRoot='', [switch]$IncludeNPC)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$RuntimeProjectRoot){$RuntimeProjectRoot=$workspace}
$RuntimeProjectRoot=[IO.Path]::GetFullPath($RuntimeProjectRoot)
$proof=Join-Path $workspace ('Saved/BuildChecks/'+$ProofName)
$run=Join-Path $proof ([Guid]::NewGuid().ToString('N'))
$temp=Join-Path $run 'Temp'
$runtime=Join-Path $temp 'Runtime'
$process=$null; $loaded=$null; $copies=[Collections.Generic.List[object]]::new()
$tests=@('ProjectRPG.Narrative.Codec','ProjectRPG.Narrative.Coordinator','ProjectRPG.Narrative.PlayerTransport','ProjectRPG.Narrative.SubsystemOwnership','ProjectRPG.Narrative.DialogueWidget')
$tests+=@('ProjectRPG.Narrative.InformationDialogue')
$tests+=@('ProjectRPG.Narrative.DungeonObjectives')
if($IncludeNPC){$tests+=@('ProjectRPG.Narrative.NPCInteraction')}
$tests+=@('ProjectRPG.Online.DungeonLobbyWidget','ProjectRPG.Online.DungeonLobbyNavigation.NativeConnection','ProjectRPG.Online.DungeonLobbyNavigation.OrderAndAvailability','ProjectRPG.Online.DungeonLobbyBackInput.Preparation')
if($Phase -ne 'native'){
    if($BackendUrl -notmatch '^http://127\.0\.0\.1:\d+/api$' -or $CharacterId -eq [Guid]::Empty -or $SteamId -notmatch '^7656119\d{10}$'){throw 'Invalid isolated HTTP probe context'}
    $tests=@('ProjectRPG.Narrative.HttpDialoguePersistence')
}
$result=[ordered]@{Passed=$false;Phase=$Phase;NPC=[bool]$IncludeNPC;NativeTests=@();Exit=$null;LoadedModule=$null;SnapshotModuleSHA256=$null;Failure=$null;Cleanup=$null;RuntimeDeleted=$false;Gpu=$false;BackendHttpE2E=($Phase -ne 'native')}
$null=New-Item -ItemType Directory -Path $runtime -Force
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Copy-Fixed([string]$From,[string]$To){
    $before=Hash $From
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $To) -Force
    Copy-Item -LiteralPath $From -Destination $To
    if((Hash $From) -cne $before -or (Hash $To) -cne $before){throw ('Mixed snapshot: '+$From)}
    $copies.Add(@{Source=$From;Copy=$To;SHA256=$before})
}
function Copy-Tree([string]$From,[string]$To,[switch]$Binaries){
    if(!(Test-Path -LiteralPath $From)){return}
    foreach($file in Get-ChildItem -LiteralPath $From -File -Recurse){
        if($Binaries -and $file.Extension -notin '.dll','.modules'){continue}
        Copy-Fixed $file.FullName (Join-Path $To $file.FullName.Substring($From.Length+1))
    }
}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
try{
    $rows=@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match ('^\|[^|]*'+[regex]::Escape($TaskId))})
    if($rows.Count -ne 1){throw 'Own task reservation missing'}
    Copy-Fixed (Join-Path $RuntimeProjectRoot 'Project_RPG.uproject') (Join-Path $runtime 'Project_RPG.uproject')
    Copy-Tree (Join-Path $RuntimeProjectRoot 'Config') (Join-Path $runtime 'Config')
    Copy-Tree (Join-Path $RuntimeProjectRoot 'Binaries/Win64') (Join-Path $runtime 'Binaries/Win64') -Binaries
    foreach($descriptor in Get-ChildItem -LiteralPath (Join-Path $RuntimeProjectRoot 'Plugins') -File -Recurse -Filter '*.uplugin'){
        $destination=Join-Path $runtime $descriptor.DirectoryName.Substring($RuntimeProjectRoot.Length+1)
        Copy-Fixed $descriptor.FullName (Join-Path $destination $descriptor.Name)
        Copy-Tree (Join-Path $descriptor.DirectoryName 'Config') (Join-Path $destination 'Config')
        Copy-Tree (Join-Path $descriptor.DirectoryName 'Binaries') (Join-Path $destination 'Binaries') -Binaries
        foreach($name in @('Content','Resources')){
            $from=Join-Path $descriptor.DirectoryName $name
            if(Test-Path -LiteralPath $from){$null=New-Item -ItemType Junction -Path (Join-Path $destination $name) -Target $from}
        }
    }
    $null=New-Item -ItemType Junction -Path (Join-Path $runtime 'Content') -Target (Join-Path $RuntimeProjectRoot 'Content')
    foreach($entry in $copies){if((Hash $entry.Source) -cne $entry.SHA256){throw 'Snapshot cohort changed'}}
    $result.SnapshotModuleSHA256=Hash (Join-Path $runtime 'Binaries/Win64/UnrealEditor-Project_RPG.dll')
    if($ExpectedModuleSHA256 -and $ExpectedModuleSHA256 -cne $result.SnapshotModuleSHA256){throw 'Unexpected module provenance'}
    $copies | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'SnapshotInputs.json') -Encoding UTF8
    @{Ready=$true;ModuleSHA256=$result.SnapshotModuleSHA256;Runtime=$runtime}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'SnapshotReady.json') -Encoding UTF8
    @'

[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Engine/Maps/Entry
EditorStartupMap=/Engine/Maps/Entry
ServerDefaultMap=/Engine/Maps/Entry
GlobalDefaultGameMode=/Script/Engine.GameModeBase
GameInstanceClass=/Script/Engine.GameInstance
[/Script/Engine.Engine]
AssetManagerClassName=/Script/Engine.AssetManager
'@ | Add-Content -LiteralPath (Join-Path $runtime 'Config/DefaultEngine.ini') -Encoding UTF8
    if($Phase -ne 'native'){
        $world='/Engine/Maps/Entry'
        @('','[/Script/Project_RPG.HttpWebManager]',('ApiUrl='+$BackendUrl),'BackendServiceToken=',
          '','[/Script/Project_RPG.RPGOnlineLobbySubsystem]',('LobbyMapPackage='+$world),'bAutoShowOnlineLobby=False') |
            Add-Content -LiteralPath (Join-Path $runtime 'Config/DefaultGame.ini') -Encoding UTF8
    }
    $log=Join-Path $run 'Native.log'
    $arguments=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),('/Engine/Maps/Entry'),('-ExecCmds="Automation RunTests '+($tests -join '+')+'"'),'-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-DDC=(InstalledEnginePak,Local)',('-UserDir="'+(Join-Path $temp 'User')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-abslog="'+$log+'"'),'-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    if($Phase -ne 'native'){
        $arguments+=@('-game','-RPGDevelopmentLogin',('-RPGNarrativePhase='+$Phase),('-RPGNarrativeCharacter='+$CharacterId.ToString()),('-RPGNarrativeSteam='+$SteamId),('-RPGNarrativeWorld='+$world),
            ('-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl='+$BackendUrl),
            ('-ini:Game:[/Script/Project_RPG.RPGOnlineLobbySubsystem]:LobbyMapPackage='+$world),
            '-ini:Game:[/Script/Project_RPG.RPGOnlineLobbySubsystem]:bAutoShowOnlineLobby=False')
        if($IncludeInformation){$arguments+='-RPGNarrativeInformation'}
        if($IncludeNPC){$arguments+='-RPGNarrativeNPC'}
    }
    $arguments | Set-Content -LiteralPath (Join-Path $run 'Command.txt') -Encoding UTF8
    $program=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    # Test an already built module without starting another all-platform SDK UBT
    # process, which would contend with other agents' independent builds.
    $previousSDKSetup=[Environment]::GetEnvironmentVariable('UE_SKIP_UBT_SDK_SETUP','Process')
    try{
        [Environment]::SetEnvironmentVariable('UE_SKIP_UBT_SDK_SETUP','1','Process')
        $process=Start-Process -FilePath $program -ArgumentList $arguments -PassThru -WindowStyle Hidden
    }finally{[Environment]::SetEnvironmentVariable('UE_SKIP_UBT_SDK_SETUP',$previousSDKSetup,'Process')}
    $null=$process.Handle
    @{Pid=$process.Id;Program=$program;StartUtc=$process.StartTime.ToUniversalTime().ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'Process.json') -Encoding UTF8
    $deadline=[DateTime]::UtcNow.AddMinutes(10)
    while(!$process.HasExited){
        if(!$loaded){
            try{
                $module=@((Get-Process -Id $process.Id).Modules | Where-Object {$_.ModuleName -eq 'UnrealEditor-Project_RPG.dll'})
                if($module.Count -eq 1){$loaded=@{Path=$module[0].FileName;SHA256=(Hash $module[0].FileName)}}
            }catch{}
        }
        if([DateTime]::UtcNow -ge $deadline){throw 'Own native process timed out'}
        $null=$process.WaitForExit(250)
    }
    $process.WaitForExit();$result.Exit=$process.ExitCode;$result.LoadedModule=$loaded
    if($process.ExitCode -ne 0 -or !$loaded -or $loaded.SHA256 -cne $result.SnapshotModuleSHA256 -or
        [IO.Path]::GetFullPath($loaded.Path) -cne [IO.Path]::GetFullPath((Join-Path $runtime 'Binaries/Win64/UnrealEditor-Project_RPG.dll'))){throw 'Native exit/module mismatch'}
    $body=Get-Content -LiteralPath $log -Raw -Encoding UTF8
    $matches=[regex]::Matches($body,'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>[^}]+)\}')
    $result.NativeTests=@($matches | ForEach-Object {@{Path=$_.Groups['path'].Value;Result=$_.Groups['result'].Value}})
    foreach($test in $tests){if(@($result.NativeTests | Where-Object {$_.Path -ceq $test -and $_.Result -ceq 'Success'}).Count -ne 1){throw ('Native test failed/missing: '+$test)}}
    if($body -match 'Fatal error:|Assertion failed:|Ensure condition failed:'){throw 'Native fatal/assert/ensure'}
    $discovery=[regex]::Match($body,('(?m)^.*Found '+$tests.Count+' automation tests.*$'))
    if(!$discovery.Success -or $body.Substring($discovery.Index) -match '(?m)^.*: Error:'){throw 'Native test-phase errors or missing discovery'}
    $result.Passed=$true
    Write-Output ('PASS Narrative '+$Phase+' '+$tests.Count+' tests')
}catch{$result.Failure=$_.Exception.Message;throw}
finally{
    if($process -and !$process.HasExited){
        $actual=Get-Process -Id $process.Id -ErrorAction Stop
        if([IO.Path]::GetFullPath($actual.Path) -cne [IO.Path]::GetFullPath($program) -or $actual.StartTime -ne $process.StartTime){throw 'Own process identity changed'}
        Stop-Process -Id $process.Id;$process.WaitForExit()
    }
    try{
        $result.Cleanup=Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $run
        $result.RuntimeDeleted=!(Test-Path -LiteralPath $temp)
    }catch{$result.Failure=([string]$result.Failure+'; cleanup: '+$_.Exception.Message).Trim(';',' ')}
    if(!$result.RuntimeDeleted){$result.Passed=$false}
    $result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding UTF8
    Write-Output ('Result: '+(Join-Path $run 'Result.json'))
    if(!$result.RuntimeDeleted){throw 'Owned frontend cleanup incomplete; inspect Result.json'}
}
