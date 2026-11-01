# The native probe invokes only the owned active ability's existing GAS cancel.
# These gates synchronize evidence capture; they never set a UI snapshot or a goal.
$script:CancelChargeReleaseAt=$null
function Read-CancelChargePIE([string]$ClientLog){
    Set-StrictMode -Off
    $output=Join-Path $runRoot 'Cancellation.json'
    if(Test-Path -LiteralPath $output){
        $state=Get-Content -Raw -Encoding UTF8 -LiteralPath $output | ConvertFrom-Json
        if($state.failure){throw ('PIE cancellation observer: '+$state.failure)}
    }
    $cancelGate=Join-Path $runRoot 'cancel.ready'
    if(!(Test-Path -LiteralPath $cancelGate)){
        Read-ActionProgressPIE $ClientLog
        $active=@($script:ActionProgressPIESamples.ToArray() | Where-Object {!$_.released -and $_.seconds.Count -eq 1 -and $_.stages.Count -ge 1})
        if($active.Count){[IO.File]::WriteAllText($cancelGate,'cancel owned active charge')}
        return $false
    }
    if($ClientLog -match 'RPG_TRAINING_PLAYTEST RELEASE Action=3 '){
        if(!$script:CancelChargeReleaseAt){$script:CancelChargeReleaseAt=[DateTime]::UtcNow}
        $releaseGate=Join-Path $runRoot 'release-observed.ready'
        if(!(Test-Path -LiteralPath $releaseGate)){[IO.File]::WriteAllText($releaseGate,'native input released')}
        # Cover the authored 1.5s charge + 3s maximum hold + finisher window on
        # the server, not only the moment at which a predicted client cancelled.
        if($state -and $state.passed -and ([DateTime]::UtcNow-$script:CancelChargeReleaseAt).TotalSeconds -ge 8){
            Read-ActionProgressPIE $ClientLog
            return $true
        }
    }
    return $false
}

function Complete-CancelChargePIE {
    Set-StrictMode -Off
    $state=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $runRoot 'Cancellation.json') | ConvertFrom-Json
    if(!$state.passed -or !$state.cancelled -or !$state.lateReleaseStillHidden){throw 'Incomplete cancellation proof'}
    $server=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $runRoot 'ServerCancellation.json') | ConvertFrom-Json
    if(!$server.passed -or !$server.cancelled -or $server.role -ne 3){throw 'Server did not report an authoritative cancelled end'}
    $samples=$script:ActionProgressPIESamples.ToArray()
    $shown=@($samples | Where-Object {!$_.released -and $_.seconds.Count -eq 1})
    $hidden=@($samples | Where-Object {$_.released -and $_.seconds.Count -eq 0 -and !$_.ready})
    if(!$shown.Count -or !$hidden.Count){throw 'Missing actual Slate before/after cancellation'}
    $port=$script:ActionProgressPIEPort
    if(!(Invoke-PartyHudEditorTool $port 'EditorToolset.EditorAppToolset' 'IsPIERunning')){throw 'PIE ended before cancellation evidence'}
    $null=Invoke-PartyHudEditorTool $port 'EditorToolset.EditorAppToolset' 'StopPIE' @{} -RequestTimeoutSeconds 55
    if(Invoke-PartyHudEditorTool $port 'EditorToolset.EditorAppToolset' 'IsPIERunning'){throw 'Owned PIE did not stop'}
    @{passed=$true;scenario='GAS cancellation';started=$true;stopped=$true;cancelled=$true;lateReleaseStillHidden=$true;postReleaseObservationSeconds=8;uiStateInjected=$false;physicalInput=$false}
}
