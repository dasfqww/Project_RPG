[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../Network/DungeonPartyHudObservation.ps1')
$script:checks=0
function Check([bool]$Value,[string]$Name) {
    if (-not $Value) { throw "Vitals scenario check failed: $Name" }
    $script:checks++
}
# Pure orchestration doubles: no UE, HTTP, DB, identity or real screenshot.
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('ProjectRpgHudVitals-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $testRoot
$victoryGate=Join-Path $testRoot 'owned.gate'
$serverLog=Join-Path $testRoot 'server.log'
$members=@(@{PublicName='Alpha'},@{PublicName='Beta'})
$script:PartyHudEvidence=[Collections.Generic.List[object]]::new()
$script:PartyHudEvidence.Add(@{phase='initial';client='client-1';rows=@(
    @{name='Alpha';health=150},@{name='Beta';health=150})})
$script:PartyHudEvidence.Add(@{phase='initial';client='client-2';rows=@(
    @{name='Alpha';health=150},@{name='Beta';health=150})})
$script:captures=[Collections.Generic.List[string]]::new()
function Wait-Marker([string]$Log,[string]$Marker) {
    if ($Marker -like '* READY *') {
        Check (-not (Test-Path -LiteralPath ($victoryGate+'.party-hud-damage'))) 'no damage before initial observations'
    } elseif ($Marker -like '* DAMAGED *') {
        Check ((Test-Path -LiteralPath ($victoryGate+'.party-hud-damage')) -and
            -not (Test-Path -LiteralPath ($victoryGate+'.party-hud-defeat'))) 'only the damage gate released'
        'PARTY_HUD_VITALS DAMAGED Before=150 After=120 SurvivorHP=150 AuthorizedServerDamage=1 DirectHPWrite=0' |
            Set-Content -LiteralPath $Log -Encoding UTF8
    } elseif ($Marker -like '* INCAPACITATED *') {
        Check ($script:captures.Count -eq 2 -and (Test-Path -LiteralPath ($victoryGate+'.party-hud-defeat'))) 'both damaged views precede lethal gate'
    } else { throw 'Unexpected marker in pure scenario double.' }
}
function Wait-PartyHudRenderedState {
    param([string]$ClientName,[string]$Phase,[string]$SelfName,[string]$ReconnectingName='',
        [string]$IncapacitatedName='',[hashtable]$ExpectedHealth=@{})
    Check ($ExpectedHealth.Beta -eq 150 -and $ExpectedHealth.Alpha -eq $(if ($Phase -eq 'damaged') {120} else {0}) -and
        ($Phase -ne 'incapacitated' -or $IncapacitatedName -ceq 'Alpha')) 'capture has authoritative target and survivor vitals'
    $script:captures.Add($Phase+':'+$ClientName)
}
$result=Invoke-PartyHudVitalsScenario
Check ($script:captures.Count -eq 4 -and ($script:captures -join ',') -ceq
    'damaged:client-1,damaged:client-2,incapacitated:client-1,incapacitated:client-2') 'each phase reads both owned clients once'
Check ($result.authorizedServerDamage -and -not $result.directHealthWrite -and -not $result.naturalCombat -and
    -not $result.hardwareInputVerified -and $result.damagedTargetHealth -eq 120) 'proof distinguishes fixture from natural combat and input'
# These are only this test's exact known files; no recursive removal.
Remove-Item -LiteralPath ($victoryGate+'.party-hud-damage'),($victoryGate+'.party-hud-defeat'),$serverLog
$script:PartyHudEvidence.RemoveAt(1)
$rejected=$false
try { $null=Invoke-PartyHudVitalsScenario } catch { $rejected=$true }
Check ($rejected -and -not (Test-Path -LiteralPath ($victoryGate+'.party-hud-damage'))) 'a missing second view blocks damage gate'
$script:PartyHudEvidence.Clear()
$rejected=$false
try { $null=Invoke-PartyHudVitalsScenario } catch { $rejected=$true }
Check ($rejected -and -not (Test-Path -LiteralPath ($victoryGate+'.party-hud-damage'))) 'missing initial view blocks damage gate'
Remove-Item -LiteralPath $testRoot
[pscustomobject]@{passed=$true;orchestrationChecks=$script:checks;actualGameRun=$false} | ConvertTo-Json -Compress
