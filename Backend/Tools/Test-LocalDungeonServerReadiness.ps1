[CmdletBinding()]
param([Parameter(Mandatory)] [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$root = (Resolve-Path -LiteralPath $OutputDirectory).Path
$actualProcess = Get-Process -Id $PID
. (Join-Path $PSScriptRoot 'LocalDungeonServerReadiness.ps1')
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

foreach ($case in @('Ready', 'LoadingWithMarker', 'NoMarker', 'WrongAssignment', 'Terminal', 'PidReused', 'QueryFailure')) {
    & {
        $state = [pscustomobject]@{
            processId = $actualProcess.Id; processStartTimeUtcTicks = $actualProcess.StartTime.ToUniversalTime().Ticks
            dungeonSessionId = [Guid]::NewGuid().ToString('D'); serverId = 'readiness-fixture'
            serverAddress = '127.0.0.1:17777'; dungeonId = 'Dungeon.RaidSwordMan'; difficulty = 'Normal'
            mapPackage = '/Game/Maps/bossmap'; logPath = (Join-Path $root "$case.server.log")
        }
        $marker = "RPG_DUNGEON_SERVER_READY Session=$($state.dungeonSessionId) Server=$($state.serverId) Dungeon=$($state.dungeonId) Map=$($state.mapPackage) Difficulty=$($state.difficulty)"
        if ($case -eq 'NoMarker') { $marker = $marker.Replace('Dungeon.RaidSwordMan', 'Dungeon.Wrong') }
        $marker | Set-Content -LiteralPath $state.logPath -Encoding UTF8
        function Get-Process {
            param($Id, $ErrorAction)
            return [pscustomobject]@{ Id = $Id; Path = $actualProcess.Path; StartTime = $actualProcess.StartTime.AddTicks([int]($case -eq 'PidReused')) }
        }
        function Invoke-RestMethod {
            param($Method, $Uri, $Headers, $TimeoutSec)
            if ($case -eq 'QueryFailure') { throw 'Fixture backend unavailable' }
            return [pscustomobject]@{
                dungeonSessionId = $state.dungeonSessionId; serverId = $(if ($case -eq 'WrongAssignment') { 'other-server' } else { $state.serverId })
                serverAddress = $state.serverAddress; dungeonId = $state.dungeonId; difficulty = $state.difficulty
                state = $(if ($case -eq 'Terminal') { 'Failed' } elseif ($case -eq 'LoadingWithMarker') { 'Loading' } else { 'InProgress' })
            }
        }
        $accepted = $false
        try { $accepted = Wait-RPGLocalDungeonServerReady -State $state -Executable $actualProcess.Path -BackendUrl 'http://127.0.0.1:39999' -Headers @{} -TimeoutSeconds 1 }
        catch { }
        Require ($accepted -eq ($case -eq 'Ready')) "Readiness evidence was misclassified: $case"
        Write-Output "PASS readiness mock: $case"
    }
}
Write-Output 'Scope: matching process identity + authenticated backend + exact UE content marker; no real process stopped.'
