# Decide from a fresh backend snapshot and this client's actual map-load log.
# This helper never changes membership, settlement, gameplay or process state.
. (Join-Path $PSScriptRoot 'TrainingDevelopmentContent.ps1')
function Get-RPGTrainingServerDisposition {
    param($Allocation,$Session,[string]$ClientLog,[int]$LogStart=0,[string]$Profile='Training')
    if(!$Allocation -or !$Session -or $LogStart -lt 0 -or $LogStart -gt $ClientLog.Length){return 'Keep'}
    try{Assert-RPGTrainingDevelopmentAllocation $Allocation $Profile}catch{return 'Keep'}
    if([string]$Session.dungeonSessionId -cne [string]$Allocation.DungeonSessionId -or
        [string]$Session.serverId -cne [string]$Allocation.ServerId -or
        [string]$Session.dungeonId -cne [string]$Allocation.DungeonId -or
        [string]$Session.difficulty -cne 'Normal' -or
        @($Session.members).Count -ne 1 -or
        [string]$Session.state -cnotin @('Cleared','Failed','Closed')){return 'Keep'}
    $maps=[regex]::Matches($ClientLog.Substring($LogStart),
        '(?m)^.*LogWorld: Bringing World (?<map>/Game/[A-Za-z0-9_/]+)\.')
    $entered=$false
    foreach($entry in $maps){
        $package=$entry.Groups['map'].Value -creplace '/UEDPIE_\d+_([^/]+)$', '/$1'
        if($package -ceq [string]$Allocation.MapPackage){$entered=$true}
        elseif($package -ceq '/Game/Maps/LobbyMap' -and $entered -and $entry -eq $maps[$maps.Count-1]){return 'Retire'}
        else{$entered=$false}
    }
    return 'Keep'
}
