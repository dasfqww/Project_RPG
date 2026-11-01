[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024, 65535)][int]$Port = 18812,
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 300,
    [switch]$ListenServer,
    [switch]$SkipSDKSetup,
    [switch]$TraceRPC,
    [switch]$DeathAbilities,
    [switch]$NaturalCombat,
    [switch]$CombatPhases,
    [switch]$SwordManVariant,
    [switch]$SummonMechanic,
    [switch]$SummonPlayerKill,
    [switch]$Dodge,
    [switch]$PlayerAttack,
    [switch]$SingleSwing,
    [switch]$JumpSmash,
    [switch]$AssultBlade,
    [switch]$ChargeSlash,
    [switch]$WhirlWind,
    [switch]$BossDeath,
    [switch]$BossAttackPresentation,
    [switch]$BossAreaStrike,
    [ValidateRange(10, 60)][int]$MaxFPS = 30
)

$ErrorActionPreference = 'Stop'
if ($SummonPlayerKill) { $SummonMechanic = $true }
if ($SingleSwing) { $PlayerAttack = $true }
if (([int]$JumpSmash.IsPresent + [int]$AssultBlade.IsPresent + [int]$ChargeSlash.IsPresent + [int]$WhirlWind.IsPresent) -gt 1) { throw 'Choose only one player skill.' }
$playerSkill = if ($JumpSmash) { 'JumpSmash' } elseif ($AssultBlade) { 'AssultBlade' } elseif ($ChargeSlash) { 'ChargeSlash' } elseif ($WhirlWind) { 'WhirlWind' } else { $null }
if ($playerSkill -and ($SingleSwing -or $PlayerAttack)) { throw 'Choose a player skill or basic PlayerAttack/SingleSwing, not both.' }
if ($playerSkill) { $PlayerAttack = $true }
if ($BossAreaStrike) { $BossAttackPresentation = $true }
if ($BossAttackPresentation) { $CombatPhases = $true }
if ($SwordManVariant) { $CombatPhases = $true }
if (([int]$CombatPhases.IsPresent + [int]$Dodge.IsPresent + [int]$PlayerAttack.IsPresent + [int]$BossDeath.IsPresent + [int]$SummonMechanic.IsPresent) -gt 1) { throw 'Choose one of CombatPhases, Dodge, PlayerAttack, BossDeath or SummonMechanic.' }
if ($CombatPhases -or $Dodge -or $PlayerAttack -or $BossDeath -or $SummonMechanic) { $NaturalCombat = $true }
if ($DeathAbilities -and $NaturalCombat) { throw 'Choose DeathAbilities or NaturalCombat, not both.' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'Project_RPG.uproject'
if (-not (Test-Path -LiteralPath $editor)) { throw "Missing editor: $editor" }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'))) {
    throw 'Build Project_RPGEditor first.'
}
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$moduleHashBefore = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
if ($SummonMechanic -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_MECHANIC_COMPLETE')) {
    throw 'Build the new SummonMechanic probe before starting clients; the current module does not include it.'
}
if ($SummonMechanic -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_CLIENT_TEAMS_READY')) {
    throw 'Build the replicated summon-team update before starting clients.'
}
if ($SummonPlayerKill -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_PLAYER_KILL_COMPLETE')) {
    throw 'Build the SummonPlayerKill probe before starting clients; the current module does not include it.'
}
if ($SummonPlayerKill -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_PLAYER_SLOT_READY')) {
    throw 'Build the quick-slot-aware SummonPlayerKill probe before starting clients; legacy skill input is suppressed by the current controller.'
}
if ($SummonPlayerKill -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_PLAYER_TARGET_DATA')) {
    throw 'Build the updated SummonPlayerKill target observer before starting clients.'
}
if ($SummonPlayerKill -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_PLAYER_AIM_CANDIDATE_READY')) {
    throw 'Build the current-aim-candidate-aware SummonPlayerKill probe before starting clients.'
}
if ($SummonPlayerKill -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_SUMMON_GUARDIAN_DIAG')) {
    throw 'Build the updated Guardian combat-state observation before starting clients.'
}
$bossDeathAssetPath = Join-Path $projectRoot 'Content\Blueprints\Character\NPC\BP_RaidNPCBase.uasset'
$bossDeathAssetHash = if ($BossDeath) { (Get-FileHash -LiteralPath $bossDeathAssetPath -Algorithm SHA256).Hash } else { $null }
$attackAssetHashes = [ordered]@{}
$variantAssetHashes = [ordered]@{}
if (-not $SummonMechanic -and -not $SwordManVariant -and
    -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($modulePath)).Contains('RAIDMAP_E2E_BASE_SELECTED')) {
    throw 'Build the explicit baseline boss selection before running legacy scenarios with the new playable encounter.'
}
if ($SummonMechanic) {
    foreach ($name in @('BP_RaidSwordManSummonMechanic', 'AIC_RaidSwordManSummonMechanic', 'BT_RaidSwordManSummonMechanic',
        'DA_RaidSwordManSummonMechanic', 'DA_RaidSwordManSummonMechanicStartUp', 'GA_RaidSwordManSummonMechanic')) {
        $relative = 'Content\Blueprints\Character\NPC\Raid\SwordManSummonMechanic\' + $name + '.uasset'
        $variantAssetHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $relative) -Algorithm SHA256).Hash
    }
}
if ($SwordManVariant) {
    foreach ($name in @('BP_RaidSwordManVariant', 'AIC_RaidSwordManVariant', 'BT_RaidSwordManVariant',
        'BB_RaidSwordManVariant', 'DA_RaidSwordManVariant', 'DA_RaidSwordManVariantStartUp',
        'GameplayAbility\GA_RaidSwordManVariant_GroundHazard', 'GameplayAbility\GA_RaidSwordManVariant_Dash',
        'GameplayAbility\GA_RaidSwordManVariant_Summon')) {
        $relative = 'Content\Blueprints\Character\NPC\Raid\SwordManVariant\' + $name + '.uasset'
        $variantAssetHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $relative) -Algorithm SHA256).Hash
    }
}
if ($BossAttackPresentation) {
    foreach ($name in @('Strike', 'HeavyStrike')) {
        foreach ($relative in @(
            "Content\Blueprints\Character\NPC\SwordMan\Presentation\A_RaidSwordMan_$name.uasset",
            "Content\Blueprints\Character\NPC\SwordMan\Presentation\AM_RaidSwordMan_$name.uasset",
            "Content\Blueprints\Character\NPC\SwordMan\GameplayAbility\GA_RaidSwordMan_$name.uasset")) {
            $attackAssetHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $relative) -Algorithm SHA256).Hash
        }
    }
    $relative = 'Content\GameplayCue\GC_Boss_AttackWarning.uasset'
    $attackAssetHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $relative) -Algorithm SHA256).Hash
    foreach ($relative in @('Content\Blueprints\Character\NPC\SwordMan\DA_RaidSwordMan.uasset',
        'Content\Blueprints\Character\NPC\SwordMan\BT_RaidSwordMan.uasset')) {
        $attackAssetHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $relative) -Algorithm SHA256).Hash
    }
}
$runRoot = Join-Path $projectRoot ('Saved\Tests\BossMap\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$runtimeFailurePattern = 'RAIDMAP_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*): Error'
if ($BossDeath -or $BossAttackPresentation -or $SummonMechanic) { $runtimeFailurePattern += '|LogScript: Error|Blueprint Runtime Error' }
$previousEnvironment = @{}
foreach ($name in @('UE_SKIP_UBT_SDK_SETUP', 'PROJECT_RPG_GAME_SERVER_ID',
    'PROJECT_RPG_DUNGEON_SESSION_ID', 'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Start-Probe {
    param([string]$Name, [string]$Destination, [string]$Mode, [string]$Extra = '')
    $log = Join-Path $runRoot ($Name + '.log')
    $arguments = @(
        ('"' + $project + '"'), $Destination, $Mode, $Extra,
        '-RPGBossMapE2E', '-RPGNetTestMode', '-RPGRequireIris', '-UseIrisReplication=1',
        '-unattended', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-NoLoadingScreen', '-DisablePython',
        '-DDC=(EnginePak,Local)', ('-LocalDataCachePath="' + (Join-Path $projectRoot 'Saved\Tests\BossState\DerivedDataCache') + '"'),
        ('-UserDir="' + (Join-Path $runRoot ($Name + '-User')) + '"'),
        ('-abslog="' + $log + '"'), ('-ExecCmds="t.MaxFPS ' + $MaxFPS + '"')
    )
    $arguments = @($arguments | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($TraceRPC) {
        $arguments += '-LogCmds="LogIris Verbose,LogIrisRpc Verbose"'
        $arguments += '-DPCvars="net.Iris.LogReplicationProtocols=1"'
    }
    elseif ($SummonPlayerKill) {
        # Distinguish input routing/eligibility failures from an NPC kill-count
        # failure without adding another native probe or bypassing activation.
        $arguments += '-LogCmds="LogAbilitySystem Verbose"'
    }
    if ($DeathAbilities) { $arguments += '-RPGBossMapE2EDeathAbilities' }
    if ($NaturalCombat) { $arguments += '-RPGBossMapE2ENaturalCombat' }
    if ($CombatPhases) { $arguments += '-RPGBossMapE2ECombatPhases' }
    if ($SwordManVariant) { $arguments += '-RPGBossMapE2ESwordManVariant' }
    if ($SummonMechanic) { $arguments += '-RPGBossMapE2ESummonMechanic' }
    if ($SummonPlayerKill) { $arguments += @('-RPGBossMapE2ESummonPlayerKill', '-RPGBossMapE2EAssultBlade') }
    if ($Dodge) { $arguments += '-RPGBossMapE2EDodge' }
    if ($PlayerAttack) { $arguments += '-RPGBossMapE2EPlayerAttack' }
    if ($SingleSwing) { $arguments += '-RPGBossMapE2ESingleSwing' }
    if ($JumpSmash) { $arguments += '-RPGBossMapE2EJumpSmash' }
    if ($AssultBlade) { $arguments += '-RPGBossMapE2EAssultBlade' }
    if ($ChargeSlash) { $arguments += '-RPGBossMapE2EChargeSlash' }
    if ($WhirlWind) { $arguments += '-RPGBossMapE2EWhirlWind' }
    if ($BossDeath) { $arguments += '-RPGBossMapE2EBossDeath' }
    if ($BossAttackPresentation) { $arguments += '-RPGBossMapE2EAttackPresentation' }
    if ($BossAreaStrike) { $arguments += '-RPGBossMapE2EAreaStrike' }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $processes.Add($process)
    Write-Host "Started $Name (PID $($process.Id)): $log"
    return $log
}

function Assert-ProbesHealthy {
    foreach ($process in $processes) {
        if ($process.HasExited) { throw "Probe exited ($($process.ExitCode)); inspect $runRoot" }
    }
    foreach ($log in @('server', 'client1', 'late-client')) {
        $probeLog = Join-Path $runRoot ($log + '.log')
        if ((Test-Path -LiteralPath $probeLog) -and
            (Select-String -LiteralPath $probeLog -Pattern $runtimeFailurePattern -Quiet)) {
            throw "Runtime/probe failure; inspect $probeLog"
        }
        if (($BossDeath -or $BossAttackPresentation) -and (Test-Path -LiteralPath $probeLog)) {
            # NPC scope: do not turn the pre-existing GameMode result-widget
            # startup warning into a death failure or silently ignore NPC BP faults.
            # A newly started client can have an existing but still empty log.
            [string]$text = Get-Content -LiteralPath $probeLog -Raw -Encoding UTF8
            foreach ($warning in [regex]::Matches($text, '(?s)LogScript: Warning: Accessed None.*?(?=\r?\n\[|\z)')) {
                if ($warning.Value -match '/Game/Blueprints/Character/NPC/|/Game/GameplayCue/GC_Boss_AttackWarning') {
                    throw "NPC Blueprint runtime warning; inspect $probeLog"
                }
            }
        }
    }
}

function Wait-Marker {
    param([string]$Path, [string]$Marker)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-ProbesHealthy
        if ((Test-Path -LiteralPath $Path) -and (Select-String -LiteralPath $Path -SimpleMatch $Marker -Quiet)) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Missing marker '$Marker'; inspect $Path"
}

try {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    # This is a local session-recovery test, not a backend settlement test.
    # Never inherit an assigned production dungeon or service credential.
    foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID', 'PROJECT_RPG_DUNGEON_SESSION_ID', 'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $map = '/Game/Maps/bossmap'
    $mode = '-server -port=' + $Port
    if ($ListenServer) { $map += '?listen'; $mode = '-game -port=' + $Port }
    $server = Start-Probe 'server' $map $mode
    Wait-Marker $server 'RAIDMAP_E2E_READY'
    Wait-Marker $server 'LogIris: Display: Iris ReplicationSystem['
    # BeginPlay readiness precedes Engine's remaining module/asset startup.
    # Connecting then can exhaust PendingNetDriver's timeout before the server
    # ever services packets. This existing marker is emitted from a game tick.
    Wait-Marker $server 'RAIDMAP_E2E_START_DIAG ModeTick=1'
    if ($ChargeSlash) { Wait-Marker $server 'RAIDMAP_E2E_CHARGE_DURABILITY_FIXTURE Authority=1 MaxHP=2500 EncounterIdle=1' }
    $first = Start-Probe 'client1' ('127.0.0.1:' + $Port) '-game' '-RPGBossMapE2ERequester'
    Wait-Marker $first 'RAIDMAP_E2E_CLIENT_STARTED'
    Wait-Marker $server 'RAIDMAP_E2E_LATE_JOIN_READY'
    $late = Start-Probe 'late-client' ('127.0.0.1:' + $Port) '-game'
    Wait-Marker $late 'RAIDMAP_E2E_CLIENT_STARTED'
    if (-not $DeathAbilities -and -not $NaturalCombat) {
        foreach ($log in @($first, $late)) {
            Wait-Marker $log 'RAIDMAP_E2E_CLIENT_QUIESCED MovementTick=0'
            Wait-Marker $log 'RAIDMAP_E2E_CLIENT_PAWN_LOSS_READY MovementQuietSeconds=1'
        }
    }
    if ($NaturalCombat) {
        if ($SwordManVariant) { Wait-Marker $server 'RAIDMAP_E2E_VARIANT_SELECTED Class=/Game/Blueprints/Character/NPC/Raid/SwordManVariant/BP_RaidSwordManVariant.BP_RaidSwordManVariant_C RuntimeOnly=1' }
        Wait-Marker $server 'RAIDMAP_E2E_NATURAL_COMBAT_STARTED BrainRestarted=1 InjectedDamage=0 ManualAbility=0'
        Wait-Marker $server 'RAIDMAP_E2E_NATURAL_SERVER_TELEGRAPH'
        Wait-Marker $server 'RAIDMAP_E2E_NATURAL_SERVER_DAMAGE'
        foreach ($log in @($first, $late)) {
            Wait-Marker $log 'LogIris: Display: Iris ReplicationSystem['
            Wait-Marker $log 'RAIDMAP_E2E_NATURAL_CLIENT_COMPLETE BossMovement=1 Telegraph=1 Indicator=1 ReplicatedDamage=1'
        }
        if ($SummonMechanic) {
            Wait-Marker $server 'RAIDMAP_E2E_VARIANT_SELECTED Class=/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic/BP_RaidSwordManSummonMechanic.BP_RaidSwordManSummonMechanic_C RuntimeOnly=1'
            Wait-Marker $server 'RAIDMAP_E2E_SUMMON_MECHANIC_WAVE Guardians=2 Ready=1 NavigationPaths=2'
            Wait-Marker $server 'RAIDMAP_E2E_SUMMON_MECHANIC_GUARDIAN_DAMAGE'
            foreach ($log in @($first, $late)) {
                Wait-Marker $log 'RAIDMAP_E2E_SUMMON_CLIENT_TEAMS_READY Guardians=2 HostileToLocalPawn=1'
                Wait-Marker $log 'RAIDMAP_E2E_SUMMON_MECHANIC_CLIENT_WAVE Guardians=2 Objective=2 ParticipantHPDecreased=1'
                Wait-Marker $log 'RAIDMAP_E2E_SUMMON_MECHANIC_CLIENT_PARTIAL Defeated=1 Required=2 Phase=1'
                Wait-Marker $log 'RAIDMAP_E2E_SUMMON_MECHANIC_CLIENT_COMPLETE Phase=2 ObjectiveInactive=1 WaveCleared=1'
            }
            Wait-Marker $server 'RAIDMAP_E2E_SUMMON_MECHANIC_COMPLETE GuardianNavigation=1 GuardianGAS=1 ProgressClients=2 ResumeClients=2 ServerKillStimulus=1'
            if ($SummonPlayerKill) {
                foreach ($log in @($first, $late)) {
                    Wait-Marker $log 'RAIDMAP_E2E_SUMMON_PLAYER_SLOT_READY OwnedSkillRegistered=1 OwnerSlotReplicated=1 EnhancedInput=1'
                }
                Wait-Marker $server 'RAIDMAP_E2E_SUMMON_PLAYER_KILL_REQUEST Skill=AssultBlade RemoteOwner=1 VictimHPFixture=1 FirstKillServerStimulus=1'
                Wait-Marker $server 'RAIDMAP_E2E_SUMMON_PLAYER_KILL_HIT Skill=AssultBlade AuthoredAbilityActive=1 ServerHitContext=1 HPZero=1'
                Wait-Marker $server 'RAIDMAP_E2E_SUMMON_PLAYER_KILL_COMPLETE Skill=AssultBlade EnhancedInput=1 ServerHitContext=1 NaturalEnd=1 SecurityUnchanged=1 VictimHPFixture=1 FirstKillServerStimulus=1 Clients=2'
            }
        }
        if ($BossDeath) {
            Wait-Marker $server 'RAIDMAP_E2E_BOSS_DEATH_SERVER_HIT'
            foreach ($log in @($first, $late)) {
                Wait-Marker $log 'RAIDMAP_E2E_BOSS_DEATH_CLIENT_COMPLETE Dead=1 HPZero=1 DeathTag=1 Montage=1 TelegraphCleared=1 Victory=1'
            }
            Wait-Marker $server 'RAIDMAP_E2E_BOSS_DEATH_SERVER_COMPLETE DeathAbility=1 NaturalEnd=1 BTStopped=1 MovementStopped=1 NoStaleDamage=1 Clients=2'
        }
        if ($CombatPhases) {
            if ($BossAttackPresentation) {
                foreach ($log in @($server, $first, $late)) {
                    Wait-Marker $log 'RAIDMAP_E2E_BOSS_ATTACK_MONTAGE Action=Strike Montage=AM_RaidSwordMan_Strike Active=1'
                    Wait-Marker $log 'RAIDMAP_E2E_BOSS_ATTACK_MONTAGE Action=HeavyStrike Montage=AM_RaidSwordMan_HeavyStrike Active=1'
                }
                # GAS suppresses local Cue execution on dedicated servers by default.
                # Require delivery to both remote clients (and the listen host only).
                foreach ($log in @($first, $late)) { Wait-Marker $log 'RAIDMAP_E2E_BOSS_ATTACK_CUE Observed=1' }
                if ($ListenServer) { Wait-Marker $server 'RAIDMAP_E2E_BOSS_ATTACK_CUE Observed=1' }
                Wait-Marker $server 'RAIDMAP_E2E_BOSS_ATTACK_CANCEL Phase=1 OldMontageOwnershipCleared=1'
                Wait-Marker $server 'RAIDMAP_E2E_PHASE_PATTERN_SELECTED Phase=0 Ability=GA_RaidSwordMan_Strike_C ServerOnly=1'
                Wait-Marker $server 'RAIDMAP_E2E_PHASE_PATTERN_SELECTED Phase=1 Ability=GA_RaidSwordMan_HeavyStrike_C ServerOnly=1'
            }
            Wait-Marker $server 'RAIDMAP_E2E_PHASE_SERVER_TRANSITION'
            Wait-Marker $server 'RAIDMAP_E2E_PHASE_SERVER_STALE_WINDOW Clear=1 ParticipantHPUnchanged=1'
            Wait-Marker $server 'RAIDMAP_E2E_PHASE_SERVER_HEAVY_DAMAGE'
            foreach ($log in @($first, $late)) {
                Wait-Marker $log 'RAIDMAP_E2E_PHASE_CLIENT_WINDUP'
                Wait-Marker $log 'RAIDMAP_E2E_PHASE_CLIENT_HEAVY Phase=1'
                Wait-Marker $log 'RAIDMAP_E2E_PHASE_CLIENT_COMPLETE Phase=1 HeavyTelegraph=1 HeavyDamageReplicated=1'
            }
            Wait-Marker $server 'RAIDMAP_E2E_PHASE_SERVER_COMPLETE PhaseTransition=1 OldAttackCancelled=1 HeavyGASDamage=1 Clients=2'
            if ($SwordManVariant) {
                Wait-Marker $server 'RAIDMAP_E2E_VARIANT_HAZARD_DAMAGE'
                foreach ($log in @($server, $first, $late)) { Wait-Marker $log 'RAIDMAP_E2E_VARIANT_HAZARD_ACTOR' }
                Wait-Marker $server 'RAIDMAP_E2E_VARIANT_RESET_STARTED RealWindup=1 Wiped=1 Resetting=1'
                Wait-Marker $server 'RAIDMAP_E2E_VARIANT_RESET_COMPLETE Cancelled=1 BTStopped=1 Stationary=1 NoStaleDamage=1 RestartedPhase0=1'
                Wait-Marker $server 'RAIDMAP_E2E_VARIANT_HAZARD_COMPLETE NaturalBT=1 GASDamage=1 IrisClients=2 ResetRemoved=1'
            }
            if ($BossAreaStrike) {
                Wait-Marker $server 'RAIDMAP_E2E_AREA_PARTICIPANTS_READY RemotePlayers=2 ServerPlacement=1 NaturalThreat=1'
                Wait-Marker $server 'RAIDMAP_E2E_AREA_SERVER_COMPLETE RemoteVictims=2 SameExecution=1 SameFrame=1 OnceEach=1 OwnedHPClients=2'
                foreach ($log in @($first, $late)) { Wait-Marker $log 'RAIDMAP_E2E_AREA_CLIENT_DAMAGE OwnHPDecreased=1' }
            }
        }
        if ($Dodge) {
            Wait-Marker $server 'RAIDMAP_E2E_DODGE_SERVER_REQUEST'
            Wait-Marker $server 'RAIDMAP_E2E_DODGE_DIRECTION_MATCH Server=1'
            Wait-Marker $server 'RAIDMAP_E2E_DODGE_SERVER_RESULT'
            foreach ($log in @($first, $late)) {
                Wait-Marker $log 'RAIDMAP_E2E_DODGE_INPUT_READY BoundMove=1 BoundRoll=1'
                Wait-Marker $log 'RAIDMAP_E2E_DODGE_CLIENT_COMPLETE'
            }
            Wait-Marker $server 'RAIDMAP_E2E_DODGE_SERVER_COMPLETE Input=1 GAS=1 Escaped=1 NoDamage=1 Clients=2'
        }
        if ($PlayerAttack) {
            Wait-Marker $server 'RAIDMAP_E2E_ATTACK_SERVER_REQUEST'
            if ($ChargeSlash) {
                Wait-Marker $server 'RAIDMAP_E2E_CHARGE_RAGE_FIXTURE Server=1 ReplicatedToOwner=1 RealRageActivation=0'
                Wait-Marker $server 'RAIDMAP_E2E_CHARGE_SERVER_HOLD InputPressed=1 BossHPUnchanged=1'
                Wait-Marker $server 'RAIDMAP_E2E_CHARGE_SERVER_RELEASE InputReleased=1 AttackSection=1'
            }
            if ($playerSkill) { Wait-Marker $server "RAIDMAP_E2E_SKILL_SERVER_HIT Skill=$playerSkill AuthoredAbilityContext=1 ServerHitQuery=1 " }
            else { Wait-Marker $server 'RAIDMAP_E2E_ATTACK_SERVER_HIT AuthoredAbilityContext=1 ' }
            if (-not $playerSkill -and -not (Select-String -LiteralPath $server -Pattern 'RAIDMAP_E2E_ATTACK_SERVER_HIT AuthoredAbilityContext=1 ServerWeaponOverlap=(?:1 ServerWeaponSweep=[01]|0 ServerWeaponSweep=1)' -Quiet)) {
                throw "Attack has no authoritative overlap/sweep proof; inspect $server"
            }
            foreach ($log in @($first, $late)) {
                Wait-Marker $log 'RAIDMAP_E2E_ATTACK_INPUT_READY NativeWeapon=1 Mapping=1 BoundAttack=1'
                Wait-Marker $log 'RAIDMAP_E2E_ATTACK_CLIENT_COMPLETE WeaponReplicated=1 BossDamageReplicated=1'
                if ($playerSkill) { Wait-Marker $log "RAIDMAP_E2E_SKILL_CLIENT_COMPLETE Skill=$playerSkill MontageReplicated=1 WeaponReplicated=1 BossDamageReplicated=1" }
                if ($ChargeSlash) { Wait-Marker $log 'RAIDMAP_E2E_CHARGE_CLIENT_PHASES Hold=1 Release=1 MontageReplicated=1' }
                if ($ChargeSlash) { Wait-Marker $log 'RAIDMAP_E2E_CHARGE_INPUT_READY RagePrerequisite=1' }
                if ($WhirlWind) { Wait-Marker $log 'RAIDMAP_E2E_CHANNEL_CLIENT_PHASES Loop=1 End=1 RepeatedHP=1 MontageReplicated=1' }
            }
            if ($WhirlWind) {
                Wait-Marker $server 'RAIDMAP_E2E_CHANNEL_SERVER_HOLD InputPressed=1 LoopSection=1'
                Wait-Marker $server 'RAIDMAP_E2E_CHANNEL_SERVER_STOP InputReleased=1 EndSection=1 RepeatedHits=1'
                Wait-Marker $server 'RAIDMAP_E2E_CHANNEL_SERVER_QUIET NaturalEnd=1 NoPostStopDamage=1 BossHPUnchanged=1'
                Wait-Marker $server 'RAIDMAP_E2E_SKILL_SERVER_COMPLETE Skill=WhirlWind Input=1 ServerHitQuery=1 GASDamage=1 Threat=1 Clients=2 HitCount='
                if (-not (Select-String -LiteralPath $server -Pattern 'RAIDMAP_E2E_SKILL_SERVER_COMPLETE Skill=WhirlWind .* HitCount=(?:[3-9]|[1-9]\d+)(?:\s|$)' -Quiet)) {
                    throw "Channel must apply at least three server damage effects; inspect $server"
                }
                if (-not (Select-String -LiteralPath @($server, $first, $late) -SimpleMatch 'RAIDMAP_E2E_CHANNEL_INPUT_RELEASE EnhancedInput=1 FrozenTiming=1 ' -Quiet)) {
                    throw "Channel must stop through authored Enhanced Input; inspect $runRoot"
                }
            }
            elseif ($playerSkill) { Wait-Marker $server "RAIDMAP_E2E_SKILL_SERVER_COMPLETE Skill=$playerSkill Input=1 ServerHitQuery=1 GASDamage=1 Threat=1 Clients=2 HitCount=1" }
            else { Wait-Marker $server 'RAIDMAP_E2E_ATTACK_SERVER_COMPLETE Input=1 WeaponHit=1 GASDamage=1 Threat=1 Clients=2' }
            if ($ChargeSlash -and -not (Select-String -LiteralPath @($server, $first, $late) -SimpleMatch 'RAIDMAP_E2E_CHARGE_INPUT_RELEASE EnhancedInput=1 FrozenTiming=1 ' -Quiet)) {
                throw "Charge must release through authored Enhanced Input; inspect $runRoot"
            }
            if ($SingleSwing -and -not (Select-String -LiteralPath $server -Pattern 'RAIDMAP_E2E_ATTACK_SERVER_COMPLETE .* HitCount=1(?:\s|$)' -Quiet)) {
                throw "Single swing must apply exactly one server damage effect; inspect $server"
            }
        }
        Wait-Marker $server 'RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2'
    }
    else {
        if ($DeathAbilities) { Wait-Marker $server 'RAIDMAP_E2E_DEATH_ABILITIES_EXECUTED' }
        else { Wait-Marker $server 'RAIDMAP_E2E_PAWNS_LOST' }
        foreach ($log in @($first, $late)) {
            Wait-Marker $log 'LogIris: Display: Iris ReplicationSystem['
            if ($DeathAbilities) {
                Wait-Marker $log 'RAIDMAP_E2E_CLIENT_DEFEATED PawnMissing=0 WidgetBound=1'
                Wait-Marker $log 'RAIDMAP_E2E_CLIENT_DEATH_PRESENTATION CapsuleDisabled=1 SamePawn=1'
            }
            else { Wait-Marker $log 'RAIDMAP_E2E_CLIENT_DEFEATED PawnMissing=1 WidgetBound=1' }
        }
        Wait-Marker $server 'RAIDMAP_E2E_DEFEAT_BARRIER Clients=2'
        Wait-Marker $first 'RAIDMAP_E2E_CLIENT_RETRY_CLICK ExistingRPC=1'
        Wait-Marker $first 'RAIDMAP_E2E_CLIENT_RETRY_GUARD Pending=1 DuplicateBlocked=1'
        foreach ($log in @($first, $late)) {
            Wait-Marker $log 'RAIDMAP_E2E_CLIENT_RECOVERED FullHealth=1 AvatarReady=1 LocalPossess=1 InputReady=1 ResultClosed=1'
            if ($DeathAbilities) { Wait-Marker $log 'RAIDMAP_E2E_CLIENT_DEATH_RECOVERED SamePawn=1 CapsuleEnabled=1 AnimationResumed=1 ExistingStartupWeaponsPreserved=1 Count=' }
        }
        if ($DeathAbilities) { Wait-Marker $server 'RAIDMAP_E2E_SERVER_COMPLETE SamePawn=1 NewBoss=1 Clients=2' }
        else { Wait-Marker $server 'RAIDMAP_E2E_SERVER_COMPLETE Repossessed=1 NewBoss=1 Clients=2' }
    }
    # Completion RPCs can precede later asynchronous network diagnostics.
    # Keep our processes alive briefly and inspect every log before declaring PASS.
    for ($check = 0; $check -lt 8; ++$check) {
        Start-Sleep -Milliseconds 250
        Assert-ProbesHealthy
    }
    if ($moduleHashBefore -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) {
        throw 'Editor module changed during boss map validation; rerun on one unchanged build.'
    }
    if ($BossDeath -and $bossDeathAssetHash -ne (Get-FileHash -LiteralPath $bossDeathAssetPath -Algorithm SHA256).Hash) {
        throw 'Raid NPC death presentation asset changed during validation; rerun on one unchanged asset.'
    }
    foreach ($entry in $attackAssetHashes.GetEnumerator()) {
        if ($entry.Value -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.Key) -Algorithm SHA256).Hash) {
            throw "Raid attack asset changed during validation: $($entry.Key)"
        }
    }
    foreach ($entry in $variantAssetHashes.GetEnumerator()) {
        if ($entry.Value -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.Key) -Algorithm SHA256).Hash) {
            throw "Variant asset changed during validation: $($entry.Key)"
        }
    }
    $proof = [ordered]@{
        schemaVersion = 1
        editorModuleSHA256 = $moduleHashBefore
        listenServer = [bool]$ListenServer
        swordManVariant = [bool]$SwordManVariant
        summonMechanic = [bool]$SummonMechanic
        guardianNavigationGASAndProgressReplicationVerified = [bool]$SummonMechanic
        summonTeamReplicationVerified = [bool]$SummonMechanic
        summonMechanicServerKillStimulus = [bool]$SummonMechanic
        summonMechanicPlayerKillInputVerified = [bool]$SummonPlayerKill
        summonMechanicPlayerKillSkill = $(if ($SummonPlayerKill) { 'AssultBlade' } else { $null })
        summonMechanicPlayerKillHealthFixture = [bool]$SummonPlayerKill
        summonMechanicPlayerKillQuickSlotRegistrationFixture = [bool]$SummonPlayerKill
        summonMechanicPlayerKillLiveAimCandidateGate = [bool]$SummonPlayerKill
        summonMechanicAllKillsFromPlayerInputVerified = $false
        swordManVariantBossResetAndRestartVerified = [bool]$SwordManVariant
        swordManVariantNaturalHazardVerified = [bool]$SwordManVariant
        swordManVariantAssetSHA256 = $variantAssetHashes
        jumpSmash = [bool]$JumpSmash
        assultBlade = [bool]$AssultBlade
        chargeSlash = [bool]$ChargeSlash
        whirlWind = [bool]$WhirlWind
        channelManualStopAndQuietVerified = [bool]$WhirlWind
        bossDeath = [bool]$BossDeath
        bossDeathNativeAttackFixture = [bool]$BossDeath
        bossDeathNaturalAbilityAndCleanupVerified = [bool]$BossDeath
        bossDeathPresentationAssetSHA256 = $bossDeathAssetHash
        bossAttackPresentation = [bool]$BossAttackPresentation
        bossAttackPresentationAssetSHA256 = $attackAssetHashes
        bossAttackMontageCueAndPhaseCancelVerified = [bool]$BossAttackPresentation
        bossPhasePatternSelectionVerified = [bool]$BossAttackPresentation
        bossAreaStrike = [bool]$BossAreaStrike
        bossAreaServerPositionFixture = [bool]$BossAreaStrike
        bossAreaTwoRemoteVictimsAndOwnedHealthVerified = [bool]$BossAreaStrike
        ragePrerequisiteFixture = [bool]$ChargeSlash
        bossDurabilityFixture = [bool]$ChargeSlash
        singleSwing = [bool]$SingleSwing
        maxFPS = $MaxFPS
        logs = @($server, $first, $late)
        backendEquipmentVerified = $false
        shippingVerified = $false
    }
    $proof | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'BossMap.proof.json') -Encoding UTF8
    if ($playerSkill) {
        $targeting = if ($JumpSmash) { 'ground' } elseif ($ChargeSlash) { 'camera aim with held/released input' } elseif ($WhirlWind) { 'camera aim with held/released channel' } else { 'soft' }
        Write-Host "PASS: authored $playerSkill Enhanced Input/$targeting targeting/montage, native weapon-granted skill, server HitQuery/GAS boss damage/threat and Iris montage/weapon/HP on both clients. RagePrerequisiteFixture=$ChargeSlash BossDurabilityFixture=$ChargeSlash; no real Rage activation or lethal-hit assertion. In-memory equipment fixture; no backend or Shipping proof. Logs: $runRoot"
    }
    elseif ($PlayerAttack) {
        Write-Host "PASS: authored attack input/montage, native weapon loadout, authoritative weapon overlap/sweep, GAS boss damage/threat and Iris weapon/HP on both clients. SingleSwing=$SingleSwing MaxFPS=$MaxFPS. In-memory server equipment fixture only; no backend acquisition/equip proof. Logs: $runRoot"
    }
    elseif ($Dodge) {
        Write-Host "PASS: natural boss target/Strike, authored Enhanced Input roll, server warp direction matches input, GAS/root motion/movement authorization, escape with unchanged HP and Iris movement/health on both clients. Authored roll is server-granted if absent; no player attack or invulnerability assertion. Logs: $runRoot"
    }
    elseif ($BossAreaStrike) {
        Write-Host "PASS: natural HeavyStrike hit two remote participants once each in the same execution/frame; both Iris clients observed their own HP decrease. Server position fixture, no manual boss attack/participant damage/threat injection; existing server-only phase threshold stimulus and montage/Cue checks retained. Logs: $runRoot"
    }
    elseif ($BossAttackPresentation) {
        Write-Host "PASS: phase-data-selected natural boss Strike/HeavyStrike, ASC montage on server/both Iris clients, cosmetic Cue on both clients, phase cancellation and no stale hit; nine unchanged raid assets. NullRHI: no GPU/VFX pixel assertion. Logs: $runRoot"
    }
    elseif ($SummonPlayerKill) {
        Write-Host "PASS: final Guardian killed through remote AssultBlade input, active authored ability/server hit context and natural ability end; two Iris clients observed progression and resumed combat. In-memory weapon/1 HP fixtures and first server-stimulus kill; no effect-context ability provenance, full-health balance, all-player-kills or timeout assertion. Logs: $runRoot"
    }
    elseif ($SummonMechanic) {
        Write-Host "PASS: real Guardian navigation/GAS damage and two Iris clients observing wave, partial progress and resumed combat. Server-only phase/kill stimuli; no player kill-input or timeout assertion. Logs: $runRoot"
    }
    elseif ($CombatPhases) {
        Write-Host "PASS: natural boss BT/GAS plus server-only threshold damage stimulus; old windup/ability cancellation, no stale hit, natural HeavyStrike and Iris phase/health/indicator/damage on both clients. No player attack/evade input assertion. Logs: $runRoot"
    }
    elseif ($BossDeath) {
        Write-Host "PASS: natural boss BT/GAS attack, server-only native lethal hit, authored death ability/montage/natural end, BT/movement/telegraph/threat cleanup, no stale damage and both Iris clients observing death/HP/tag/montage/Victory. No player input, backend reward or GPU assertion. Logs: $runRoot"
    }
    elseif ($NaturalCombat) {
        Write-Host "PASS: authored bossmap navigation, natural perception/BT movement/GAS attack, and Iris movement/telegraph/health on both remote clients. NullRHI: no GPU pixel assertion. Logs: $runRoot"
    }
    elseif ($DeathAbilities) {
        Write-Host "PASS: authored player death GAS on server/clients, late join, wipe/retry, same Pawn, capsule/animation recovery and new battle; existing startup weapon count is logged. Logs: $runRoot"
    }
    else { Write-Host "PASS: authored bossmap/classes, late join, quiesced Pawn-loss wipe, live retry dispatcher/production RPC, remote re-possession/GAS/input and new battle. Logs: $runRoot" }
}
finally {
    foreach ($process in $processes) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
    foreach ($name in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
}
