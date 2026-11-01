# Decide from a fresh backend snapshot and this client's actual map-load log.
# This helper never changes membership, settlement, gameplay or process state.
function Get-RPGTrainingServerDisposition {
    param($Allocation,$Session,[string]$ClientLog,[int]$LogStart=0)
    if(!$Allocation -or !$Session -or $LogStart -lt 0 -or $LogStart -gt $ClientLog.Length){return 'Keep'}
    if([string]$Session.dungeonSessionId -cne [string]$Allocation.DungeonSessionId -or
        [string]$Session.serverId -cne [string]$Allocation.ServerId -or
        [string]$Session.dungeonId -cne 'Progression.Training' -or
        [string]$Session.difficulty -cne 'Normal' -or
        @($Session.members).Count -ne 1 -or
        [string]$Session.state -cnotin @('Cleared','Failed','Closed')){return 'Keep'}
    $maps=[regex]::Matches($ClientLog.Substring($LogStart),
        '(?m)^.*LogWorld: Bringing World /Game/Maps/(?:Progression/)?(?:UEDPIE_\d+_)?(?<map>L_ProgressionTraining|LobbyMap)\.')
    $entered=$false
    foreach($entry in $maps){
        if($entry.Groups['map'].Value -ceq 'L_ProgressionTraining'){$entered=$true}
        elseif($entered -and $entry -eq $maps[$maps.Count-1]){return 'Retire'}
    }
    return 'Keep'
}
