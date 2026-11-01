[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$ClientCount = 2,

    [ValidateRange(1024, 65535)]
    [int]$Port = 17777,

    [ValidateRange(10, 600)]
    [int]$TimeoutSeconds = 90,

    [ValidateRange(10, 240)]
    [int]$MaxFPS = 60,

    # Zero inherits MaxFPS; explicit client limits exercise real frame-cadence races.
    [ValidateRange(0, 240)]
    [int]$ClientMaxFPS = 0,

    [string]$EngineRoot = "D:\UE_5.8",

    [string]$Map = "/Game/Maps/testmap",

    [string]$GameMode = "/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C",

    [string]$ProjectFile = "",

    [ValidateSet("Editor", "Game")]
    [string]$Runtime = "Editor",

    [switch]$SkipBuild,

    [switch]$BuildServerTarget,

    [switch]$VisibleClients,

    [switch]$VerifySkillInput,

    [switch]$VerifySkillDamage,

    [switch]$VerifySkillTargetDataAbuse,

    [switch]$VerifySkillMultiPeerAbuse,

    [switch]$VerifySkillEndRace,

    [switch]$VerifySkillFinalCap,

    [switch]$VerifyNPCDeath,

    [switch]$VerifyDungeonEjection,

    [switch]$VerifyDungeonQuarantine,

    [switch]$VerifySkillProgression,

    [switch]$VerifySkillTripods,

    [switch]$VerifyTargetDataAbuse,

    [switch]$VerifyTargetDataRate,

    [switch]$VerifyTargetDataDamage,

    [switch]$VerifyMovementSecurity,

    [switch]$VerifyAuthorizedMovement,

    [switch]$VerifyAuthoredRoll,

    [ValidateRange(0, 2000)]
    [int]$PacketLagMinMs = 0,

    [ValidateRange(0, 2000)]
    [int]$PacketLagMaxMs = 0,

    [ValidateRange(0, 50)]
    [int]$PacketLossPercent = 0,

    [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"
if ($VerifyDungeonQuarantine) { $VerifyDungeonEjection = $true }
if ($VerifyDungeonEjection) {
    if ($ClientCount -lt 2 -or $ClientCount -gt 4 -or $Runtime -ne 'Editor' -or $BuildServerTarget -or
        $VerifyNPCDeath -or $VerifySkillInput -or $VerifySkillDamage -or $VerifySkillTargetDataAbuse -or
        $VerifySkillMultiPeerAbuse -or $VerifySkillEndRace -or $VerifySkillFinalCap -or $VerifySkillProgression -or
        $VerifySkillTripods -or $VerifyTargetDataAbuse -or $VerifyTargetDataRate -or $VerifyTargetDataDamage -or
        $VerifyMovementSecurity -or $VerifyAuthorizedMovement -or $VerifyAuthoredRoll) {
        throw 'Run dungeon ejection/quarantine alone with 2-4 Editor clients.'
    }
    $GameMode = '/Script/Project_RPG.RPGDungeonEjectionNetworkGameMode'
}
if ($ClientMaxFPS -gt 0 -and $ClientMaxFPS -lt 10) { throw 'ClientMaxFPS must be 0 (inherit MaxFPS) or between 10 and 240.' }
$clientFrameRate = if ($ClientMaxFPS -eq 0) { $MaxFPS } else { $ClientMaxFPS }
if ($VerifyNPCDeath -and ($ClientCount -ne 2 -or $Runtime -ne 'Editor' -or
    $VerifySkillInput -or $VerifySkillDamage -or $VerifySkillFinalCap -or $VerifySkillMultiPeerAbuse -or
    $VerifySkillTargetDataAbuse -or $VerifySkillEndRace -or $VerifySkillProgression -or $VerifySkillTripods -or
    $VerifyTargetDataAbuse -or $VerifyTargetDataRate -or $VerifyTargetDataDamage -or
    $VerifyMovementSecurity -or $VerifyAuthorizedMovement -or $VerifyAuthoredRoll)) {
    throw 'Run NPC death separately with exactly two clients and the Editor runtime.'
}
if ($VerifySkillEndRace) { $VerifySkillMultiPeerAbuse = $true }
if ($VerifySkillFinalCap) {
    if ($VerifySkillTargetDataAbuse -or $VerifySkillMultiPeerAbuse) { throw 'Run authored final cap separately from malicious TargetData probes.' }
    $VerifySkillDamage = $true
}
if ($VerifySkillMultiPeerAbuse) { $VerifySkillTargetDataAbuse = $true }
if ($VerifySkillTargetDataAbuse) { $VerifySkillDamage = $true }
if ($VerifyTargetDataDamage) {
    if ($VerifyTargetDataRate) { throw 'Run TargetData damage and rate probes separately.' }
    $VerifyTargetDataAbuse = $true
}
if ($VerifySkillDamage) {
    $requiredSkillClients = if ($VerifySkillMultiPeerAbuse) { 2 } else { 1 }
    if ($ClientCount -ne $requiredSkillClients -or $Runtime -ne 'Editor' -or $VerifySkillProgression -or
        $VerifySkillTripods -or $VerifyTargetDataAbuse -or $VerifyTargetDataRate -or
        $VerifyMovementSecurity -or $VerifyAuthorizedMovement -or $VerifyAuthoredRoll) {
        throw "Run authored skill damage separately with exactly $requiredSkillClients clients and the Editor runtime."
    }
    $VerifySkillInput = $true
}
if ($VerifyTargetDataRate) { $VerifyTargetDataAbuse = $true }
if ($VerifyTargetDataAbuse -and ($ClientCount -ne 2 -or $Runtime -ne 'Editor' -or
    $VerifySkillInput -or $VerifySkillProgression -or $VerifySkillTripods -or
    $VerifyMovementSecurity -or $VerifyAuthorizedMovement -or $VerifyAuthoredRoll)) {
    throw 'Run TargetData abuse separately with exactly two clients and the Editor runtime.'
}
if ($VerifySkillTripods -and $ClientCount -lt 2) {
    throw 'Tripod privacy verification requires at least two clients.'
}
if ($VerifySkillTripods -and ($VerifySkillInput -or $VerifySkillProgression -or $VerifyMovementSecurity -or $VerifyAuthorizedMovement -or $VerifyAuthoredRoll)) {
    throw 'Run tripod/rejection checks separately from skill input and movement probes.'
}
if ($VerifySkillProgression) {
    $VerifySkillInput = $true
}
if ($VerifyAuthorizedMovement) {
    $VerifyMovementSecurity = $true
}
if ($VerifyAuthoredRoll) {
    $VerifyMovementSecurity = $true
}
if ($VerifyAuthoredRoll -and $VerifyAuthorizedMovement) {
    throw 'Run the authored Roll graph and generic authorization probes separately.'
}
if ($PacketLagMaxMs -lt $PacketLagMinMs) {
    throw 'PacketLagMaxMs must be at least PacketLagMinMs.'
}
if ($VerifyMovementSecurity -and $VerifySkillInput) {
    throw 'Run movement and skill probes separately; the movement positive control ends with a teleport.'
}

function Test-LogMarker {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Marker
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    return [bool](Select-String `
        -LiteralPath $Path `
        -SimpleMatch $Marker `
        -Quiet `
        -ErrorAction SilentlyContinue)
}

function Get-LogTail {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return "Log file was not created: $Path"
    }

    return (Get-Content -LiteralPath $Path -Tail 40 -ErrorAction SilentlyContinue) -join [Environment]::NewLine
}

$projectPath = if ([string]::IsNullOrWhiteSpace($ProjectFile)) {
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\Project_RPG.uproject"))
} else {
    [IO.Path]::GetFullPath($ProjectFile)
}
$projectRoot = Split-Path -Parent $projectPath
$editorPath = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$buildPath = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$gamePath = Join-Path $projectRoot "Binaries\Win64\Project_RPG.exe"
$cookedRoot = Join-Path $projectRoot "Saved\Cooked\Windows"

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Project file was not found: $projectPath"
}
if (-not (Test-Path -LiteralPath $buildPath -PathType Leaf)) {
    throw "Build.bat was not found. Pass -EngineRoot for the UE 5.8 installation."
}

if (-not $SkipBuild) {
	if ($VerifyTargetDataAbuse -or $VerifySkillDamage -or $VerifyDungeonEjection) {
		$activeProjectEditors = @(Get-CimInstance Win32_Process | Where-Object {
			$_.Name -match '^UnrealEditor(-Cmd)?\.exe$' -and $_.CommandLine -like '*Project_RPG*'
		})
		if ($activeProjectEditors.Count -gt 0) {
			throw 'Save and close project editors/tests before rebuilding a security network probe. No running editor will be stopped automatically.'
		}
	}
	$runtimeTarget = if ($Runtime -eq "Editor") {
		"Project_RPGEditor"
	} else {
		"Project_RPG"
	}
    Write-Host "Building $runtimeTarget (Development Win64)..."
    & $buildPath `
        $runtimeTarget `
        Win64 `
        Development `
        "-Project=$projectPath" `
        -WaitMutex `
        -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) {
		if ($Runtime -eq "Editor") {
			throw "Project_RPGEditor build failed. Close every Unreal Editor process using this project, then retry. Exit code: $LASTEXITCODE."
		}
		throw "Project_RPG build failed with exit code $LASTEXITCODE."
    }

	if ($BuildServerTarget) {
		Write-Host "Building Project_RPGServer (Development Win64)..."
		& $buildPath `
			Project_RPGServer `
			Win64 `
			Development `
			"-Project=$projectPath" `
			-WaitMutex
		if ($LASTEXITCODE -ne 0) {
			throw "Project_RPGServer requires a source-built engine distribution. Exit code: $LASTEXITCODE."
		}
    }
}

$runtimePath = if ($Runtime -eq "Editor") { $editorPath } else { $gamePath }
if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
	throw "Runtime executable was not found: $runtimePath"
}
if (($Runtime -eq "Game") -and
	-not (Test-Path -LiteralPath (Join-Path $cookedRoot "Project_RPG\AssetRegistry.bin") -PathType Leaf)) {
	throw "Game runtime needs Windows cooked data under $cookedRoot. Use -Runtime Editor for the uncooked development test."
}

$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ($VerifyTargetDataAbuse -or $VerifySkillDamage -or $VerifyNPCDeath -or $VerifyDungeonEjection) { $runStamp += '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) }
$logRoot = Join-Path $projectRoot "Saved\Logs\IrisMultiClient\$runStamp"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

$serverLog = Join-Path $logRoot "Server.log"
$serverUrl = "$Map`?listen?game=$GameMode"
$serverArguments = @()
if ($Runtime -eq "Editor") {
	$serverArguments += $projectPath
} else {
	$serverArguments += "-project=$projectPath"
	$serverArguments += "-basedir=$cookedRoot"
}
$serverArguments += @(
    $serverUrl,
    "-server",
    "-port=$Port",
    "-log",
    "-abslog=$serverLog",
    "-unattended",
    "-nosplash",
    "-NoSound",
    "-nullrhi",
    "-multiprocess",
    "-nosteam",
    "-NoLiveCoding",
    "-DisablePython",
    "-UseIrisReplication=1",
	"-ExecCmds=`"t.MaxFPS $MaxFPS`"",
    "-RPGNetTestMode",
    "-RPGRequireIris",
    "-RPGExpectedClients=$ClientCount"
)
if ($VerifySkillInput) {
    $serverArguments += "-RPGSkillNetE2E"
    $serverArguments += '-LogCmds="LogAbilitySystem Log"'
}
if ($VerifySkillDamage) {
    $serverArguments += @('-RPGSkillDamageE2E', '-RPGSkillEndOriginE2E')
    $damageModule = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
    $damageHashBefore = (Get-FileHash -LiteralPath $damageModule -Algorithm SHA256).Hash
}
if ($VerifySkillTargetDataAbuse) { $serverArguments += '-RPGSkillTargetDataAbuseE2E' }
if ($VerifySkillMultiPeerAbuse) { $serverArguments += '-RPGSkillMultiPeerE2E' }
if ($VerifySkillEndRace) { $serverArguments += '-RPGSkillClientEndRaceE2E' }
if ($VerifySkillFinalCap) { $serverArguments += '-RPGSkillFinalCapE2E' }
if ($VerifyNPCDeath) {
    $serverArguments += '-RPGNPCDeathE2E'
    $npcModule = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
    $npcHashBefore = (Get-FileHash -LiteralPath $npcModule -Algorithm SHA256).Hash
    $npcAssets = @('Content\Blueprints\Character\NPC\BP_RPGNPCBase.uasset',
        'Content\Blueprints\Character\NPC\DummyNPC\BP_Dummy_Gdn.uasset',
        'Content\Blueprints\Character\NPC\Gruntling\Guardian\DA_Guardian.uasset',
        'Content\Blueprints\Character\NPC\Gruntling\GameplayAbility\GA_Guardian_Death.uasset',
        'Content\Blueprints\Character\Shared\GameplayAbility\GA_NPC_Death_Base.uasset',
        'Content\Blueprints\Character\Shared\GameplayEffect\GE_Shared_ApplyDamage.uasset',
        'Content\Blueprints\Character\NPC\Gruntling\Anim\Montage\AM_Guardian_Death.uasset',
        'Content\Blueprints\Character\NPC\Gruntling\Guardian\BP_Guardian_DropItemAbility.uasset',
        'Content\Blueprints\GameData\Item\DT_ItemDrop.uasset')
    $npcAssetHashes = @($npcAssets | ForEach-Object { $path=Join-Path $projectRoot $_; [ordered]@{path=$path;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash} })
}
if ($VerifyDungeonEjection) {
    $serverArguments += '-RPGDungeonEjectionNetE2E'
    if ($VerifyDungeonQuarantine) { $serverArguments += '-RPGDungeonQuarantineNetE2E' }
    $ejectionModule = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
    $ejectionHashBefore = (Get-FileHash -LiteralPath $ejectionModule -Algorithm SHA256).Hash
    $ejectionSources = @(
        'Source\Project_RPG\Private\Tests\RPGDungeonEjectionNetworkProbe.h',
        'Source\Project_RPG\Private\Tests\RPGDungeonEjectionNetworkProbe.cpp',
        'Source\Project_RPG\Private\Tests\RPGDungeonEjectionTestFixture.h',
        'Source\Project_RPG\Private\Tests\RPGDungeonEjectionTestFixture.cpp',
        'Source\Project_RPG\Public\GameMode\RPGGameModeBase.h',
        'Source\Project_RPG\Private\GameMode\RPGGameModeBase.cpp',
        'Source\Project_RPG\Private\GameMode\RPGBossBattleGameMode.cpp',
        'Source\Project_RPG\Private\Character\RPGPlayer.cpp',
        'Source\Project_RPG\Public\Character\RPGPlayer.h',
        'Source\Project_RPG\Private\Player\RPGPlayerState.cpp',
        'Source\Project_RPG\Public\Player\RPGPlayerState.h',
        'Source\Project_RPG\Private\Component\RPGPlayerMovementComponent.cpp',
        'Source\Project_RPG\Public\Component\RPGPlayerMovementComponent.h',
        'Source\Project_RPG\Private\Component\RPGAbilitySystemComponent.cpp',
        'Source\Project_RPG\Public\Component\RPGAbilitySystemComponent.h',
        'Source\Project_RPG\Private\Controller\RPGPlayerController.cpp',
        'Source\Project_RPG\Public\Controller\RPGPlayerController.h',
        'Source\Project_RPG\Private\GameState\RPGGameStateBase.cpp',
        'Source\Project_RPG\Public\GameState\RPGGameStateBase.h',
        'Source\Project_RPG\Public\GameMode\RPGBossBattleGameMode.h',
        'Source\Project_RPG\Public\Security\RPGDungeonGameplayAuthority.h',
        'Source\Project_RPG\Public\Online\RPGDungeonMembershipSnapshot.h',
        'Source\Project_RPG\Private\Online\RPGDungeonMembershipSnapshot.cpp',
        'Scripts\Network\Run-IrisMultiClientTest.ps1',
        'Scripts\Network\DungeonEjectionNetworkProof.ps1',
        'Content\Maps\testmap.umap'
    )
    $ejectionSourceHashes = @($ejectionSources | ForEach-Object {
        $path = Join-Path $projectRoot $_
        [ordered]@{ path=$path; SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    })
}
if ($VerifyTargetDataAbuse) {
    $serverArguments += '-RPGTargetDataAbuseE2E', '-LogCmds="LogIrisRpc Verbose"'
    $wireModule = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
    $wireHashBefore = (Get-FileHash -LiteralPath $wireModule -Algorithm SHA256).Hash
}
if ($VerifyTargetDataRate) { $serverArguments += '-RPGTargetDataRateE2E' }
if ($VerifyTargetDataDamage) { $serverArguments += '-RPGTargetDataDamageE2E' }
if ($VerifySkillProgression) {
    $serverArguments += '-RPGSkillProgressionE2E'
}
if ($VerifySkillTripods) {
    $serverArguments += '-RPGSkillNetE2E', '-RPGSkillTripodE2E'
}
$simulationArguments = @("-PktLagMin=$PacketLagMinMs", "-PktLagMax=$PacketLagMaxMs",
    "-PktLoss=$PacketLossPercent")
$serverArguments += $simulationArguments
if ($VerifyMovementSecurity) {
    $serverArguments += '-RPGMovementSecurityE2E'
}
if ($VerifyAuthorizedMovement) {
    $serverArguments += '-RPGMovementSecurityAuthorizedE2E'
}
if ($VerifyAuthoredRoll) {
    $serverArguments += '-RPGMovementSecurityRollE2E'
}

$spawnedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$clientLogs = [System.Collections.Generic.List[string]]::new()
$succeeded = $false

try {
    Write-Host "Starting Iris dedicated server on 127.0.0.1:$Port..."
    $serverProcess = Start-Process `
        -FilePath $runtimePath `
        -ArgumentList $serverArguments `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden `
        -PassThru
    $spawnedProcesses.Add($serverProcess)

    # A cold uncooked editor can need longer than 45 seconds to load the map.
    $serverDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $serverDeadline) {
        if ($serverProcess.HasExited) {
            throw "Server exited before accepting clients.`n$(Get-LogTail -Path $serverLog)"
        }
        if (Test-LogMarker -Path $serverLog -Marker "RPG_NETTEST SERVER_STATE Iris=1") {
            break
        }
        if (Test-LogMarker -Path $serverLog -Marker "RPG_NETTEST IRIS_REQUIRED_BUT_INACTIVE") {
            throw "The GameNetDriver started without Iris.`n$(Get-LogTail -Path $serverLog)"
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not (Test-LogMarker -Path $serverLog -Marker "RPG_NETTEST SERVER_STATE Iris=1")) {
        throw "Server did not report an active Iris GameNetDriver.`n$(Get-LogTail -Path $serverLog)"
    }

    for ($index = 1; $index -le $ClientCount; ++$index) {
        $clientLog = Join-Path $logRoot "Client-$index.log"
        $clientLogs.Add($clientLog)
        $clientUrl = "127.0.0.1:$Port`?RPGNetTest=1"
        $clientArguments = @()
		if ($Runtime -eq "Editor") {
			$clientArguments += $projectPath
		} else {
			$clientArguments += "-project=$projectPath"
			$clientArguments += "-basedir=$cookedRoot"
		}
		$clientArguments += @(
            $clientUrl,
            "-game",
            "-log",
            "-abslog=$clientLog",
            "-unattended",
            "-nosplash",
            "-NoSound",
            "-multiprocess",
            "-nosteam",
            "-NoLiveCoding",
            "-DisablePython",
            "-UseIrisReplication=1",
			"-ExecCmds=`"t.MaxFPS $clientFrameRate`"",
            "-RPGExpectedClients=$ClientCount",
            "-ResX=960",
            "-ResY=540",
            "-WinX=$((($index - 1) % 2) * 980)",
            "-WinY=$([Math]::Floor(($index - 1) / 2) * 580)",
            "-windowed"
        )
        if (-not $VisibleClients) {
            $clientArguments += "-nullrhi"
        }
		if ($VerifySkillInput) {
			$clientArguments += "-RPGSkillNetE2E"
		}
        if ($VerifySkillDamage) { $clientArguments += @('-RPGSkillDamageE2E', '-RPGSkillEndOriginE2E') }
        if ($VerifySkillTargetDataAbuse) { $clientArguments += '-RPGSkillTargetDataAbuseE2E' }
        if ($VerifySkillMultiPeerAbuse) { $clientArguments += '-RPGSkillMultiPeerE2E' }
        if ($VerifySkillEndRace) { $clientArguments += '-RPGSkillClientEndRaceE2E' }
        if ($VerifySkillFinalCap) { $clientArguments += '-RPGSkillFinalCapE2E' }
        if ($VerifyNPCDeath) { $clientArguments += '-RPGNPCDeathE2E' }
        if ($VerifyDungeonEjection) {
            $clientArguments += '-RPGDungeonEjectionNetE2E', '-RPGNetTestMode'
            if ($VerifyDungeonQuarantine) { $clientArguments += '-RPGDungeonQuarantineNetE2E' }
        }
        if ($VerifySkillProgression) {
            $clientArguments += '-RPGSkillProgressionE2E'
        }
        if ($VerifySkillTripods) {
            $clientArguments += '-RPGSkillNetE2E', '-RPGSkillTripodE2E'
        }
        if ($VerifyTargetDataAbuse) { $clientArguments += '-RPGTargetDataAbuseE2E' }
        if ($VerifyTargetDataRate) { $clientArguments += '-RPGTargetDataRateE2E' }
        if ($VerifyTargetDataDamage) { $clientArguments += '-RPGTargetDataDamageE2E' }
        $clientArguments += $simulationArguments
        if ($VerifyMovementSecurity) {
            $clientArguments += '-RPGMovementSecurityE2E'
        }
        if ($VerifyAuthorizedMovement) {
            $clientArguments += '-RPGMovementSecurityAuthorizedE2E'
        }
        if ($VerifyAuthoredRoll) {
            $clientArguments += '-RPGMovementSecurityRollE2E'
        }

        $windowStyle = if ($VisibleClients) { "Normal" } else { "Hidden" }
        Write-Host "Starting client $index of $ClientCount..."
        $clientProcess = Start-Process `
            -FilePath $runtimePath `
            -ArgumentList $clientArguments `
            -WorkingDirectory $projectRoot `
            -WindowStyle $windowStyle `
            -PassThru
        $spawnedProcesses.Add($clientProcess)
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        foreach ($process in $spawnedProcesses) {
            if ($process.HasExited) {
                throw "A network test process exited before readiness. Logs: $logRoot"
            }
        }

        $serverReady = Test-LogMarker `
            -Path $serverLog `
            -Marker "RPG_NETTEST SERVER_READY"
        $readyClientCount = 0
        foreach ($clientLog in $clientLogs) {
            if (Test-LogMarker -Path $clientLog -Marker "RPG_NETTEST CLIENT_READY") {
                ++$readyClientCount
            }
        }

        $skillServerReady = -not $VerifySkillInput
        $skillClientsReady = -not $VerifySkillInput
        if ($VerifySkillInput) {
            $serverFailures = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                "RPG_SKILL_NETTEST FAILED" -ErrorAction SilentlyContinue).Count
            $serverCompletions = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                "RPG_SKILL_NETTEST SERVER_COMPLETE" -ErrorAction SilentlyContinue).Count
            $skillServerReady = $serverCompletions -ge $ClientCount
            $skillClientsReady = $true
            foreach ($clientLog in $clientLogs) {
                $clientFailed = Test-LogMarker -Path $clientLog `
                    -Marker "RPG_SKILL_NETTEST FAILED"
                if ($clientFailed -or $serverFailures -gt 0) {
                    throw "The network skill probe failed. Logs: $logRoot"
                }
                if (-not (Test-LogMarker -Path $clientLog `
                    -Marker "RPG_SKILL_NETTEST CLIENT_COMPLETE")) {
                    $skillClientsReady = $false
                }
                if ($VerifySkillProgression -and
                    (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_NETTEST CLIENT_LEVEL_CONFIRMED Level=4') -or
                     -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_NETTEST CLIENT_LEVEL_SNAPSHOT Level=4') -or
                     -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_NETTEST CLIENT_LEVEL_RESET Level=1 FullRefund=1'))) {
                    $skillClientsReady = $false
                }
            }
            if ($VerifySkillProgression) {
                $levelSnapshots = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                    'RPG_SKILL_NETTEST SERVER_LEVEL_SNAPSHOT Level=4' -ErrorAction SilentlyContinue).Count
                $levelResets = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                    'RPG_SKILL_NETTEST SERVER_LEVEL_RESET Level=1 FullRefund=1' -ErrorAction SilentlyContinue).Count
                $skillServerReady = $skillServerReady -and $levelSnapshots -eq $ClientCount -and $levelResets -eq $ClientCount
            }
        }

        $tripodsReady = -not $VerifySkillTripods
        if ($VerifySkillTripods) {
            if ((Test-LogMarker -Path $serverLog -Marker 'RPG_SKILL_NETTEST FAILED') -or
                (Test-LogMarker -Path $serverLog -Marker 'RPG_SKILL_TRIPOD_NETTEST FAILED')) {
                throw "Server tripod probe failed. Logs: $logRoot"
            }
            $completedTripods = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                'RPG_SKILL_TRIPOD_NETTEST SERVER_COMPLETE Selections=2 TierCleared=1 FullRefund=1 InsufficientSP=1 PrivacyBarrier=1 Activations=2 ChargeRelease=1 AssetCooldown=1' -ErrorAction SilentlyContinue).Count
            $tripodsReady = $completedTripods -eq $ClientCount
            foreach ($marker in @(
                'RPG_SKILL_TRIPOD_NETTEST SERVER_ACTIVATION Option=0 Policy=Instant Level=4 Scalar=2 Section=Attack',
                'RPG_SKILL_TRIPOD_NETTEST SERVER_ACTIVATION Option=1 Policy=Charge Level=4 Scalar=3 Section=Prepare',
                'RPG_SKILL_TRIPOD_NETTEST SERVER_RELEASE Section=Attack',
                'RPG_SKILL_TRIPOD_NETTEST SERVER_CHARGE_ENDED Cancelled=0',
                'RPG_SKILL_TRIPOD_NETTEST SERVER_ASSET_COOLDOWN Duration=',
                'RPG_SKILL_TRIPOD_NETTEST SERVER_ASSET_REINPUT Duration=')) {
                if (@(Select-String -LiteralPath $serverLog -SimpleMatch $marker -ErrorAction SilentlyContinue).Count -ne $ClientCount) {
                    $tripodsReady = $false
                }
            }
            foreach ($clientLog in $clientLogs) {
                if ((Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_NETTEST FAILED') -or
                    (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST FAILED')) {
                    throw "Client tripod probe failed. Logs: $logRoot"
                }
                if (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_COMPLETE Rejections=8 FullRefund=1 OwnerOnly=1 InsufficientSP=1 NativeButtons=1 Activations=2 ChargeRelease=1 AssetCooldown=1') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_PREDICTED Duration=') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_COOLDOWN Replicated=1 QueriesAgree=1 Duration=') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_REINPUT_BLOCKED Cooldown=1') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_REINPUT Replicated=1 Duration=') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_ACTIVATION Option=0 Policy=Instant Level=4 Scalar=2 Section=Attack') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_ACTIVATION Option=1 Policy=Charge Level=4 Scalar=3 Section=Prepare') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_RELEASE Section=Attack') -or
                    -not (Test-LogMarker -Path $clientLog -Marker 'RPG_SKILL_TRIPOD_NETTEST CLIENT_CHARGE_ENDED Cancelled=0') -or
                    -not (Test-LogMarker -Path $clientLog -Marker "RPG_SKILL_TRIPOD_NETTEST CLIENT_OWNER_ONLY_CONFIRMED Peers=$($ClientCount - 1) Entries=0 Revision=0 SPBaseline=1")) {
                    $tripodsReady = $false
                }
            }
        }

        $movementReady = -not $VerifyMovementSecurity
        if ($VerifyMovementSecurity) {
            foreach ($movementLog in (@($serverLog) + @($clientLogs))) {
                if ((Test-Path -LiteralPath $movementLog -PathType Leaf) -and
                    (Select-String -LiteralPath $movementLog -Pattern 'Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet)) {
                    throw "Movement runtime assertion/ensure failed. Inspect $movementLog"
                }
            }
            if (Test-LogMarker -Path $serverLog -Marker 'RPG_MOVEMENT_NETTEST FAILED') {
                throw "Server movement probe failed. Logs: $logRoot"
            }
            $movementCompletions = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                'RPG_MOVEMENT_NETTEST SERVER_COMPLETE FalsePositives=0 PositiveControl=1' `
                -ErrorAction SilentlyContinue).Count
            $movementReady = $movementCompletions -eq $ClientCount
            foreach ($clientLog in $clientLogs) {
                if (Test-LogMarker -Path $clientLog -Marker 'RPG_MOVEMENT_NETTEST FAILED') {
                    throw "Client movement probe failed. Logs: $logRoot"
                }
                if (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_MOVEMENT_NETTEST CLIENT_COMPLETE')) {
                    $movementReady = $false
                }
                if ($VerifyAuthorizedMovement -and
                    (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_MOVEMENT_NETTEST CLIENT_AUTHORITY_REJECTED') -or
                     -not (Test-LogMarker -Path $clientLog -Marker 'RPG_MOVEMENT_NETTEST CLIENT_JUMP_STARTED'))) {
                    $movementReady = $false
                }
                if ($VerifyAuthoredRoll -and -not (Test-LogMarker -Path $clientLog `
                    -Marker 'RPG_MOVEMENT_NETTEST ROLL_CLIENT_COMPLETE Runs=2 Natural=1 Inactive=1')) {
                    $movementReady = $false
                }
            }
            if ($VerifyAuthorizedMovement) {
                $authorizedCompletions = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                    'RPG_MOVEMENT_NETTEST AUTHORIZED_COMPLETE Jump=1 Dash=1 Knockback=1 Overlap=1 ReentrantCancel=1 BudgetExceeded=1 Cancelled=3 Finished=1' `
                    -ErrorAction SilentlyContinue).Count
                $movementReady = $movementReady -and $authorizedCompletions -eq $ClientCount
            }
            if ($VerifyAuthoredRoll) {
                $rollCompletions = @(Select-String -LiteralPath $serverLog -SimpleMatch `
                    'RPG_MOVEMENT_NETTEST ROLL_SERVER_COMPLETE Runs=2 Natural=1 Cancelled=1 AuthorizationRevoked=1' `
                    -ErrorAction SilentlyContinue).Count
                $movementReady = $movementReady -and $rollCompletions -eq $ClientCount
            }
        }

        $wireReady = -not $VerifyTargetDataAbuse
        if ($VerifyTargetDataAbuse) {
            foreach ($wireLog in (@($serverLog) + @($clientLogs))) {
                if ((Test-Path -LiteralPath $wireLog -PathType Leaf) -and
                    (Select-String -LiteralPath $wireLog -Pattern 'RPG_TARGETDATA_(WIRE|DAMAGE) FAILED|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet)) {
                    throw "TargetData wire probe failed. Inspect $wireLog"
                }
            }
            $wireReady = @(Select-String -LiteralPath $serverLog -SimpleMatch 'RPG_TARGETDATA_WIRE SERVER_COMPLETE' -ErrorAction SilentlyContinue).Count -eq 2 -and
                @(Select-String -LiteralPath $serverLog -Pattern 'Rejected reliable RPC function ServerSetReplicatedTargetData due to target not being owned by the connection\.' -ErrorAction SilentlyContinue).Count -eq 2
            foreach ($clientLog in $clientLogs) {
                if (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_TARGETDATA_WIRE CLIENT_COMPLETE')) { $wireReady = $false }
            }
            if ($VerifyTargetDataRate) {
                $wireReady = $wireReady -and (@(Select-String -LiteralPath $serverLog -SimpleMatch 'RPG_TARGETDATA_RATE SERVER_COMPLETE' -ErrorAction SilentlyContinue).Count -eq 2)
                foreach ($clientLog in $clientLogs) {
                    if (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_TARGETDATA_RATE CLIENT_COMPLETE')) { $wireReady = $false }
                }
            }
            if ($VerifyTargetDataDamage) {
                $wireReady = $wireReady -and (@(Select-String -LiteralPath $serverLog -SimpleMatch 'RPG_TARGETDATA_DAMAGE SERVER_COMPLETE' -ErrorAction SilentlyContinue).Count -eq 2)
                foreach ($clientLog in $clientLogs) {
                    if (-not (Test-LogMarker -Path $clientLog -Marker 'RPG_TARGETDATA_DAMAGE CLIENT_COMPLETE')) { $wireReady = $false }
                }
            }
        }

        $npcReady = -not $VerifyNPCDeath
        if ($VerifyNPCDeath) {
            $npcReady = Test-LogMarker -Path $serverLog -Marker 'RPG_NPC_NET SERVER_COMPLETE'
            foreach ($npcLog in (@($serverLog) + @($clientLogs))) {
                if ((Test-LogMarker -Path $npcLog -Marker 'RPG_NPC_NET FAILED') -or
                    (Test-LogMarker -Path $npcLog -Marker 'RPG_SKILL_NETTEST FAILED')) { throw "NPC death network probe failed. Logs: $logRoot" }
            }
            foreach ($clientLog in $clientLogs) { $npcReady = $npcReady -and (Test-LogMarker -Path $clientLog -Marker 'RPG_NPC_NET CLIENT_COMPLETE') }
        }
        $ejectionReady = -not $VerifyDungeonEjection
        if ($VerifyDungeonEjection) {
            foreach ($ejectionLog in (@($serverLog) + @($clientLogs))) {
                if ((Test-Path -LiteralPath $ejectionLog -PathType Leaf) -and
                    (Select-String -LiteralPath $ejectionLog -Pattern 'RPG_EJECTION_NET FAILED|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet)) {
                    throw "Dungeon ejection network probe failed. Inspect $ejectionLog"
                }
            }
            $ejectionReady = Test-LogMarker -Path $serverLog -Marker 'RPG_EJECTION_NET SERVER_COMPLETE'
            foreach ($clientLog in $clientLogs) {
                $ejectionReady = $ejectionReady -and ((Test-LogMarker -Path $clientLog -Marker 'RPG_EJECTION_NET CLIENT_COMPLETE') -or
                    (-not $VerifyDungeonQuarantine -and (Test-LogMarker -Path $clientLog -Marker 'RPG_EJECTION_NET CLIENT_DISCONNECTED')))
            }
        }
        if ($serverReady -and $readyClientCount -eq $ClientCount -and
            $skillServerReady -and $skillClientsReady -and $tripodsReady -and $movementReady -and $wireReady -and $npcReady -and $ejectionReady) {
            $succeeded = $true
            break
        }

        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)

    if (-not $succeeded) {
        throw "Timed out waiting for $ClientCount Iris clients. Server log tail:`n$(Get-LogTail -Path $serverLog)"
    }

    if ($VerifyDungeonEjection) {
        $succeeded = $false
        . (Join-Path $PSScriptRoot 'DungeonEjectionNetworkProof.ps1')
        $scenario = if ($VerifyDungeonQuarantine) { 'Quarantine' } else { 'Ejection' }
        $ejectionProof = Get-DungeonEjectionNetworkProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
            -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) `
            -ClientCount $ClientCount -Scenario $scenario -PacketLagMinMs $PacketLagMinMs `
            -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent
        $ejectionHashAfter = (Get-FileHash -LiteralPath $ejectionModule -Algorithm SHA256).Hash
        if ($ejectionHashBefore -ne $ejectionHashAfter) { throw 'Project module changed during ejection test.' }
        foreach ($source in $ejectionSourceHashes) {
            if ((Get-FileHash -LiteralPath $source.path -Algorithm SHA256).Hash -ne $source.SHA256) { throw "Ejection source/map changed during test: $($source.path)" }
        }
        $ejectionProof['moduleSHA256'] = $ejectionHashAfter
        $ejectionProof['sourceAndMapHashes'] = $ejectionSourceHashes
        # Unreal holds these logs open. Final immutable hashes are added only
        # after owned-process cleanup; -KeepRunning evidence stays provisional.
        $ejectionProof['logHashes'] = @()
        $ejectionProof['logsFinalized'] = $false
        $ejectionProof | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $logRoot 'DungeonEjectionNetwork.proof.json') -Encoding UTF8
        $succeeded = $true
        Write-Host "PASS: Dungeon $scenario native fixture over actual Iris sockets with $ClientCount clients. Synthetic identities/receipt; not backend, authored input or Shipping coverage."
    }

    if ($VerifyNPCDeath) {
        $succeeded = $false
        . (Join-Path $PSScriptRoot '..\Tests\NPCDeathNetworkProof.ps1')
        $npcProof = Get-NPCDeathNetworkProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
            -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 })
        $npcHashAfter = (Get-FileHash -LiteralPath $npcModule -Algorithm SHA256).Hash
        if ($npcHashBefore -ne $npcHashAfter) { throw 'Module changed during NPC death network test.' }
        foreach ($asset in $npcAssetHashes) { if ((Get-FileHash -LiteralPath $asset.path -Algorithm SHA256).Hash -ne $asset.SHA256) { throw 'NPC asset changed during network test.' } }
        $npcProof['moduleSHA256'] = $npcHashAfter
        $npcProof['authoredAssetHashes'] = $npcAssetHashes
        $npcProof['packetLagMinMs'] = $PacketLagMinMs; $npcProof['packetLagMaxMs'] = $PacketLagMaxMs; $npcProof['packetLossPercent'] = $PacketLossPercent
        $npcProof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $logRoot 'NPCDeathNetwork.proof.json') -Encoding UTF8
        $succeeded = $true
        Write-Host 'PASS: Saved NPC death HP/tag/montage, old actor removal and isolated replacement HP replicated to both Iris clients.'
    }
    if ($VerifySkillDamage) {
        $succeeded = $false
        $frameLogs = @((Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8)) +
            @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 })
        for ($frameIndex = 0; $frameIndex -lt $frameLogs.Count; ++$frameIndex) {
            $limit = if ($frameIndex -eq 0) { $MaxFPS } else { $clientFrameRate }
            $settings = [regex]::Matches($frameLogs[$frameIndex], 't\.MaxFPS = "(\d+)"')
            if ($settings.Count -eq 0 -or [int]$settings[$settings.Count-1].Groups[1].Value -ne $limit) {
                throw "Frame-rate setting was not applied as requested: log $frameIndex / $limit."
            }
        }
        if ($VerifySkillFinalCap) {
            . (Join-Path $PSScriptRoot 'SkillFinalCapProof.ps1')
            $finalCapProof = Get-SkillFinalCapProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
                -ClientText (Get-Content -LiteralPath $clientLogs[0] -Raw -Encoding UTF8) `
                -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent -RequireEndOrigin
        } elseif ($VerifySkillMultiPeerAbuse) {
            . (Join-Path $PSScriptRoot 'SkillMultiPeerAbuseProof.ps1')
            $multiClientTexts = @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 })
            $multiProof = Get-SkillMultiPeerAbuseProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
                -ClientTexts $multiClientTexts -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent -RequireEndOrigin -RequireEndRaceFixture:$VerifySkillEndRace
        } else {
            . (Join-Path $PSScriptRoot 'SkillDamageProof.ps1')
            $damageProof = Get-SkillDamageProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
                -ClientText (Get-Content -LiteralPath $clientLogs[0] -Raw -Encoding UTF8) `
                -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent -RequireEndOrigin
            if ($VerifySkillTargetDataAbuse) {
                . (Join-Path $PSScriptRoot 'SkillTargetDataAbuseProof.ps1')
                $skillAbuseProof = Get-SkillTargetDataAbuseProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
                    -ClientText (Get-Content -LiteralPath $clientLogs[0] -Raw -Encoding UTF8) `
                    -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent -RequireEndOrigin
            }
        }
        $damageHashAfter = (Get-FileHash -LiteralPath $damageModule -Algorithm SHA256).Hash
        if ($damageHashBefore -ne $damageHashAfter) { throw 'Project module changed during the damage test.' }
        $frameProof = [ordered]@{ serverMaxFPS=$MaxFPS; clientMaxFPS=$clientFrameRate
            actualLastConsoleSettingsVerified=$true; differentFrameRatesFixture=($MaxFPS -ne $clientFrameRate) }
        if ($VerifySkillFinalCap) {
            $finalCapProof['frameRates'] = $frameProof
            $finalCapProof['moduleSHA256'] = $damageHashAfter
            $finalCapProof | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $logRoot 'SkillFinalCap.proof.json') -Encoding UTF8
        } elseif ($VerifySkillMultiPeerAbuse) {
            $multiProof['frameRates'] = $frameProof
            $multiProof['moduleSHA256'] = $damageHashAfter
            $multiProof | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $logRoot 'SkillMultiPeerAbuse.proof.json') -Encoding UTF8
        } else {
            $damageProof['frameRates'] = $frameProof
            $damageProof['moduleSHA256'] = $damageHashAfter
            $damageProof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $logRoot 'SkillDamage.proof.json') -Encoding UTF8
            if ($VerifySkillTargetDataAbuse) {
                $skillAbuseProof['frameRates'] = $frameProof
                $skillAbuseProof['moduleSHA256'] = $damageHashAfter
                $skillAbuseProof | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $logRoot 'SkillTargetDataAbuse.proof.json') -Encoding UTF8
            }
        }
        $succeeded = $true
        Write-Host 'PASS: Authored charge damage reduced server HP, rejected targets retained HP, and client HP notification/replication matched.'
        if ($VerifySkillFinalCap) {
            Write-Host 'PASS: Authored Charge accepted exact-cap damage and rejected the lower-defense over-cap hit before HP modification; one applied hit/budget unit and actual client HP replication matched.'
        } elseif ($VerifySkillMultiPeerAbuse) {
            Write-Host 'PASS: Two overlapping authored Charge activations rejected seven malicious requests per source, including a foreign target; both target groups retained HP during malice and both clients observed both normal montage damage results.'
            if ($VerifySkillEndRace) {
                Write-Host 'PASS: Explicit delayed-client-montage fixture ended both active clients through the real matching server GAS RPC; this is not an unmodified timing-race run.'
            }
        } elseif ($VerifySkillTargetDataAbuse) {
            Write-Host 'PASS: Seven real malicious TargetData RPCs on the authored Charge player ASC left HP unchanged; normal release recovered server targeting and produced the authored montage damage.'
        }
    }
    if ($VerifyTargetDataAbuse) {
        $succeeded = $false # Proof/hash failures must clean up even with -KeepRunning.
        . (Join-Path $PSScriptRoot 'TargetDataWireProof.ps1')
        $proof = Get-TargetDataWireProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
            -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 })
        if ($VerifyTargetDataRate) {
            . (Join-Path $PSScriptRoot 'TargetDataRateProof.ps1')
            $rateProof = Get-TargetDataRateProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
                -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) `
                -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent
        }
        if ($VerifyTargetDataDamage) {
            . (Join-Path $PSScriptRoot 'TargetDataDamageProof.ps1')
            $wireDamageProof = Get-TargetDataDamageProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
                -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) `
                -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent
        }
        $wireHashAfter = (Get-FileHash -LiteralPath $wireModule -Algorithm SHA256).Hash
        if ($wireHashBefore -ne $wireHashAfter) { throw 'Project module changed during the wire test; evidence is not bound to one build.' }
        $proof['moduleSHA256'] = $wireHashAfter
        $proof['packetLagMinMs'] = $PacketLagMinMs
        $proof['packetLagMaxMs'] = $PacketLagMaxMs
        $proof['packetLossPercent'] = $PacketLossPercent
        $proof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $logRoot 'TargetData.proof.json') -Encoding UTF8
        if ($VerifyTargetDataRate) {
            $rateProof['moduleSHA256'] = $wireHashAfter
            $rateProof['packetLagMinMs'] = $PacketLagMinMs
            $rateProof['packetLagMaxMs'] = $PacketLagMaxMs
            $rateProof['packetLossPercent'] = $PacketLossPercent
            $rateProof | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $logRoot 'TargetDataRate.proof.json') -Encoding UTF8
        }
        if ($VerifyTargetDataDamage) {
            $wireDamageProof['moduleSHA256'] = $wireHashAfter
            $wireDamageProof | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $logRoot 'TargetDataDamage.proof.json') -Encoding UTF8
        }
        $succeeded = $true
        Write-Host 'PASS: Two real Iris clients, 12 TargetData receiver/callback/cache cases per peer and server-side foreign-owner RPC rejection. Isolated production-guard fixtures; not authored combat coverage.'
        if ($VerifyTargetDataRate) {
            Write-Host 'PASS: Actual 96-request bursts, shared confirm/cancel/rejected-request budget, independent receipt-time windows, live normal-peer isolation and recovery. Not unbounded flood/DoS resilience.'
        }
        if ($VerifyTargetDataDamage) {
            Write-Host 'PASS: Native wire callbacks applied real authorized GE damage; rejected/replayed requests did not change HP, submitted foreign targets were ignored, and both clients replicated both final HP values. Not authored BP combat coverage.'
        }
    } elseif ($VerifySkillTripods) {
        Write-Host 'PASS: Native tripod button/RPC/Instant-Charge GAS checks, eight rejections, owner-only selection/SP and full refund passed. Original Definition cooldown effects actually replicated to both owners; remaining/duration queries, blocked rapid input and fresh reactivation also passed. No rendered input or authored asset edits.'
    } elseif ($VerifyMovementSecurity) {
        . (Join-Path $PSScriptRoot 'MovementSecurityProof.ps1')
        $proof = Get-MovementSecurityProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
            -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) `
            -ClientCount $ClientCount -VerifyAuthorizedMovement ([bool]$VerifyAuthorizedMovement) `
            -VerifyAuthoredRoll ([bool]$VerifyAuthoredRoll) -PacketLagMinMs $PacketLagMinMs `
            -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent
        $proof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $logRoot 'Movement.proof.json') -Encoding UTF8
        Write-Host "PASS: Normal authored movement had zero reported movement violations; discontinuity positive controls were detected. Outgoing lag=$PacketLagMinMs..$PacketLagMaxMs ms, loss=$PacketLossPercent% in each direction."
        if ($VerifyAuthorizedMovement) {
            Write-Host 'PASS: Jump, BP-facing authorized dash/cancellation, knockback/expiry, same-name task overlap, reentrant cancellation, client authority rejection and budget-exceeded detection.'
        }
        if ($VerifyAuthoredRoll) {
            Write-Host 'PASS: Authored IA_Roll and Roll BP graph played AM_Roll/root motion, finished normally and revoked server approval on authority cancellation.'
            if ($ClientCount -gt 1) {
                Write-Host "PASS: Distinct player evidence, up to $($proof.maximumConcurrentRollPlayers) simultaneous Roll windows and $($proof.peerCancellationIsolationChecks) live-peer preservation checks."
            }
        }
    } elseif ($VerifySkillInput) {
        Write-Host "PASS: Iris and the authored Enhanced Input charge action reached a non-cancelled release section on server and $ClientCount clients (fixture-granted skill/Rage)."
        if ($VerifySkillProgression) {
            Write-Host 'PASS: UI view-model level requests reached the server, owner replication confirmed level 4, both activation snapshots used level 4, and reset refunded all SP. Tripod choices and rejected requests are not covered.'
        }
    } elseif (-not $VerifyDungeonEjection) {
        Write-Host "PASS: Iris server and $ClientCount clients observed the replicated ready state."
    }
    Write-Host "Logs: $logRoot"
    if ($KeepRunning) {
        Write-Host "Processes were left running because -KeepRunning was supplied."
    }
}
finally {
    if (-not $KeepRunning -or -not $succeeded) {
        foreach ($process in $spawnedProcesses) {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
            if ($VerifyDungeonEjection) { $process.WaitForExit(3000) | Out-Null }
        }
    }
    if ($VerifyDungeonEjection -and $succeeded -and -not $KeepRunning) {
        # Bind immutable final logs only after our own processes have stopped.
        $ejectionProof['logHashes'] = @((@($serverLog) + @($clientLogs)) | ForEach-Object {
            [ordered]@{path=$_; SHA256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}
        })
        $ejectionProof['logsFinalized'] = $true
        $ejectionProof | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $logRoot 'DungeonEjectionNetwork.proof.json') -Encoding UTF8
    }
}
