# Opt-in observer for owned full-Editor clients. Reads rendered Slate only;
# never seeds identities/snapshots, edits assets, changes clocks or sends RPCs.
function Get-PartyHudClosedLogHashes {
    param([string[]]$Paths)
    # The owned processes have exited, but Windows may briefly retain their log
    # handles. Wait for exclusive-writer release; never hash through a live writer.
    foreach ($path in $Paths) {
        $deadline=[DateTime]::UtcNow.AddSeconds(5)
        while ($true) {
            try {
                $hash=(Get-FileHash -LiteralPath $path -ErrorAction Stop).Hash
                [ordered]@{path=$path;SHA256=$hash}
                break
            } catch [IO.IOException] {
                if ([DateTime]::UtcNow -ge $deadline) { throw }
                Start-Sleep -Milliseconds 100
            }
        }
    }
}

function Invoke-PartyHudMcpRpc {
    param([int]$McpPort,[string]$Method,[hashtable]$Params=@{},[switch]$Notification,[int]$RequestTimeoutSeconds=15)
    if (-not $script:PartyHudMcpSessions) { $script:PartyHudMcpSessions=@{} }
    $uri="http://127.0.0.1:$McpPort/mcp"
    $headers=@{Accept='application/json, text/event-stream'}
    if ($Method -ne 'initialize') {
        if (-not $script:PartyHudMcpSessions.ContainsKey($McpPort)) {
            $null=Invoke-PartyHudMcpRpc $McpPort 'initialize' @{
                protocolVersion='2024-11-05';capabilities=@{};
                clientInfo=@{name='ProjectRpgPartyHudObservation';version='1.0'}}
            $null=Invoke-PartyHudMcpRpc $McpPort 'notifications/initialized' @{} -Notification
        }
        $headers['Mcp-Session-Id']=$script:PartyHudMcpSessions[$McpPort]
    }
    $request=@{jsonrpc='2.0';method=$Method;params=$Params}
    if (-not $Notification) { $request.id=1 }
    $json=$request | ConvertTo-Json -Depth 18 -Compress
    try {
        $response=Invoke-WebRequest -UseBasicParsing -Uri $uri -Method Post -Headers $headers `
            -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($json)) -TimeoutSec $RequestTimeoutSeconds
    } catch { throw "Owned editor MCP transport failed: Port=$McpPort Method=$Method" }
    if ($Method -eq 'initialize') {
        $session=[string]$response.Headers['Mcp-Session-Id']
        if ([string]::IsNullOrWhiteSpace($session)) { throw 'MCP initialize returned no session.' }
        $script:PartyHudMcpSessions[$McpPort]=$session
    }
    if ($Notification) { return }
    $body=[string]$response.Content
    if (-not $body.TrimStart().StartsWith('{')) {
        $body=(($body -split '\r?\n') | Where-Object { $_.StartsWith('data:') } |
            ForEach-Object { $_.Substring(5).Trim() }) -join "`n"
    }
    $rpc=$body | ConvertFrom-Json
    if ($rpc.error) { throw "MCP request failed: $Method" }
    return $rpc.result
}

function Invoke-PartyHudEditorTool {
    param([int]$McpPort,[string]$Toolset,[string]$Tool,[hashtable]$Arguments=@{},[switch]$Raw,[int]$RequestTimeoutSeconds=15)
    $owners=@($script:PartyHudClients.Values | Where-Object Port -eq $McpPort)
    if ($owners.Count -ne 1) { throw 'Only registered owned editor endpoints may be called.' }
    $owners[0].Process.Refresh()
    $listeners=@(Get-NetTCPConnection -State Listen -LocalPort $McpPort -ErrorAction SilentlyContinue)
    if ($owners[0].Process.HasExited -or $listeners.Count -ne 1 -or
        $listeners[0].OwningProcess -ne $owners[0].Process.Id -or $listeners[0].LocalAddress -ne '127.0.0.1') {
        throw "Owned editor endpoint is not ready or ownership changed: Port=$McpPort"
    }
    $result=Invoke-PartyHudMcpRpc $McpPort 'tools/call' @{
        name='call_tool';arguments=@{toolset_name=$Toolset;tool_name=$Tool;arguments=$Arguments}} -RequestTimeoutSeconds $RequestTimeoutSeconds
    if ($result.isError) {
        $detail=(@($result.content | Where-Object type -eq 'text' | ForEach-Object text) -join ' ')
        $exception=[InvalidOperationException]::new("Owned editor tool failed: $Toolset.$Tool")
        $exception.Data['McpToolError']=$detail
        throw $exception
    }
    if ($Raw) { return $result }
    $text=@($result.content | Where-Object type -eq 'text')
    if ($text.Count -ne 1) { throw "Unexpected owned editor response: $Tool" }
    return (($text[0].text | ConvertFrom-Json).returnValue)
}

function Start-PartyHudOwnedClientPIE {
    param([int]$McpPort,[Diagnostics.Process]$Process)
    $until=[DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    $ready=$false
    while ([DateTimeOffset]::UtcNow -lt $until) {
        Assert-Healthy
        $Process.Refresh()
        if ($Process.HasExited) { throw 'Owned HUD editor exited before MCP startup.' }
        try {
            $playing=Invoke-PartyHudEditorTool $McpPort 'EditorToolset.EditorAppToolset' 'IsPIERunning'
            $ready=$true
            break
        } catch { Start-Sleep -Milliseconds 400 }
    }
    if (-not $ready) { throw 'Owned HUD editor MCP did not start.' }
    if ($playing) { throw 'Unexpected existing PIE in a newly owned editor.' }
    $watcherTimedOut=$false
    try {
        $null=Invoke-PartyHudEditorTool $McpPort 'EditorToolset.EditorAppToolset' 'StartPIE' @{
            options=@{bSimulate=$false;playMode='PlayMode_InViewPort';warmupSeconds=0.0}} -RequestTimeoutSeconds 55
    } catch {
        $detail=[string]$_.Exception.Data['McpToolError']
        if ($detail -notmatch '(?i)timed out.*PIE|PIE.*timed out') { throw }
        # The engine's fixed 30-second watcher does not cancel its queued play
        # request. Never start a second PIE; observe the owned request instead.
        $watcherTimedOut=$true
    }
    $until=[DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    $playing=$false
    while([DateTimeOffset]::UtcNow -lt $until) {
        Assert-Healthy
        try {
            $playing=Invoke-PartyHudEditorTool $McpPort 'EditorToolset.EditorAppToolset' 'IsPIERunning' -RequestTimeoutSeconds 55
            if ($playing) { break }
        } catch { }
        Start-Sleep -Milliseconds 400
    }
    if (-not $playing) { throw 'Owned requested PIE did not become active.' }
    $owner=@($script:PartyHudClients.Values | Where-Object Port -eq $McpPort)[0]
    $owner.StartupWatcherTimedOut=$watcherTimedOut
    $observer=Invoke-PartyHudEditorTool $McpPort 'SlateInspectorToolset.SlateInspectorToolset' 'Observe' @{
        ref='';maxDepth=80}
    if (-not $script:PartyHudObservers) { $script:PartyHudObservers=@{} }
    $script:PartyHudObservers[$McpPort]=$observer
}

function Get-PartyHudSlateEvidence {
    param([string]$Snapshot,[string[]]$Names,[string]$SelfName,[string]$ReconnectingName='',
        [string]$IncapacitatedName='',[hashtable]$ExpectedHealth=@{},[string[]]$RemovedNames=@())
    $lines=@($Snapshot -split '\r?\n')
    if ($Names.Count -notin @(1,2) -or @($Names | Select-Object -Unique).Count -ne $Names.Count -or
        $SelfName -notin $Names -or @($Names | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count -or
        @($RemovedNames | Where-Object { [string]::IsNullOrWhiteSpace($_) -or $_ -in $Names }).Count -or
        @($RemovedNames | Select-Object -Unique).Count -ne $RemovedNames.Count) {
        throw 'HUD observer expects distinct retained public fixture names, one self and disjoint removed names.'
    }
    if (($ReconnectingName -and $ReconnectingName -notin $Names) -or
        ($IncapacitatedName -and $IncapacitatedName -notin $Names) -or
        ($ReconnectingName -and $ReconnectingName -ceq $IncapacitatedName)) {
        throw 'Expected grace/incapacitated members must be distinct known public names.'
    }
    $headers=@(for($i=0;$i -lt $lines.Count;$i++) { if ($lines[$i] -match 'text "파티 전투 정보 [0-9]+명') { $i } })
    $footers=@(for($i=0;$i -lt $lines.Count;$i++) { if ($lines[$i] -match 'text "접속·탈퇴 상태는 서버 확인 기준') { $i } })
    if ($headers.Count -ne 1 -or $footers.Count -ne 1 -or $footers[0] -le $headers[0] -or
        $lines[$headers[0]] -notmatch ('파티 전투 정보 '+$Names.Count+'명(?:[^0-9]|$)')) { return $null }
    # Count only the party panel, not player/boss bars elsewhere in the viewport.
    # A departed name may remain in a transient notice, but never in a member row.
    $panelLines=@($lines[($headers[0]+1)..($footers[0]-1)])
    $rowLabel='체력 [0-9]|\\n재접속 대기| · 상태 확인 중'
    if (@($panelLines | Where-Object { $_ -match $rowLabel }).Count -ne $Names.Count -or
        @($panelLines | Where-Object { $_ -match '^\s*progressbar\b' }).Count -ne
            ($Names.Count - $(if ($ReconnectingName) {1} else {0}))) { return $null }
    foreach ($removed in $RemovedNames) {
        if (@($lines | Where-Object { $_.Contains($removed) -and $_ -match $rowLabel }).Count) { return $null }
    }
    $rows=@()
    foreach ($name in $Names) {
        $escaped=[regex]::Escape($name)
        $indices=@(for($i=0;$i -lt $lines.Count;$i++) {
            if ($lines[$i] -match $escaped -and ($lines[$i] -match '체력 [0-9]' -or
                $lines[$i] -match '\\n재접속 대기')) { $i }
        })
        # Do not mistake a notification or a duplicate/stale viewport for a row.
        if ($indices.Count -ne 1) { return $null }
        $index=$indices[0]
        $line=$lines[$index]
        $self=$line.Contains('[나] ' + $name)
        if ($self -ne ($name -ceq $SelfName)) { return $null }
        $progress=$false
        for($i=$index+1;$i -lt $lines.Count;$i++) {
            if ($lines[$i] -match '접속·탈퇴 상태는 서버 확인 기준' -or
                @($Names | Where-Object { $lines[$i].Contains($_) }).Count) { break }
            if ($lines[$i] -match '^\s*progressbar\b') { $progress=$true;break }
        }
        if ($name -ceq $ReconnectingName) {
            if ($line -notmatch '\\n재접속 대기' -or $line -match '체력 [0-9]' -or $progress) { return $null }
            $rows+=@{name=$name;self=$self;reconnectGrace=$true;healthBarVisible=$false}
        } else {
            $health=[regex]::Match($line,'체력 ([0-9,]+(?:\.[0-9]+)?) / ([0-9,]+(?:\.[0-9]+)?)')
            $dead=$name -ceq $IncapacitatedName
            if (-not $health.Success -or -not $progress -or
                ($dead -and $line -notmatch '전투 불능') -or (-not $dead -and $line -notmatch '생존')) { return $null }
            $current=[double]::Parse($health.Groups[1].Value.Replace(',',''),[Globalization.CultureInfo]::InvariantCulture)
            $maximum=[double]::Parse($health.Groups[2].Value.Replace(',',''),[Globalization.CultureInfo]::InvariantCulture)
            if (($dead -and $current -ne 0) -or (-not $dead -and $current -le 0) -or
                $maximum -le 0 -or $current -gt $maximum) { return $null }
            if ($ExpectedHealth.ContainsKey($name)) {
                $expected=[double]$ExpectedHealth[$name]
                if ([double]::IsNaN($expected) -or [double]::IsInfinity($expected) -or $expected -lt 0 -or
                    [Math]::Abs($current-$expected) -gt 0.051) { return $null }
            }
            $rows+=@{name=$name;self=$self;reconnectGrace=$false;incapacitated=$dead;
                healthBarVisible=$true;health=$current;maxHealth=$maximum}
        }
    }
    return [ordered]@{headerCount=$Names.Count;rows=$rows;removedNames=@($RemovedNames);
        renderedSlate=$true;syntheticSnapshot=$false}
}

function Wait-PartyHudRenderedState {
    param([string]$ClientName,[string]$Phase,[string]$SelfName,[string]$ReconnectingName='',
        [string]$IncapacitatedName='',[hashtable]$ExpectedHealth=@{},[string[]]$RemovedNames=@(),
        [ValidateRange(0,600)][int]$MaximumWaitSeconds=0)
    $client=$script:PartyHudClients[$ClientName]
    if (-not $client) { throw "No owned HUD client: $ClientName" }
    $fixtureNames=@($members | ForEach-Object PublicName)
    if (@($RemovedNames | Where-Object { $_ -notin $fixtureNames }).Count) { throw 'Removed names must belong to the real fixture party.' }
    $until=[DateTimeOffset]::UtcNow.AddSeconds($(if ($MaximumWaitSeconds) {$MaximumWaitSeconds} else {$TimeoutSeconds}))
    while([DateTimeOffset]::UtcNow -lt $until) {
        Assert-Healthy
        $snapshot=Invoke-PartyHudEditorTool $client.Port 'SlateInspectorToolset.SlateInspectorToolset' 'Snapshot' @{
            ref='';maxDepth=80}
        $names=@($fixtureNames | Where-Object { $_ -notin $RemovedNames })
        $evidence=$null
        if ($snapshot.Contains('접속·탈퇴 상태는 서버 확인 기준')) {
            $evidence=Get-PartyHudSlateEvidence $snapshot $names $SelfName $ReconnectingName $IncapacitatedName $ExpectedHealth -RemovedNames $RemovedNames
        }
        if ($evidence) {
            $snapshotPath=Join-Path $runRoot "party-hud-$Phase-$ClientName.txt"
            $snapshot | Set-Content -LiteralPath $snapshotPath -Encoding UTF8
            $evidence.client=$ClientName
            $evidence.phase=$Phase
            $evidence.snapshot=$snapshotPath
            $evidence.snapshotSHA256=(Get-FileHash -LiteralPath $snapshotPath).Hash
            $evidence.observedAtUtc=[DateTimeOffset]::UtcNow.ToString('o')
            try {
                $capture=Invoke-PartyHudEditorTool $client.Port 'EditorToolset.EditorAppToolset' 'CaptureEditorImage' @{} -Raw -RequestTimeoutSeconds 55
                $image=@($capture.content | Where-Object type -eq 'image')
                if ($image.Count) { $data=$image[0].data }
                else { $data=(($capture.content[0].text | ConvertFrom-Json).returnValue.data) }
                if (-not $data) { throw 'No image data returned' }
                $imagePath=Join-Path $runRoot "party-hud-$Phase-$ClientName.png"
                [IO.File]::WriteAllBytes($imagePath,[Convert]::FromBase64String($data))
                $evidence.image=$imagePath
                $evidence.imageSHA256=(Get-FileHash -LiteralPath $imagePath).Hash
            } catch {
                # A valid rendered Slate observation is not a screenshot-quality
                # approval. Preserve that distinction if capture is unavailable.
                $evidence.imageCaptured=$false
            }
            $script:PartyHudEvidence.Add($evidence)
            Write-Host "PARTY_HUD_OBSERVED Phase=$Phase Client=$ClientName Rows=$($evidence.headerCount) SyntheticSnapshot=0"
            return
        }
        Start-Sleep -Milliseconds 400
    }
    if ($snapshot) { $snapshot | Set-Content -LiteralPath (Join-Path $runRoot "party-hud-failed-$Phase-$ClientName.txt") -Encoding UTF8 }
    throw "Rendered party HUD did not reach $Phase on $ClientName."
}

function Invoke-PartyHudVitalsScenario {
    # All mutation is the owned server's opt-in authorized-damage fixture. This
    # observer only releases file gates after both real clients were captured.
    Wait-Marker $serverLog 'PARTY_HUD_VITALS READY '
    $initial=@($script:PartyHudEvidence | Where-Object { $_.phase -eq 'initial' })
    if ($initial.Count -ne 2 -or @($initial | Where-Object client -eq 'client-1').Count -ne 1 -or
        @($initial | Where-Object client -eq 'client-2').Count -ne 1) {
        throw 'Capture both initial HUDs before releasing the damage gate.'
    }
    $targetName=$members[0].PublicName
    $survivorName=$members[1].PublicName
    $first=@($initial | Where-Object client -eq 'client-1')[0]
    $targetRows=@($first.rows | Where-Object name -CEQ $targetName)
    $survivorRows=@($first.rows | Where-Object name -CEQ $survivorName)
    if ($targetRows.Count -ne 1 -or $survivorRows.Count -ne 1) { throw 'Initial public party rows are missing or ambiguous.' }
    $targetBefore=[double]$targetRows[0].health
    $survivorBefore=[double]$survivorRows[0].health
    if ([double]::IsNaN($targetBefore) -or [double]::IsInfinity($targetBefore) -or $targetBefore -le 0 -or
        [double]::IsNaN($survivorBefore) -or [double]::IsInfinity($survivorBefore) -or $survivorBefore -le 0) {
        throw 'Initial party must be alive before releasing the damage gate.'
    }
    $null=New-Item -ItemType File -Path ($victoryGate+'.party-hud-damage') -ErrorAction Stop
    Wait-Marker $serverLog 'PARTY_HUD_VITALS DAMAGED '
    $matches=@(Select-String -LiteralPath $serverLog -Pattern 'PARTY_HUD_VITALS DAMAGED Before=([0-9.eE+-]+) After=([0-9.eE+-]+) SurvivorHP=([0-9.eE+-]+) AuthorizedServerDamage=1 DirectHPWrite=0')
    if ($matches.Count -ne 1) { throw 'Expected one authorized server damage observation.' }
    $match=$matches[0].Matches[0]
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $before=[double]::Parse($match.Groups[1].Value,$culture)
    $after=[double]::Parse($match.Groups[2].Value,$culture)
    $survivor=[double]::Parse($match.Groups[3].Value,$culture)
    if ([double]::IsNaN($before) -or [double]::IsInfinity($before) -or
        [double]::IsNaN($after) -or [double]::IsInfinity($after) -or
        [double]::IsNaN($survivor) -or [double]::IsInfinity($survivor) -or
        [Math]::Abs($before-$targetBefore) -gt 0.051 -or $after -le 0 -or $after -ge $before -or
        [Math]::Abs($survivor-$survivorBefore) -gt 0.051) { throw 'Server damage did not match the initial party vitals.' }
    $expected=@{}; $expected[$targetName]=$after; $expected[$survivorName]=$survivor
    foreach ($index in 0,1) {
        Wait-PartyHudRenderedState "client-$($index+1)" 'damaged' $members[$index].PublicName -ExpectedHealth $expected
    }
    $null=New-Item -ItemType File -Path ($victoryGate+'.party-hud-defeat') -ErrorAction Stop
    Wait-Marker $serverLog 'PARTY_HUD_VITALS INCAPACITATED TargetHP=0 DeathTag=1 SurvivorUnchanged=1 InProgress=1 NoMembershipChange=1'
    $expected[$targetName]=0
    foreach ($index in 0,1) {
        Wait-PartyHudRenderedState "client-$($index+1)" 'incapacitated' $members[$index].PublicName `
            -IncapacitatedName $targetName -ExpectedHealth $expected
    }
    return [ordered]@{authorizedServerDamage=$true;directHealthWrite=$false;initialTargetHealth=$before;
        damagedTargetHealth=$after;incapacitatedTargetHealth=0;survivorHealth=$survivor;
        damageAndDeathRenderedOnBothClients=$true;hardwareInputVerified=$false;naturalCombat=$false}
}

function Stop-PartyHudObservers {
    foreach($port in @($script:PartyHudObservers.Keys)) {
        try {
            $null=Invoke-PartyHudEditorTool $port 'SlateInspectorToolset.SlateInspectorToolset' 'Unobserve' @{
                identifier=$script:PartyHudObservers[$port]}
        } catch { } # A deliberately killed owned client has already closed MCP.
    }
}
