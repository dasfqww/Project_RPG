# Read the actual owned PIE Slate tree. No UI setters, ability activation or reward calls.
. (Join-Path $PSScriptRoot '../Network/DungeonPartyHudObservation.ps1')

function Start-ActionProgressPIE($Client,[int]$Port,[scriptblock]$KeepAlive){
    # The existing MCP helper uses optional JSON fields; its caller must allow them.
    Set-StrictMode -Off
    $script:PartyHudMcpSessions=@{}
    $listeners=@(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
    if(@($listeners | Where-Object OwningProcess -ne $Client.Id).Count){throw 'PIE observer port is already in use'}
    $script:ActionProgressPIEPort=$Port
    $script:PartyHudClients=@{progress=@{Port=$Port;Process=$Client}}
    $script:ActionProgressPIESamples=[Collections.Generic.List[object]]::new()
    $script:ActionProgressPIELast=$null
    $script:ActionProgressPIENextRead=[DateTime]::MinValue
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $ready=$false
    while([DateTime]::UtcNow -lt $deadline){
        if($Client.HasExited){throw 'Owned PIE editor exited during startup'}
        if($KeepAlive){& $KeepAlive}
        try{
            $playing=Invoke-PartyHudEditorTool $Port 'EditorToolset.EditorAppToolset' 'IsPIERunning'
            $ready=$true;break
        }catch{Start-Sleep -Milliseconds 250}
    }
    if(!$ready -or $playing){throw 'Owned PIE editor unavailable or already playing'}
    if($KeepAlive){& $KeepAlive}
    try{
        $null=Invoke-PartyHudEditorTool $Port 'EditorToolset.EditorAppToolset' 'StartPIE' @{
            options=@{bSimulate=$false;playMode='PlayMode_InViewPort';warmupSeconds=0.0}} -RequestTimeoutSeconds 55
    }catch{
        if([string]$_.Exception.Data['McpToolError'] -notmatch '(?i)timed out.*PIE|PIE.*timed out'){throw}
        # A timed-out watcher does not cancel the play request. Observe it, never queue another.
    }
    while(!(Invoke-PartyHudEditorTool $Port 'EditorToolset.EditorAppToolset' 'IsPIERunning')){
        if([DateTime]::UtcNow -ge $deadline -or $Client.HasExited){throw 'Owned PIE did not start'}
        if($KeepAlive){& $KeepAlive}
        Start-Sleep -Milliseconds 250
    }
    $null=Invoke-PartyHudEditorTool $Port 'SlateInspectorToolset.SlateInspectorToolset' 'Observe' @{ref='';maxDepth=80}
    Write-Host ('ACTION_PROGRESS_PIE STARTED PID='+$Client.Id)
}

function Read-ActionProgressPIE([string]$ClientLog){
    Set-StrictMode -Off
    if($ClientLog -notmatch 'RPG_TRAINING_PLAYTEST PRESS Action=3 '){return}
    if($script:ActionProgressPIESamples.Count -ge 12){return}
    $released=$ClientLog -match 'RPG_TRAINING_PLAYTEST RELEASE Action=3 '
    # Deep Slate snapshots run on the game thread. Avoid shortening this probe's
    # wall-clock hold by repeatedly interrupting the client's simulation clock.
    if(!$released -and [DateTime]::UtcNow -lt $script:ActionProgressPIENextRead){return}
    $snapshot=Invoke-PartyHudEditorTool $script:ActionProgressPIEPort 'SlateInspectorToolset.SlateInspectorToolset' 'Snapshot' @{ref='';maxDepth=80;bIncludeSourceLocations=$false}
    $seconds=@([regex]::Matches($snapshot,'text "([0-9]+(?:[.,][0-9]+)?s)"') | ForEach-Object {$_.Groups[1].Value})
    $stages=@([regex]::Matches($snapshot,'text "([0-9]+ / [0-9]+)"') | ForEach-Object {$_.Groups[1].Value})
    $key=([string]$released+';'+($seconds -join ',')+';'+($stages -join ',')+';'+[string]($snapshot -match 'text "Ready"'))
    if($key -eq $script:ActionProgressPIELast){return}
    $script:ActionProgressPIELast=$key
    $index=$script:ActionProgressPIESamples.Count
    $name='progress-pie-'+$index+'.txt'
    [IO.File]::WriteAllText((Join-Path $runRoot $name),$snapshot,[Text.UTF8Encoding]::new($false))
    $sample=[pscustomobject]@{atUtc=[DateTime]::UtcNow.ToString('o');released=$released;seconds=$seconds;stages=$stages;ready=($snapshot -match 'text "Ready"');progressBar=($snapshot -match '(?m)^\s*progressbar\b');snapshot=$name}
    $script:ActionProgressPIESamples.Add($sample)
    $wait=if($seconds.Count -eq 1 -and [double]($seconds[0].TrimEnd('s').Replace(',','.')) -lt 1.0){0.35}else{0.0}
    $script:ActionProgressPIENextRead=[DateTime]::UtcNow.AddSeconds($wait)
    Write-Host ('ACTION_PROGRESS_PIE SAMPLE '+($sample | ConvertTo-Json -Compress))
}

function Complete-ActionProgressPIE {
    Set-StrictMode -Off
    $port=$script:ActionProgressPIEPort
    if(!(Invoke-PartyHudEditorTool $port 'EditorToolset.EditorAppToolset' 'IsPIERunning')){throw 'Owned PIE ended before final observation'}
    $samples=$script:ActionProgressPIESamples.ToArray()
    $active=@($samples | Where-Object {!$_.released -and $_.seconds.Count -eq 1 -and $_.stages.Count -ge 1 -and $_.progressBar})
    $ready=@($active | Where-Object ready)
    $ended=@($samples | Where-Object {$_.released -and $_.seconds.Count -eq 0 -and !$_.ready})
    if(!$active.Count -or !$ready.Count -or !$ended.Count){throw 'PIE charge running/Ready/end presentation was not fully observed'}
    $null=Invoke-PartyHudEditorTool $port 'EditorToolset.EditorAppToolset' 'StopPIE' @{} -RequestTimeoutSeconds 55
    if(Invoke-PartyHudEditorTool $port 'EditorToolset.EditorAppToolset' 'IsPIERunning'){throw 'Owned PIE did not stop'}
    @{passed=$true;started=$true;running=$active.Count;ready=$ready.Count;ended=$ended.Count;stopped=$true;uiStateInjected=$false;physicalInput=$false}
}
