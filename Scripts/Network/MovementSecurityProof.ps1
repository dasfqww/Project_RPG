# Pure log validation: do not let duplicate markers stand in for another player.
function Get-MovementSecurityProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string[]] $ClientTexts,
        [ValidateRange(1, 16)] [int] $ClientCount,
        [bool] $VerifyAuthorizedMovement = $false,
        [bool] $VerifyAuthoredRoll = $false,
        [int] $PacketLagMinMs = 0,
        [int] $PacketLagMaxMs = 0,
        [int] $PacketLossPercent = 0
    )
    $allTexts = @($ServerText) + $ClientTexts
    foreach ($logText in $allTexts) {
        if ($logText -match 'RPG_MOVEMENT_NETTEST FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') {
            throw 'Movement proof contains a probe or runtime failure.'
        }
    }
    if ($ClientTexts.Count -ne $ClientCount) {
        throw 'Movement proof is missing a client log.'
    }
    $normal = [regex]::Matches($ServerText,
        'RPG_MOVEMENT_NETTEST NORMAL_RESULT Duration=([\d.]+) Distance=([\d.]+) Samples=(\d+) Walking=(\d+) FalsePositives=(\d+) PlayerId=(\d+)')
    if ($normal.Count -ne $ClientCount) {
        throw 'Movement proof requires one identified server measurement per client; rebuild stale binaries.'
    }
    $playerIds = @($normal | ForEach-Object { [int]$_.Groups[6].Value })
    if (@($playerIds | Sort-Object -Unique).Count -ne $ClientCount -or @($playerIds | Where-Object { $_ -le 0 }).Count) {
        throw 'Movement proof has duplicate or invalid server player identities.'
    }
    $serverIdentities = [regex]::Matches($ServerText, 'RPG_MOVEMENT_NETTEST PEER_IDENTIFIED Role=Server PlayerId=(\d+)')
    $serverEnds = [regex]::Matches($ServerText,
        'RPG_MOVEMENT_NETTEST SERVER_COMPLETE FalsePositives=0 PositiveControl=1 PlayerId=(\d+)')
    if ($serverIdentities.Count -ne $ClientCount -or $serverEnds.Count -ne $ClientCount) {
        throw 'Movement proof is missing per-player identification or positive controls.'
    }
    foreach ($playerId in $playerIds) {
        if (@($serverIdentities | Where-Object { [int]$_.Groups[1].Value -eq $playerId }).Count -ne 1 -or
            @($serverEnds | Where-Object { [int]$_.Groups[1].Value -eq $playerId }).Count -ne 1) {
            throw "Movement proof has duplicate or missing completion for player $playerId."
        }
        if ($VerifyAuthorizedMovement -and [regex]::Matches($ServerText,
            "RPG_MOVEMENT_NETTEST AUTHORIZED_COMPLETE Jump=1 Dash=1 Knockback=1 Overlap=1 ReentrantCancel=1 BudgetExceeded=1 Cancelled=3 Finished=1 PlayerId=$playerId(?!\d)").Count -ne 1) {
            throw "Authorized movement proof is missing for player $playerId."
        }
    }
    $clientIds = @()
    $clientRolls = @()
    foreach ($clientText in $ClientTexts) {
        $identities = [regex]::Matches($clientText, 'RPG_MOVEMENT_NETTEST PEER_IDENTIFIED Role=Client PlayerId=(\d+)')
        if ($identities.Count -ne 1) { throw 'Client movement proof must contain exactly one player identity.' }
        $playerId = [int]$identities[0].Groups[1].Value
        $clientIds += $playerId
        if ($playerIds -notcontains $playerId -or [regex]::Matches($clientText,
            "RPG_MOVEMENT_NETTEST CLIENT_COMPLETE Duration=[\d.]+ PlayerId=$playerId(?!\d)").Count -ne 1) {
            throw 'Client movement completion does not match its authoritative player.'
        }
        if ($VerifyAuthorizedMovement -and
            ($clientText -notmatch 'RPG_MOVEMENT_NETTEST CLIENT_AUTHORITY_REJECTED' -or
             $clientText -notmatch 'RPG_MOVEMENT_NETTEST CLIENT_JUMP_STARTED')) {
            throw 'Client authority/jump coverage is missing.'
        }
        if ($VerifyAuthoredRoll) {
            $rows = [regex]::Matches($clientText,
                'RPG_MOVEMENT_NETTEST ROLL_CLIENT_RESULT Run=(\d+) Cancelled=(\d+) MontageSamples=(\d+) PlayerId=(\d+)')
            if ($rows.Count -ne 2 -or [regex]::Matches($clientText,
                "RPG_MOVEMENT_NETTEST ROLL_CLIENT_COMPLETE Runs=2 Natural=1 Inactive=1 PlayerId=$playerId(?!\d)").Count -ne 1) {
                throw 'Client Roll proof is missing playback or completion.'
            }
            foreach ($run in 1, 2) {
                $row = @($rows | Where-Object { [int]$_.Groups[1].Value -eq $run -and [int]$_.Groups[4].Value -eq $playerId })
                if ($row.Count -ne 1 -or [int]$row[0].Groups[3].Value -lt 1 -or
                    [int]$row[0].Groups[2].Value -notin 0, 1 -or ($run -eq 1 -and [int]$row[0].Groups[2].Value -ne 0)) {
                    throw 'Client Roll proof contains a duplicate run or invalid lifetime.'
                }
                $clientRolls += [ordered]@{ playerId = $playerId; run = $run;
                    cancelledLocally = [bool][int]$row[0].Groups[2].Value; montageSamples = [int]$row[0].Groups[3].Value }
            }
        }
    }
    if (@($clientIds | Sort-Object -Unique).Count -ne $ClientCount) {
        throw 'Duplicate client identities cannot stand in for a missing player.'
    }
    $measurements = @($normal | ForEach-Object {
        $groups = $_.Groups
        $duration = [double]::Parse($groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
        $distance = [double]::Parse($groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
        if ($duration -lt 12 -or $distance -lt 1500 -or [int]$groups[3].Value -lt 60 -or
            [int]$groups[4].Value -lt [int]$groups[3].Value / 2 -or [int]$groups[5].Value -ne 0) {
            throw 'Server movement measurement has insufficient coverage or a false positive.'
        }
        [ordered]@{ playerId = [int]$groups[6].Value; durationSeconds = $duration; distanceCm = $distance;
            samples = [int]$groups[3].Value; walkingSamples = [int]$groups[4].Value;
            falsePositives = 0; discontinuityPositiveControlDetected = $true }
    })
    $serverRolls = @()
    $maximumConcurrent = 0
    $isolationChecks = 0
    if ($VerifyAuthoredRoll) {
        $rows = [regex]::Matches($ServerText,
            'RPG_MOVEMENT_NETTEST ROLL_SERVER_RESULT Run=(\d+) Cancelled=(\d+) Distance=([\d.]+) MontageSamples=(\d+) BudgetUsed=(\d+) AuthorizationRevoked=1 PlayerId=(\d+)')
        $ends = [regex]::Matches($ServerText,
            'RPG_MOVEMENT_NETTEST ROLL_SERVER_COMPLETE Runs=2 Natural=1 Cancelled=1 AuthorizationRevoked=1 PlayerId=(\d+) MaxConcurrent=(\d+) IsolationChecks=(\d+)')
        if ($rows.Count -ne 2 * $ClientCount -or $ends.Count -ne $ClientCount) {
            throw 'Server Roll proof requires two runs and one completion for every player.'
        }
        $preserved = [regex]::Matches($ServerText,
            'RPG_MOVEMENT_NETTEST ROLL_PEER_PRESERVED PlayerId=(\d+) OtherPlayerId=(\d+) Remaining=([\d.]+) Preserved=1')
        foreach ($playerId in $playerIds) {
            foreach ($run in 1, 2) {
                $row = @($rows | Where-Object { [int]$_.Groups[1].Value -eq $run -and [int]$_.Groups[6].Value -eq $playerId })
                if ($row.Count -ne 1 -or [int]$row[0].Groups[2].Value -ne ($run - 1) -or
                    [int]$row[0].Groups[4].Value -lt 1 -or [int]$row[0].Groups[5].Value -notin 0, 1) {
                    throw 'Server Roll proof contains a duplicate run or invalid cancellation.'
                }
                $distance = [double]::Parse($row[0].Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
                if ($run -eq 1 -and ($distance -lt 100 -or $distance -gt 700)) {
                    throw 'Server Roll proof does not cover a real natural movement.'
                }
                $serverRolls += [ordered]@{ playerId = $playerId; run = $run; cancelled = ($run -eq 2);
                    distanceCm = $distance; montageSamples = [int]$row[0].Groups[4].Value;
                    extraBudgetUsed = [bool][int]$row[0].Groups[5].Value; authorizationRevoked = $true }
            }
            $end = @($ends | Where-Object { [int]$_.Groups[1].Value -eq $playerId })
            if ($end.Count -ne 1 -or [int]$end[0].Groups[2].Value -lt 1 -or [int]$end[0].Groups[2].Value -gt $ClientCount) {
                throw 'Server Roll completion has a duplicate identity or invalid concurrency.'
            }
            $maximumConcurrent = [Math]::Max($maximumConcurrent, [int]$end[0].Groups[2].Value)
            $checks = [int]$end[0].Groups[3].Value
            if (@($preserved | Where-Object { [int]$_.Groups[1].Value -eq $playerId }).Count -ne $checks) {
                throw 'Peer cancellation isolation count does not match its evidence.'
            }
            $isolationChecks += $checks
        }
        $preservedPairs = @{}
        foreach ($row in $preserved) {
            if ($playerIds -notcontains [int]$row.Groups[1].Value -or
                $playerIds -notcontains [int]$row.Groups[2].Value -or $row.Groups[1].Value -eq $row.Groups[2].Value) {
                throw 'Peer cancellation evidence does not identify two distinct players.'
            }
            $pair = '{0}:{1}' -f $row.Groups[1].Value, $row.Groups[2].Value
            if ($preservedPairs.ContainsKey($pair)) {
                throw 'Duplicate peer preservation cannot count as independent isolation evidence.'
            }
            $preservedPairs[$pair] = $true
        }
        if ($ClientCount -gt 1) {
            $barriers = [regex]::Matches($ServerText, "RPG_MOVEMENT_NETTEST ROLL_BARRIER Players=$ClientCount StartAt=([\d.]+)")
            if ($barriers.Count -ne 1 -or $maximumConcurrent -lt 2 -or $isolationChecks -lt 1) {
                throw 'Multi-client Roll proof must observe concurrent root motion and preserve a live peer during cancellation.'
            }
        }
    }
    [ordered]@{
        packetLagMinMs = $PacketLagMinMs; packetLagMaxMs = $PacketLagMaxMs
        outgoingPacketLossPercent = $PacketLossPercent; clientCount = $ClientCount
        playerIdentityCoverageVerified = $true
        authorizedMovementChecksVerified = $VerifyAuthorizedMovement
        authoredRollGraphVerified = $VerifyAuthoredRoll
        serverMeasurements = $measurements
        serverRollMeasurements = $serverRolls
        clientRollMeasurements = $clientRolls
        concurrentRollObserved = ($VerifyAuthoredRoll -and $ClientCount -gt 1)
        maximumConcurrentRollPlayers = $maximumConcurrent
        peerCancellationIsolationChecks = $isolationChecks
    }
}
