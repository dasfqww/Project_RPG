# Drives only the owned Editor's visible Slate controls. No gameplay state setters.
. (Join-Path $PSScriptRoot '../Network/DungeonPartyHudObservation.ps1')

function Get-TrainingLobbyNodes([string]$Snapshot){
    $index=0
    foreach($line in ($Snapshot -split '\r?\n')){
        if($line -match '^\s*(?<role>button|textbox|combobox)(?: "(?<label>(?:\\.|[^"\\])*)")?.*\[pos=-?\d+,-?\d+ size=(?<width>\d+),(?<height>\d+)\].*\[ref=(?<ref>[^\]]+)\]'){
            if([int]$Matches.width -gt 0 -and [int]$Matches.height -gt 0){
                [pscustomobject]@{index=$index;role=$Matches.role;label=$Matches.label;ref=$Matches.ref;enabled=($line -notmatch '\bdisabled\b');line=$line}
            }
        }
        $index++
    }
}
function Find-TrainingLobbyButton($Nodes,[string]$Label,[switch]$AllowDisabled){
    $found=@($Nodes | Where-Object {$_.role -eq 'button' -and $_.label -ceq $Label -and ($AllowDisabled -or $_.enabled)})
    if($found.Count -gt 1){throw ('Ambiguous visible lobby action: '+$Label)}
    if($found.Count -eq 1){return $found[0]}
}
function Find-TrainingLobbyField($Nodes,$Button,[string]$Role){
    $found=@($Nodes | Where-Object {$_.role -eq $Role -and $_.index -lt $Button.index} | Sort-Object index -Descending)
    if(!$found.Count){throw ('No visible '+$Role+' before lobby action')}
    if(!$found[0].enabled){return}
    return $found[0]
}
function Expand-TrainingLobbyRoster([object]$Response){
    # Windows PowerShell ConvertFrom-Json emits an empty JSON array as one
    # pipeline object. Enumerate it before counting characters.
    foreach($character in $Response){if($null -ne $character){$character}}
}
function Invoke-TrainingLobbyTool([string]$Tool,[hashtable]$Arguments=@{},[string]$Toolset='SlateInspectorToolset.SlateInspectorToolset',[int]$RequestTimeoutSeconds=15){
    Set-StrictMode -Off
    Invoke-PartyHudEditorTool $script:TrainingLobbyUI.port $Toolset $Tool $Arguments -RequestTimeoutSeconds $RequestTimeoutSeconds
}
function Save-TrainingLobbySnapshot([string]$Name,[string]$Snapshot){
    # Output-log widgets may contain network Browse URLs. Never retain those.
    $safe=($Snapshot -split '\r?\n' | Where-Object {$_ -notmatch '(?i)token|ticket|authorization|Browse:|command\s*line'}) -join "`n"
    [IO.File]::WriteAllText((Join-Path $runRoot ($Name+'.slate.txt')),$safe,[Text.UTF8Encoding]::new($false))
}
function Start-TrainingLobbyUI($Client,[int]$Port,[string]$Steam,[string]$Name){
    Set-StrictMode -Off
    $script:PartyHudMcpSessions=@{}
    $script:PartyHudClients=@{trainingLobby=@{Port=$Port;Process=$Client}}
    $script:TrainingLobbyUI=@{port=$Port;steam=$Steam;name=$Name;stage=0;lastSnapshot='';actions=[Collections.Generic.List[object]]::new();nextTick=[DateTime]::MinValue}
    $until=[DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds);$ready=$false
    while([DateTime]::UtcNow -lt $until){
        if($Client.HasExited){throw 'Owned UI editor exited before startup'}
        try{$playing=Invoke-TrainingLobbyTool 'IsPIERunning' @{} 'EditorToolset.EditorAppToolset';$ready=$true;break}catch{Start-Sleep -Milliseconds 250}
    }
    if(!$ready -or $playing){throw 'Owned UI editor unavailable or already playing'}
    try{$null=Invoke-TrainingLobbyTool 'StartPIE' @{options=@{bSimulate=$false;playMode='PlayMode_InEditorFloating';warmupSeconds=0.0}} 'EditorToolset.EditorAppToolset' 55}
    catch{if([string]$_.Exception.Data['McpToolError'] -notmatch '(?i)timed out.*PIE|PIE.*timed out' -and $_.Exception.Message -notmatch 'MCP transport failed'){throw}}
    while($true){
        try{if(Invoke-TrainingLobbyTool 'IsPIERunning' @{} 'EditorToolset.EditorAppToolset'){break}}catch{}
        if($Client.HasExited -or [DateTime]::UtcNow -ge $until){throw 'Owned PIE start could not be observed'}
        Start-Sleep -Milliseconds 250
    }
    $null=Invoke-TrainingLobbyTool 'Observe' @{ref='';maxDepth=100}
    $windows=(Invoke-TrainingLobbyTool 'Windows' @{action='list'}) | ConvertFrom-Json
    Write-Host ('TRAINING_LOBBY_UI windows '+($windows | ConvertTo-Json -Compress))
    $game=@($windows | Where-Object {$_.title -match 'Preview|Standalone|미리보기'})
    if($game.Count -eq 1){$null=Invoke-TrainingLobbyTool 'Windows' @{action='select';index=$game[0].index}}
    Write-Host 'TRAINING_LOBBY_UI owned floating PIE started'
}
function Send-TrainingLobbyAction([string]$Tool,[hashtable]$Arguments,[string]$Name){
    $ok=Invoke-TrainingLobbyTool $Tool $Arguments
    if(!$ok){throw ('Slate action failed: '+$Name)}
    $script:TrainingLobbyUI.actions.Add([pscustomobject]@{action=$Name;tool=$Tool;stage=$script:TrainingLobbyUI.stage})
    Write-Host ('TRAINING_LOBBY_UI '+$Name)
}
function Tick-TrainingLobbyUI {
    Set-StrictMode -Off
    $state=$script:TrainingLobbyUI
    if([DateTime]::UtcNow -lt $state.nextTick){return $false}
    $state.nextTick=[DateTime]::UtcNow.AddMilliseconds(500)
    $snapshot=Invoke-TrainingLobbyTool 'Snapshot' @{ref='';maxDepth=100;bIncludeSourceLocations=$false}
    $state.lastSnapshot=$snapshot
    $nodes=@(Get-TrainingLobbyNodes $snapshot)
    switch($state.stage){
        0 {
            $button=Find-TrainingLobbyButton $nodes '개발 백엔드 로그인'
            if(!$button){return $false}
            Save-TrainingLobbySnapshot 'lobby-login' $snapshot
            $field=Find-TrainingLobbyField $nodes $button 'textbox'
            if(!$field){return $false}
            Send-TrainingLobbyAction 'Type' @{ref=$field.ref;text=$state.steam;submit=$false} 'TypeDevelopmentId'
            Send-TrainingLobbyAction 'PressKey' @{key='Tab'} 'FocusLogin'
            Send-TrainingLobbyAction 'PressKey' @{key='Enter'} 'Login'
            $state.stage=1
        }
        1 {
            $server=Find-TrainingLobbyButton $nodes '서버 선택'
            if($server){
                Save-TrainingLobbySnapshot 'server-selection' $snapshot
                Send-TrainingLobbyAction 'Click' @{ref=$server.ref;button='left'} 'SelectServer'
                $state.serverSelected=$true
                return $false
            }
            $button=Find-TrainingLobbyButton $nodes '캐릭터 생성' -AllowDisabled
            if(!$button){return $false}
            if($snapshot -match '내 캐릭터를 불러오고 있습니다'){return $false}
            $field=Find-TrainingLobbyField $nodes $button 'textbox'
            if(!$field){return $false}
            # Tab uses the screen's real navigation and ScrollWhenFocusChanges.
            # Cached offscreen geometry alone cannot establish input delivery.
            if($field.line -notmatch '\[focused\]'){
                Send-TrainingLobbyAction 'PressKey' @{key='Tab'} 'FocusCharacterName'
                return $false
            }
            # Read-only observer credential, requested only after the UI login
            # produced the authenticated character page. Not a UI auth injection.
            $auth=Api 'auth/steam-ticket' 'Post' @{} @{ticket=('dev:'+$state.steam)}
            $state.player=@{Authorization='Bearer '+$auth.accessToken}
            $roster=@(Expand-TrainingLobbyRoster (Api 'characters' 'Get' $state.player))
            if($roster.Count){throw 'Fresh UI account already has characters'}
            Save-TrainingLobbySnapshot 'lobby-character-form' $snapshot
            Send-TrainingLobbyAction 'PressKey' @{key='Ctrl+A'} 'SelectCharacterNameDraft'
            Send-TrainingLobbyAction 'Type' @{ref=$field.ref;text=$state.name;submit=$false} 'TypeCharacterName'
            $state.stage=2
        }
        2 {
            $button=Find-TrainingLobbyButton $nodes '캐릭터 생성'
            if(!$button){return $false}
            $field=Find-TrainingLobbyField $nodes $button 'textbox'
            if(!$field){return $false}
            # Slate's accessible textbox label can remain its hint after typing.
            # The next stage verifies the saved name through the owner's API.
            if($button.line -notmatch '\[focused\]'){
                Send-TrainingLobbyAction 'PressKey' @{key='Tab'} 'FocusCreateCharacter'
                return $false
            }
            Send-TrainingLobbyAction 'PressKey' @{key='Enter'} 'CreateCharacter'
            $state.stage=3
        }
        3 {
            if($snapshot -match '내 캐릭터를 불러오고 있습니다'){return $false}
            $roster=@(Expand-TrainingLobbyRoster (Api 'characters' 'Get' $state.player))
            if(!$roster.Count){return $false}
            if($roster.Count -ne 1 -or $roster[0].name -cne $state.name -or $roster[0].progression.classId -cne 'RPGPlayerClassDefinition:GreatSwordWarrior'){throw 'UI-created character differs from expected owner/class'}
            $button=Find-TrainingLobbyButton $nodes '접속' -AllowDisabled
            if(!$button){return $false}
            $field=Find-TrainingLobbyField $nodes $button 'combobox'
            if(!$field){return $false}
            Send-TrainingLobbyAction 'SelectOption' @{ref=$field.ref;value=$state.name} 'SelectCharacter'
            $state.character=[string]$roster[0].characterId
            $state.stage=4
        }
        4 {
            $button=Find-TrainingLobbyButton $nodes '접속'
            if(!$button){return $false}
            Send-TrainingLobbyAction 'Click' @{ref=$button.ref;button='left'} 'ConnectCharacterToLobby'
            $state.stage=5
        }
        5 {
            $button=Find-TrainingLobbyButton $nodes '튜토리얼 입장'
            if(!$button){return $false}
            if($snapshot -notmatch '로비'){throw 'Character connect did not display the menu lobby'}
            if(!$state.serverSelected){throw 'Menu flow skipped the real server-selection page'}
            Save-TrainingLobbySnapshot 'menu-lobby' $snapshot
            Send-TrainingLobbyAction 'Click' @{ref=$button.ref;button='left'} 'EnterTutorialOnce'
            $state.stage=6
        }
        6 {
            $session=Api ('characters/'+$state.character+'/active-dungeon-session') 'Get' $state.player
            if(!$session){return $false}
            if($session.dungeonId -cne 'Progression.Training' -or $session.difficulty -cne 'Normal' -or $session.state -cnotin @('Waiting','Loading','InProgress')){throw 'UI-created session differs from training selection'}
            $state.session=[string]$session.dungeonSessionId
            if($session.state -eq 'InProgress'){$state.stage=8}
        }
        8 {
            $log=Read-Log (Join-Path $temp 'client.log')
            if($log -notmatch 'Bringing World .*(?:UEDPIE_\d+_)?L_ProgressionTraining'){return $false}
            if($snapshot -notmatch '기본 조작 훈련 0/4' -or $snapshot -notmatch '기본 공격' -or $snapshot -notmatch '차지 공격'){return $false}
            $session=Api ('dungeon-sessions/'+$state.session) 'Get' $state.player
            if($session.state -ne 'InProgress'){throw 'UI entry changed session before practice'}
            $growth=Api ('characters/'+$state.character+'/progression') 'Get' $state.player
            if($growth.experience -ne 0 -or $growth.level -ne 1){throw 'UI entry credited uncompleted training'}
            Save-TrainingLobbySnapshot 'training-guide-on-entry' $snapshot
            $state.stage=9
            return $true
        }
        9 {return $true}
    }
    return $false
}
function Complete-TrainingLobbyUI {
    $state=$script:TrainingLobbyUI
    if($state.stage -ne 9){throw 'Lobby UI entry did not finish'}
    $null=Invoke-TrainingLobbyTool 'StopPIE' @{} 'EditorToolset.EditorAppToolset' 55
    if(Invoke-TrainingLobbyTool 'IsPIERunning' @{} 'EditorToolset.EditorAppToolset'){throw 'Owned PIE did not stop after UI observation'}
    [pscustomobject]@{passed=$true;character=$state.character;session=$state.session;actions=$state.actions.ToArray();actualSlateInput=$true;physicalInput=$false;characterSessionDepartureThroughUI=$true;serverSelected=$state.serverSelected;singleTutorialButton=$true;trainingGuideOnEntry=$true;completionAndReturnTested=$false}
}
function Save-TrainingLobbyUIFailure {
    Set-StrictMode -Off
    if($script:TrainingLobbyUI -and $script:TrainingLobbyUI.lastSnapshot -and $script:TrainingLobbyUI.stage -ne 9){
        Save-TrainingLobbySnapshot 'lobby-ui-final-diagnostic' $script:TrainingLobbyUI.lastSnapshot
    }
}
