# Reads GAS/UMG state from Python; rendered bounds come from native Slate.
function Get-PlayerVitalsSlateLayout([string]$Snapshot,$Sample){
    $lines=@($Snapshot -split '\r?\n')
    $heads=@(for($i=0;$i -lt $lines.Count;$i++){if($lines[$i] -match '^window "[^\r\n]*\[NetMode:'){$i}})
    if($heads.Count -ne 1){throw 'Expected exactly one owned PIE window'}
    $begin=$heads[0];$end=$lines.Count
    for($i=$begin+1;$i -lt $lines.Count;$i++){if($lines[$i] -match '^window '){$end=$i;break}}
    $window=$lines[$begin]
    $number='-?(?:\d+(?:\.\d*)?|\.\d+)'
    $rectangle='\[pos=('+ $number+'),('+ $number+') size=('+ $number+'),('+ $number+')\]'
    function Rect([string]$Line){
        $m=[regex]::Match($Line,$rectangle)
        if(!$m.Success){throw 'Missing native Slate rectangle'}
        $v=@(1..4 | ForEach-Object {[double]::Parse($m.Groups[$_].Value,[Globalization.CultureInfo]::InvariantCulture)})
        if($v[2] -le 0 -or $v[3] -le 0){throw 'Slate control has no rendered area'}
        @{x=$v[0];y=$v[1];width=$v[2];height=$v[3]}
    }
    $bounds=Rect $window
    $ref=[regex]::Match($window,'\[ref=([^\]]+)\]').Groups[1].Value
    $result=@{windowRef=$ref;window=$bounds;resources=@{}}
    $same=$Sample.controls.health.text -ceq $Sample.controls.mana.text
    foreach($kind in @('health','mana')){
        $text=[string]$Sample.controls.$kind.text
        if([string]::IsNullOrWhiteSpace($text)){throw 'Missing actual native resource label'}
        $indices=@(for($i=$begin+1;$i -lt $end;$i++){
            if($lines[$i] -match ('^\s*text "'+[regex]::Escape($text)+'" ')){$i}
        })
        if($indices.Count -ne $(if($same){2}else{1})){throw ('Resource label missing or ambiguous: '+$kind)}
        $at=$indices[$(if($same -and $kind -eq 'mana'){1}else{0})]
        $barLine=$null
        for($i=$at-1;$i -gt $begin;$i--){if($lines[$i] -match '^\s*progressbar '){$barLine=$lines[$i];break}}
        if(!$barLine){throw ('Missing preceding resource bar: '+$kind)}
        $bar=Rect $barLine;$label=Rect $lines[$at]
        foreach($r in @($bar,$label)){
            if($r.x -lt $bounds.x -or $r.y -lt $bounds.y -or
               ($r.x+$r.width) -gt ($bounds.x+$bounds.width) -or
               ($r.y+$r.height) -gt ($bounds.y+$bounds.height)){throw 'Resource is outside its PIE window'}
        }
        $cx=$label.x+$label.width/2;$cy=$label.y+$label.height/2
        if($cx -lt $bar.x -or $cx -gt $bar.x+$bar.width -or $cy -lt $bar.y -or $cy -gt $bar.y+$bar.height){throw 'Label does not belong to its resource bar'}
        $barRef=[regex]::Match($barLine,'\[ref=([^\]]+)\]').Groups[1].Value
        if(!$barRef){throw 'Missing resource bar ref'}
        $result.resources[$kind]=@{text=$text;bar=$bar;label=$label;barRef=$barRef}
    }
    if($result.resources.health.barRef -ceq $result.resources.mana.barRef){throw 'Both labels resolved to one bar'}
    return $result
}

function Save-PlayerVitalsScreenshot([int]$Port,[string]$Ref,[string]$Path){
    $shot=Invoke-PartyHudEditorTool $Port 'SlateInspectorToolset.SlateInspectorToolset' 'Screenshot' @{ref=$Ref} -Raw -RequestTimeoutSeconds 55
    $images=@($shot.content | Where-Object type -eq 'image')
    $encoded=$null
    if($images.Count -eq 1){$encoded=$images[0].data}
    else{
        # call_tool returns FToolsetImage as a JSON returnValue, rather than
        # guaranteeing an MCP image content block.
        foreach($part in @($shot.content | Where-Object type -eq 'text')){
            $body=$part.text | ConvertFrom-Json
            if($body.returnValue.data -and $body.returnValue.mimeType -eq 'image/png'){$encoded=$body.returnValue.data;break}
        }
    }
    if(!$encoded){throw 'Screenshot returned no PNG image'}
    $bytes=[Convert]::FromBase64String(($encoded -replace '^data:image/png;base64,',''))
    if($bytes.Length -lt 8 -or [BitConverter]::ToString($bytes[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A'){throw 'Invalid screenshot PNG signature'}
    [IO.File]::WriteAllBytes($Path,$bytes)
    return @{bytes=$bytes.Length;sha256=(Get-FileHash -LiteralPath $Path).Hash}
}
function Wait-PlayerVitalsEditorReady($Client,[scriptblock]$KeepAlive){
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while([DateTime]::UtcNow -lt $deadline){
        if($Client.HasExited){throw 'Owned Vitals editor exited during initialization'}
        & $KeepAlive
        $log=Read-ActiveLog (Join-Path $temp 'client.log')
        if($log.Contains('Engine is initialized. Leaving FEngineLoop::Init()') -and
           $log.Contains('PLAYER_VITALS_PIE_OBSERVER STARTED ReadOnly=1')){return}
        Start-Sleep -Milliseconds 250
    }
    throw 'Owned Vitals editor initialization timed out'
}

function Get-PlayerVitalsPIEState {
    $path=Join-Path $runRoot 'Vitals.json'
    if(!(Test-Path -LiteralPath $path)){return}
    # Atomic publication replaces the old file. Permit that replacement while
    # reading a complete prior sample on Windows.
    $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
    try{return ($reader.ReadToEnd() | ConvertFrom-Json)}finally{$reader.Dispose()}
}

function Read-PlayerVitalsPIE {
    Set-StrictMode -Off
    $vitals=Get-PlayerVitalsPIEState
    if(!$vitals){return}
    if($vitals.failure){throw ('Vitals PIE observer failed: '+$vitals.failure)}
    if(@($vitals.samples).Count -and !$script:VitalsSlateLayout -and [DateTime]::UtcNow -ge $script:VitalsNextSnapshot){
        $script:VitalsNextSnapshot=[DateTime]::UtcNow.AddMilliseconds(500)
        $port=$script:ActionProgressPIEPort
        $null=Invoke-PartyHudEditorTool $port 'SlateInspectorToolset.SlateInspectorToolset' 'Observe' @{ref='';maxDepth=80} -RequestTimeoutSeconds 55
        $snapshot=Invoke-PartyHudEditorTool $port 'SlateInspectorToolset.SlateInspectorToolset' 'Snapshot' @{ref='';maxDepth=80;bIncludeSourceLocations=$false} -RequestTimeoutSeconds 55
        [IO.File]::WriteAllText((Join-Path $runRoot 'vitals-first-slate.txt'),$snapshot,[Text.UTF8Encoding]::new($false))
        try{$script:VitalsSlateLayout=Get-PlayerVitalsSlateLayout $snapshot $vitals.samples[-1]}
        catch{$script:VitalsSlateLayout=$null;$script:VitalsSlateFailure=$_.Exception.Message}
    }
    return ($null -ne $script:VitalsSlateLayout)
}

function Start-PlayerVitalsPIE($Client,[int]$Port,[scriptblock]$KeepAlive){
    Set-StrictMode -Off
    $script:PartyHudMcpSessions=@{}
    $script:ActionProgressPIEPort=$Port
    $script:PartyHudClients=@{vitals=@{Port=$Port;Process=$Client}}
    $script:VitalsSlateLayout=$null
    $script:VitalsSlateFailure=$null
    $script:VitalsNextSnapshot=[DateTime]::MinValue
    try{
        $null=Invoke-PartyHudEditorTool $Port 'EditorToolset.EditorAppToolset' 'StartPIE' @{
            options=@{bSimulate=$false;playMode='PlayMode_InEditorFloating';warmupSeconds=0.0}} -RequestTimeoutSeconds 55
    }catch{
        if([string]$_.Exception.Data['McpToolError'] -notmatch '(?i)timed out.*PIE|PIE.*timed out' -and
           $_.Exception.Message -notmatch 'MCP transport failed'){throw}
        # A timed-out HTTP watcher does not cancel the queued play request.
        # Never issue another start; observe the owned process's actual state.
    }
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while([DateTime]::UtcNow -lt $deadline){
        if($Client.HasExited){throw 'Owned editor exited before Vitals PIE start'}
        & $KeepAlive
        $null=Read-PlayerVitalsPIE
        $vitals=Get-PlayerVitalsPIEState
        if($vitals.pie_running){Write-Host ('PLAYER_VITALS_PIE STARTED PID='+$Client.Id);return}
        Start-Sleep -Milliseconds 250
    }
    throw 'Owned Vitals PIE start timed out'
}

function Complete-PlayerVitalsPIE {
    Set-StrictMode -Off
    $null=Read-PlayerVitalsPIE
    $vitals=Get-PlayerVitalsPIEState
    $shown=@($vitals.samples | Where-Object visible)
    $port=$script:ActionProgressPIEPort
    if(!$vitals.pie_running){throw 'PIE ended before Vitals inspection'}
    $null=Invoke-PartyHudEditorTool $port 'SlateInspectorToolset.SlateInspectorToolset' 'Observe' @{ref='';maxDepth=80}
    $snapshot=Invoke-PartyHudEditorTool $port 'SlateInspectorToolset.SlateInspectorToolset' 'Snapshot' @{ref='';maxDepth=80;bIncludeSourceLocations=$false}
    [IO.File]::WriteAllText((Join-Path $runRoot 'vitals-slate.txt'),$snapshot,[Text.UTF8Encoding]::new($false))
    if(!$vitals.started -or !$vitals.read_only -or !$shown.Count){throw 'Missing authored native Vitals samples'}
    $latest=$shown[-1]
    $layout=Get-PlayerVitalsSlateLayout $snapshot $latest
    [IO.File]::WriteAllText((Join-Path $runRoot 'VitalsLayout.json'),($layout | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $capture=Save-PlayerVitalsScreenshot $port $layout.windowRef (Join-Path $runRoot 'vitals-window.png')
    [IO.File]::WriteAllText((Join-Path $runRoot 'vitals-stop.ready'),'owned PIE end')
    $deadline=[DateTime]::UtcNow.AddSeconds(55)
    do{
        Start-Sleep -Milliseconds 100
        $null=Read-PlayerVitalsPIE
        $vitals=Get-PlayerVitalsPIEState
        if($vitals.pie_stopped -and !$vitals.pie_running){break}
    }while([DateTime]::UtcNow -lt $deadline)
    if(!$vitals.pie_stopped -or $vitals.pie_running){throw 'Owned Vitals PIE did not stop'}
    @{passed=$true;started=$true;stopped=$true;samples=$shown.Count;actualGasAndControlsMatch=$true;
      healthValues=@($shown.values.health | Select-Object -Unique);manaValues=@($shown.values.mana | Select-Object -Unique);
      slateTexts=$true;nativeSlateLayout=$layout;screenshot=$capture;uiStateInjected=$false;physicalInput=$false}
}
