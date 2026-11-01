[CmdletBinding()]
param(
    [string]$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')),
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024,65535)][int]$Port = 18859,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 240,
    [switch]$InputCollisions,
    [switch]$Stun,
    [switch]$Knockback,
    [switch]$Airborne,
    [switch]$AirborneTerrain,
    [switch]$AirborneLateJoin,
    [ValidateSet('Airborne','Downed','GettingUp')][string]$AirborneLateJoinPhase = 'Airborne',
    [ValidateSet('Player','NPC')][string]$AirborneLateJoinTarget = 'Player',
    [switch]$Slow,
	[switch]$Immunity,
    [switch]$Burn,
    [switch]$MontageImmunity,
    [switch]$Stagger,
    [switch]$AuthoredStagger,
    [ValidateRange(0,200)][int]$PacketLagMinMilliseconds = 0,
    [ValidateRange(0,200)][int]$PacketLagMaxMilliseconds = 0,
    [ValidateRange(0,5)][int]$PacketLossPercent = 0,
    [switch]$Rendered
)
$ErrorActionPreference = 'Stop'
if ($AuthoredStagger) { $Stagger=$true }
if ($AirborneTerrain) { $Airborne=$true }
if ($AirborneLateJoin) { $Airborne=$true }
if ($AirborneTerrain -and $AirborneLateJoin) { throw 'Run airborne terrain and late-join scenarios separately.' }
if ($Rendered -and $AirborneLateJoin) { throw 'Airborne late join verifies runtime state without screenshots.' }
$airborneJoinStep = switch ($AirborneLateJoinPhase) { 'Downed' {2}; 'GettingUp' {3}; default {1} }
if ($AirborneLateJoinTarget -eq 'NPC') { $airborneJoinStep += 4 }
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$project = Join-Path $ProjectRoot 'Project_RPG.uproject'
$module = Join-Path $ProjectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$moduleHash = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
$moduleText = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($module))
if (-not $moduleText.Contains('RAIDMAP_E2E_LIGHT_HIT_PREPARED DataDrivenGrant=1 SharedGAClasses=1')) {
    throw 'Build the current data-driven shared-hit presentation probe first.'
}
if (([int][bool]$Stun + [int][bool]$Knockback + [int][bool]$InputCollisions + [int][bool]$Airborne + [int][bool]$Slow + [int][bool]$Immunity + [int][bool]$Burn + [int][bool]$MontageImmunity + [int][bool]$Stagger) -gt 1) { throw 'Choose one combat control scenario.' }
if ($Rendered -and ($Immunity -or $Burn -or $MontageImmunity -or $Stagger)) { throw 'This scenario verifies replicated rules and does not produce screenshots.' }
if ($PacketLagMinMilliseconds -gt $PacketLagMaxMilliseconds) { throw 'Packet lag minimum must not exceed maximum.' }
if ($Rendered -and !$InputCollisions -and !$Stun -and !$Knockback -and !$Airborne -and !$Slow) { throw 'Rendered capture requires a control or input scenario.' }
if ($InputCollisions -and !$moduleText.Contains('RAIDMAP_E2E_LIGHT_INPUT_COMPLETE')) { throw 'Build the current input collision probe first.' }
if ($Stun -and !$moduleText.Contains('RAIDMAP_E2E_STUN_COMPLETE')) { throw 'Build the current common stun probe first.' }
if ($Knockback -and !$moduleText.Contains('RAIDMAP_E2E_KNOCKBACK_COMPLETE')) { throw 'Build the current common knockback probe first.' }
if ($Airborne -and !$moduleText.Contains('RAIDMAP_E2E_AIRBORNE_COMPLETE')) { throw 'Build the common airborne probe first.' }
if ($AirborneTerrain -and !$moduleText.Contains('RAIDMAP_E2E_AIRBORNE_TERRAIN_COMPLETE')) { throw 'Build the airborne terrain probe first.' }
if ($AirborneLateJoin -and !$moduleText.Contains('RAIDMAP_E2E_AIRBORNE_LATE_JOIN_COMPLETE')) { throw 'Build the airborne late-join probe first.' }
if ($Slow -and !$moduleText.Contains('RAIDMAP_E2E_SLOW_COMPLETE')) { throw 'Build the shared slow probe first.' }
if ($MontageImmunity -and !$moduleText.Contains('RAIDMAP_E2E_MONTAGE_IMMUNITY_COMPLETE')) { throw 'Build the montage immunity probe first.' }
if ($Stagger -and !$moduleText.Contains('RAIDMAP_E2E_STAGGER_COMPLETE')) { throw 'Build the shared stagger probe first.' }
if ($AuthoredStagger -and !$moduleText.Contains('RAIDMAP_E2E_AUTHORED_STAGGER_COMPLETE')) { throw 'Build the authored stagger probe first.' }
if ($Burn -and !$moduleText.Contains('RAIDMAP_E2E_BURN_COMPLETE')) { throw 'Build the shared burn probe first.' }
if ($Immunity -and !$moduleText.Contains('RAIDMAP_E2E_IMMUNITY_COMPLETE')) { throw 'Build the shared immunity probe first.' }
$run = Join-Path $ProjectRoot ('Saved\Tests\SharedHit\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$logs = [Collections.Generic.List[string]]::new()
$previous = @{}
foreach ($name in @('UE_SKIP_UBT_SDK_SETUP','PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
function Start-Probe([string]$Name,[string]$Destination,[string]$Mode,[string]$Commands='t.MaxFPS 30') {
    $log = Join-Path $run ($Name + '.log')
    if ($Rendered -and $Name -eq 'client1') {
        $Commands='r.MotionBlurQuality 0,'+$Commands
        if ($Stun -or $Knockback -or $Airborne -or $Slow) { $Commands='fx.Niagara.ForceWaitForCompilationOnActivate 1,'+$Commands }
    }
    $arguments = @(('"'+$project+'"'),$Destination,$Mode,
        '-RPGBossMapE2E','-RPGBossMapE2ELightHit','-RPGNetTestMode','-RPGRequireIris','-UseIrisReplication=1',
        '-unattended','-nosplash','-nosound','-nosteam','-NoLoadingScreen','-DisablePython','-NoLiveCoding',
        ('-UserDir="'+(Join-Path $run ($Name+'-User'))+'"'),('-abslog="'+$log+'"'),('-ExecCmds="'+$Commands+'"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    if ($InputCollisions) { $arguments += '-RPGBossMapE2ELightHitInput' }
    if ($Stun) { $arguments += '-RPGBossMapE2EStun' }
    if ($Knockback) { $arguments += '-RPGBossMapE2EKnockback' }
    if ($Airborne) { $arguments += '-RPGBossMapE2EAirborne' }
    if ($AirborneTerrain) { $arguments += '-RPGBossMapE2EAirborneTerrain' }
    if ($AirborneLateJoin) {
        $arguments += @('-RPGBossMapE2EAirborneLateJoin', ('-RPGAirborneJoinPhase='+$AirborneLateJoinPhase),
            ('-RPGAirborneJoinDirectory="'+$run+'"'))
        if ($AirborneLateJoinTarget -eq 'NPC') { $arguments += '-RPGAirborneJoinNPC' }
        if ($Name -eq 'client2') { $arguments += '-RPGBossMapE2EAirborneJoinGate' }
    }
    if ($Slow) { $arguments += '-RPGBossMapE2ESlow' }
	if ($Immunity) { $arguments += '-RPGBossMapE2EImmunity' }
    if ($Burn) { $arguments += '-RPGBossMapE2EBurn' }
    if ($MontageImmunity) { $arguments += '-RPGBossMapE2EMontageImmunity' }
    if ($Stagger) { $arguments += '-RPGBossMapE2EStagger' }
    if ($AuthoredStagger) { $arguments += '-RPGBossMapE2EAuthoredStagger' }
    if ($PacketLagMaxMilliseconds -or $PacketLossPercent) {
        $arguments += @("-PktLagMin=$PacketLagMinMilliseconds","-PktLagMax=$PacketLagMaxMilliseconds","-PktLoss=$PacketLossPercent")
    }
    if ($Rendered -and $Name -eq 'client1') {
        New-Item -ItemType Directory -Path (Join-Path $run 'Captures') -Force | Out-Null
        $arguments += @('-RenderOffscreen','-windowed','-ResX=1280','-ResY=720',('-RPGLightHitCaptureDir="'+(Join-Path $run 'Captures')+'"'))
    } else {
        $arguments += @('-nullrhi','-DDC=(EnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $ProjectRoot 'Saved\Tests\SharedHit\DDC')+'"'))
    }
    $processes.Add((Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru))
    $logs.Add($log)
    Write-Host "Started $Name PID=$($processes[$processes.Count-1].Id) Log=$log"
    return $log
}
function Assert-Healthy {
    foreach ($process in $processes) { if ($process.HasExited) { throw "Probe exited; inspect $run" } }
    foreach ($log in $logs) {
        if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Quiet -Pattern 'RAIDMAP_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*): Error|Blueprint Runtime Error|LogScript: Error')) {
            throw "Probe failed; inspect $log"
        }
    }
}
function Wait-Marker([string]$Log,[string]$Marker) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        if ((Test-Path -LiteralPath $Log) -and (Select-String -LiteralPath $Log -SimpleMatch $Marker -Quiet)) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Missing '$Marker'; inspect $Log"
}
try {
    $env:UE_SKIP_UBT_SDK_SETUP = '1'
    foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
        [Environment]::SetEnvironmentVariable($name,$null,'Process')
    }
    $server = Start-Probe 'server' '/Game/Maps/bossmap' ('-server -port='+$Port)
    Wait-Marker $server 'RAIDMAP_E2E_START_DIAG ModeTick=1'
    $first = Start-Probe 'client1' '/Engine/Maps/Entry' '-game' ('t.MaxFPS 30,open 127.0.0.1:'+$Port)
    Wait-Marker $first 'RAIDMAP_E2E_CLIENT_STARTED'
	if ($AirborneLateJoin) {Wait-Marker $server 'RAIDMAP_E2E_LATE_JOIN_READY'}
    elseif ($AuthoredStagger) {Wait-Marker $server 'RAIDMAP_E2E_LATE_JOIN_READY'}
    elseif ($Stagger) {Wait-Marker $server 'RAIDMAP_E2E_STAGGER_WAIT_LATE_JOIN ActiveStagger=1'}
    elseif ($MontageImmunity) {Wait-Marker $server 'RAIDMAP_E2E_MONTAGE_IMMUNITY_WAIT_LATE_JOIN ActiveWindow=1'}
    elseif ($Burn) {Wait-Marker $server 'RAIDMAP_E2E_BURN_WAIT_LATE_JOIN ActiveBurn=1'}
    elseif ($Immunity) {Wait-Marker $server 'RAIDMAP_E2E_IMMUNITY_WAIT_LATE_JOIN ActiveImmunity=1'}
    elseif ($Slow) {Wait-Marker $server 'RAIDMAP_E2E_SLOW_WAIT_LATE_JOIN ActiveSlow=1'}
    elseif ($Stun) {Wait-Marker $server 'RAIDMAP_E2E_STUN_WAIT_LATE_JOIN ActiveStun=1'}
    else {Wait-Marker $server 'RAIDMAP_E2E_LATE_JOIN_READY'}
    $second = Start-Probe 'client2' '/Engine/Maps/Entry' '-game' ('t.MaxFPS 30,open 127.0.0.1:'+$Port)
    if ($AirborneLateJoin) {
        Wait-Marker $second 'RAIDMAP_E2E_AIRBORNE_JOIN_MAP_READY GameplayJoined=0'
        Wait-Marker $server ('RAIDMAP_E2E_AIRBORNE_WAIT_LATE_JOIN Step='+$airborneJoinStep+' ')
        Wait-Marker $second 'RAIDMAP_E2E_AIRBORNE_JOIN_RELEASE SendingJoin=1'
    }
    Wait-Marker $second 'RAIDMAP_E2E_CLIENT_STARTED'
	if ($AuthoredStagger) {
        Wait-Marker $server 'RAIDMAP_E2E_AUTHORED_STAGGER_COMPLETE PlayerSeconds=0.25 NPCSeconds=0.35 OriginalStartupGrants=1 PlayerPose=1 NPCPose=1 CancelCombat=1 NaturalExpiry=1 SecurityViolations=0 Clients=2'
    } elseif ($Stagger) {
        Wait-Marker $server 'RAIDMAP_E2E_STAGGER_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 CancelCombat=1 InputBlocked=1 NaturalExpiry=1 ExactRemoval=1 Immunity=1 SeparateFromStun=1 HigherStun=1 Death=1 LateJoin=1 SecurityViolations=0 Clients=2'
    } elseif ($MontageImmunity) {
        Wait-Marker $server 'RAIDMAP_E2E_MONTAGE_IMMUNITY_COMPLETE ServerMontage=1 OwnerObserver=1 Overlap=1 Seek=1 PauseResume=1 BlendOut=1 ForeignPreserved=1 SelectiveStun=1 Death=1 LateJoin=1 SecurityViolations=0 Clients=2'
    } elseif ($Burn) {
        Wait-Marker $server 'RAIDMAP_E2E_BURN_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 Ticks=1 Overlap=1 ExactCleanse=1 NaturalExpiry=1 Immunity=1 SourceDeath=1 StatusUI=1 LateJoin=1 SecurityViolations=0 Clients=2'
    } elseif ($Immunity) {
		Wait-Marker $server 'RAIDMAP_E2E_IMMUNITY_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 ScopedAbility=1 Overlap=1 SelectiveStun=1 ForeignPreserved=1 DamagePreserved=1 Death=1 LateJoin=1 SecurityViolations=0 Clients=2'
	} elseif ($Slow) {
        Wait-Marker $server 'RAIDMAP_E2E_SLOW_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 ActualMovement=1 StrongestFallback=1 Cleanse=1 NaturalExpiry=1 Immunity=1 Death=1 StatusUI=1 LateJoin=1 SecurityViolations=0 Clients=2'
    } elseif ($Airborne) {
        Wait-Marker $server 'RAIDMAP_E2E_AIRBORNE_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 ActualLanding=1 DownGetUp=1 InputBlocked=1 Converged=1 Immunity=1 SecurityViolations=0 Clients=2'
        if ($AirborneTerrain) { Wait-Marker $server 'RAIDMAP_E2E_AIRBORNE_TERRAIN_COMPLETE WalkableSlope=1 LedgeLowerFloor=1 DelayedLanding=1 BothRoles=1 SecurityViolations=0 Clients=2' }
        if ($AirborneLateJoin) {
            Wait-Marker $second ('RAIDMAP_E2E_AIRBORNE_JOIN_SNAPSHOT Step='+$airborneJoinStep+' ')
            Wait-Marker $server ('RAIDMAP_E2E_AIRBORNE_LATE_JOIN_COMPLETE JoinStep='+$airborneJoinStep+' SameExecution=1 NaturalLanding=1 DownGetUp=1 SecurityViolations=0 Clients=2')
        }
    } elseif ($Knockback) {
        Wait-Marker $server 'RAIDMAP_E2E_KNOCKBACK_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 InputBlocked=1 Converged=1 Wall=1 Replace=1 Stun=1 Immunity=1 Death=1 SecurityViolations=0 Clients=2'
    } elseif ($Stun) {
        Wait-Marker $server 'RAIDMAP_E2E_STUN_COMPLETE PlayerNPC=1 NPCPlayer=1 CancelCombat=1 NaturalExpiry=1 LateJoin=1 BlockInput=1 OverlapCleanse=1 Immunity=1 Lethal=1 Clients=2'
    } elseif ($InputCollisions) {
        Wait-Marker $server 'RAIDMAP_E2E_LIGHT_INPUT_PREPARED EquipmentGrant=1 IncomingServerEventFixture=1'
        Wait-Marker $server 'RAIDMAP_E2E_LIGHT_INPUT_COMPLETE EnhancedInput=1 AttackNaturalEnds=2 RollNaturalEnds=2 Clients=2 SecurityViolations=0 OutgoingDamage=1'
    } else {
        Wait-Marker $server 'RAIDMAP_E2E_LIGHT_HIT_PREPARED DataDrivenGrant=1 SharedGAClasses=1'
        Wait-Marker $server 'RAIDMAP_E2E_LIGHT_HIT_COMPLETE PlayerNPC=1 NPCPlayer=1 Directions=4 Clients=2 Duplicate=1 Invalid=1 Lethal=1'
    }
    foreach ($log in $logs) { Wait-Marker $log 'LogIris: Display: Iris ReplicationSystem[' }
    if ($Knockback -or $Airborne -or $Slow -or $Immunity -or $Burn -or $MontageImmunity -or $Stagger) {
        foreach ($log in $logs) { Wait-Marker $log ("RAIDMAP_E2E_KNOCKBACK_NET LagMin=$PacketLagMinMilliseconds LagMax=$PacketLagMaxMilliseconds Loss=$PacketLossPercent") }
    }
    foreach ($log in @($first,$second)) {
		if ($AuthoredStagger) {
            foreach ($checkpoint in 0..4) {Wait-Marker $log ('RAIDMAP_E2E_AUTHORED_STAGGER_CLIENT Step='+$checkpoint+' ')}
        } elseif ($Stagger) {
            $firstCheckpoint=if($log -eq $first){0}else{1}
            foreach ($checkpoint in $firstCheckpoint..9) {Wait-Marker $log ('RAIDMAP_E2E_STAGGER_CLIENT Step='+$checkpoint+' ')}
        } elseif ($MontageImmunity) {
            $firstCheckpoint=if($log -eq $first){0}else{1}
            foreach ($checkpoint in $firstCheckpoint..9) {Wait-Marker $log ('RAIDMAP_E2E_MONTAGE_IMMUNITY_CLIENT Step='+$checkpoint+' ')}
        } elseif ($Burn) {
            $firstCheckpoint=if($log -eq $first){0}else{1}
            foreach ($checkpoint in $firstCheckpoint..8) {Wait-Marker $log ('RAIDMAP_E2E_BURN_CLIENT Step='+$checkpoint+' ')}
        } elseif ($Immunity) {
			$firstCheckpoint=if($log -eq $first){0}else{1}
			foreach ($checkpoint in $firstCheckpoint..7) {Wait-Marker $log ('RAIDMAP_E2E_IMMUNITY_CLIENT Step='+$checkpoint+' ')}
		} elseif ($Slow) {
            $firstCheckpoint=if($log -eq $first){0}else{1}
            foreach ($checkpoint in $firstCheckpoint..10) {Wait-Marker $log ('RAIDMAP_E2E_SLOW_CLIENT Step='+$checkpoint+' ')}
        } elseif ($Airborne) {
            $firstCheckpoint=if($AirborneLateJoin -and $log -eq $second){$airborneJoinStep}else{0}
            foreach ($checkpoint in $firstCheckpoint..9) {Wait-Marker $log ('RAIDMAP_E2E_AIRBORNE_CLIENT Step='+$checkpoint+' ')}
        } elseif ($Knockback) {
            foreach ($checkpoint in 0..14) {Wait-Marker $log ('RAIDMAP_E2E_KNOCKBACK_CLIENT Step='+$checkpoint+' ')}
        } elseif ($Stun) {
            $firstCheckpoint=if($log -eq $first){0}else{1}
            foreach ($checkpoint in $firstCheckpoint..7) {Wait-Marker $log ('RAIDMAP_E2E_STUN_CLIENT Step='+$checkpoint+' ')}
        } elseif ($InputCollisions) {
            foreach ($checkpoint in 0..12) { Wait-Marker $log ('RAIDMAP_E2E_LIGHT_INPUT_CLIENT Step='+$checkpoint+' ') }
        } else {
            foreach ($checkpoint in 0..10) { Wait-Marker $log ('RAIDMAP_E2E_LIGHT_HIT_CLIENT Checkpoint='+$checkpoint+' ') }
        }
    }
    for ($i=0;$i -lt 8;++$i) { Start-Sleep -Milliseconds 250; Assert-Healthy }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash) { throw 'Runtime module changed during validation.' }
    $captures=@()
    if ($Rendered) {
        if($Slow){$captures=@(1,6|ForEach-Object {Join-Path $run ('Captures\Slow-Step'+$_+'.png')})}
        elseif($Airborne){$captures=@(1,2,3|ForEach-Object {Join-Path $run ('Captures\Airborne-Phase'+$_+'.png')})}
        elseif($Knockback){$captures=@((Join-Path $run 'Captures\Knockback-Active.png'))}
        elseif($Stun){$captures=@((Join-Path $run 'Captures\Stun-Active.png'))}
        else{$captures=@(4,5,11|ForEach-Object {Join-Path $run ('Captures\Step-'+$_.ToString('00')+'.png')})}
        foreach($capture in $captures) {if (!(Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).Length -eq 0) {throw "Missing rendered capture: $capture"}}
    }
    [ordered]@{passed=$true;editorModuleSHA256=$moduleHash;logs=@($logs);directions=$(if($InputCollisions -or $Stun -or $Knockback -or $Airborne -or $Slow -or $Immunity -or $Burn -or $MontageImmunity -or $Stagger){0}else{4});remoteClients=2;
        approvedServerDamageEventStimulus=$true;directAbilityGrantFixture=(!$AuthoredStagger);nativeMontagePlayback=($AuthoredStagger -or (!$Slow -and !$Immunity -and !$Burn -and !$MontageImmunity -and !$Stagger));
        dataDrivenReactionPolicy=$true;sharedOriginalAbilityClasses=$true;testOnlyDurability=10000;lethalFixtureHP=$(if($InputCollisions -or $Airborne -or $Burn -or $MontageImmunity -or $AuthoredStagger){$null}else{1});
        inputCollisionCases=$(if($InputCollisions){4}else{0});outgoingEquipmentGrantAndHitWindowVerified=[bool]$InputCollisions;
        commonStun=[bool]$Stun;stunLateJoinAndNaturalExpiry=[bool]$Stun;stunInputAndAIMovementGates=[bool]$Stun;
        commonStagger=[bool]$Stagger;staggerLateJoinVerified=[bool]($Stagger -and !$AuthoredStagger);staggerInputAndAIGates=[bool]($Stagger -and !$AuthoredStagger);
        authoredStaggerData=[bool]$AuthoredStagger;authoredStartupGrants=[bool]$AuthoredStagger;authoredPlayerAndNPCMontageInstances=[bool]$AuthoredStagger;
        montageWindowImmunity=[bool]$MontageImmunity;montageWindowLateJoinVerified=[bool]$MontageImmunity;serverMontageTimelineVerified=[bool]$MontageImmunity;clientImmunityScopesRejected=[bool]$MontageImmunity;
        commonBurn=[bool]$Burn;burnPeriodicDamageAndStatusUI=[bool]$Burn;burnLateJoinVerified=[bool]$Burn;
        commonSlow=[bool]$Slow;slowActualMovementAndStatusUI=[bool]$Slow;slowLateJoinVerified=[bool]$Slow;
		commonImmunity=[bool]$Immunity;immunityScopedAbilityAndOverlap=[bool]$Immunity;immunityLateJoinVerified=[bool]$Immunity;
        commonAirborne=[bool]$Airborne;airborneActualLandingDownAndGetUp=[bool]$Airborne;airborneLateJoinVerified=[bool]$AirborneLateJoin;guardianAirborneArtIsTemporary=[bool]$Airborne;
        airborneLateJoinPhase=$(if($AirborneLateJoin){$AirborneLateJoinPhase}else{$null});airborneLateJoinPendingTravelGate=[bool]$AirborneLateJoin;
        airborneLateJoinTarget=$(if($AirborneLateJoin){$AirborneLateJoinTarget}else{$null});
        airborneTerrainWalkableSlopeAndLedge=[bool]$AirborneTerrain;airborneTerrainGeometryReplicated=[bool]$AirborneTerrain;
        commonKnockback=[bool]$Knockback;ownerObserverDisplacementConvergence=[bool]($Knockback -or $Airborne -or $Slow);
        packetLagMinMilliseconds=$PacketLagMinMilliseconds;packetLagMaxMilliseconds=$PacketLagMaxMilliseconds;packetLossPercent=$PacketLossPercent;
        enhancedInputOrAttackWindowVerified=[bool]$InputCollisions;enhancedInputVerified=[bool]($InputCollisions -or $Stun -or $Knockback -or $Airborne -or $Slow -or $Stagger);physicalKeyboardVerified=$false;
        renderedCaptures=$captures;captureMotionBlurDisabled=[bool]$Rendered;captureObserverPawnHidden=[bool]($Rendered -and $InputCollisions);
        captureNiagaraWaitForCompilation=[bool]($Rendered -and ($Stun -or $Knockback -or $Airborne -or $Slow));
        renderedPixelsVerified=$false;shippingVerified=$false} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'SharedHit.proof.json') -Encoding UTF8
    Write-Host "PASS: Shared combat reaction on two Iris clients. Slow=$Slow Airborne=$Airborne Knockback=$Knockback Stun=$Stun InputCollisions=$InputCollisions RenderedCaptures=$Rendered Incoming damage-event fixture. Logs: $run"
} finally {
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
    foreach ($process in $processes) { if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }; $process.Dispose() }
}
