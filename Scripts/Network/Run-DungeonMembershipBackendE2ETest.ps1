[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024,65535)][int]$Port = 18838,
    [ValidateRange(60,600)][int]$TimeoutSeconds = 300,
    [ValidateRange(2,4)][int]$ClientCount = 2,
    [string]$BackendUrl = 'http://127.0.0.1:3038',
    [switch]$VerifyRemainingPartySettlement,
    [switch]$VerifyUnexpectedDisconnect,
    [switch]$VerifyReconnectWithinGrace,
    [switch]$ObservePartyCombatHud,
    [switch]$ObservePartyCombatVitals,
    [switch]$ObservePartyCombatInput,
    [switch]$ObservePartyCombatDeparture,
    [switch]$ObservePartyCombatExpiry,
    [ValidateRange(1024,65530)][int]$PartyHudMcpBasePort = 18080,
    [switch]$VerifyReconnectAdmissionLoss,
    [switch]$VerifyReconnectConfirmationResponseLoss,
    [switch]$VerifyAbandonmentResponseLoss,
    [switch]$VerifyAbandonmentClientRetry,
    [switch]$VerifyAbandonmentReauthentication,
    [switch]$VerifyAbandonedClientLobbyReturn,
    [switch]$VerifyAbandonedClientReceipt,
    [switch]$VerifyDungeonClientInitialAbandonment,
    [switch]$VerifyGameplayAbandonment,
    [switch]$VerifyGameplayAbandonmentResponseLoss,
    [switch]$VerifyGameplayAbandonmentReauthentication,
    [switch]$SkipPostgresStart,
    [switch]$NoBackendRestore
)

# Real PostgreSQL/HTTP, development authentication, ticket admission and periodic
# membership polling. Native movement injection, not Steam/authored combat/UI.
# Explicit opt-out uses the player's HTTP request; unexpected loss uses real
# socket cleanup and the production ten-minute poller. Neither seeds receipts.
$ErrorActionPreference = 'Stop'
if ($ObservePartyCombatInput -and (-not $ObservePartyCombatHud -or -not $VerifyRemainingPartySettlement -or
    $ClientCount -ne 2 -or $ObservePartyCombatVitals -or $ObservePartyCombatDeparture -or $ObservePartyCombatExpiry -or $VerifyUnexpectedDisconnect -or $VerifyReconnectWithinGrace -or
    $VerifyReconnectAdmissionLoss -or $VerifyAbandonmentResponseLoss -or $VerifyAbandonmentClientRetry -or
    $VerifyAbandonmentReauthentication -or $VerifyAbandonedClientLobbyReturn -or $VerifyAbandonedClientReceipt -or
    $VerifyDungeonClientInitialAbandonment -or $VerifyGameplayAbandonment)) {
    throw 'Party HUD input-only observation requires exactly two rendered clients and the party fixture without any lifecycle scenario.'
}
if ($ObservePartyCombatDeparture -and (-not $ObservePartyCombatHud -or -not $VerifyRemainingPartySettlement -or
    $ClientCount -ne 2 -or $ObservePartyCombatInput -or $ObservePartyCombatVitals -or $ObservePartyCombatExpiry -or $VerifyUnexpectedDisconnect -or
    $VerifyReconnectWithinGrace -or $VerifyReconnectAdmissionLoss -or $VerifyAbandonmentResponseLoss -or
    $VerifyAbandonmentClientRetry -or $VerifyAbandonmentReauthentication -or $VerifyAbandonedClientLobbyReturn -or
    $VerifyAbandonedClientReceipt -or $VerifyDungeonClientInitialAbandonment -or $VerifyGameplayAbandonment)) {
    throw 'Party HUD departure-only observation requires exactly two rendered clients and the plain explicit-abandonment party fixture.'
}
if ($ObservePartyCombatExpiry -and (-not $ObservePartyCombatHud -or -not $VerifyRemainingPartySettlement -or
    -not $VerifyUnexpectedDisconnect -or $ClientCount -ne 2 -or $VerifyReconnectWithinGrace -or
    $ObservePartyCombatInput -or $ObservePartyCombatVitals -or $ObservePartyCombatDeparture -or
    $VerifyAbandonmentResponseLoss -or $VerifyAbandonmentClientRetry -or $VerifyAbandonmentReauthentication -or
    $VerifyAbandonedClientLobbyReturn -or $VerifyAbandonedClientReceipt -or $VerifyDungeonClientInitialAbandonment -or
    $VerifyGameplayAbandonment)) {
    throw 'Party HUD expiry-only observation requires exactly two rendered clients and the natural unexpected-disconnect party fixture.'
}
if ($ObservePartyCombatVitals -and (-not $ObservePartyCombatHud -or $VerifyReconnectAdmissionLoss)) {
    throw 'Party vitals observation requires the rendered two-player HUD reconnect scenario without pending-admission faults.'
}
if ($ObservePartyCombatHud -and ($ClientCount -ne 2 -or
    (-not $VerifyReconnectWithinGrace -and -not $ObservePartyCombatInput -and -not $ObservePartyCombatDeparture -and -not $ObservePartyCombatExpiry))) {
    throw 'Party HUD observation requires exactly two clients and a reconnect, input-only, departure-only or expiry-only scenario.'
}
if ($VerifyGameplayAbandonmentResponseLoss -and -not $VerifyGameplayAbandonment) { throw 'Combat response loss requires combat consent mode.' }
if ($VerifyGameplayAbandonmentReauthentication -and -not $VerifyGameplayAbandonmentResponseLoss) { throw 'Combat reauthentication requires combat response loss mode.' }
if ($VerifyGameplayAbandonment -and ($ClientCount -ne 2 -or -not $VerifyRemainingPartySettlement -or
    $VerifyUnexpectedDisconnect -or $VerifyAbandonmentResponseLoss -or $VerifyAbandonmentClientRetry -or
    $VerifyAbandonmentReauthentication -or $VerifyAbandonedClientLobbyReturn -or $VerifyAbandonedClientReceipt -or
    $VerifyDungeonClientInitialAbandonment)) { throw 'Combat consent requires exactly two clients and only remaining-party settlement mode.' }
if ($VerifyReconnectConfirmationResponseLoss -and -not $VerifyReconnectAdmissionLoss) { throw 'Committed confirmation response loss requires pending reconnect loss mode.' }
if ($VerifyReconnectAdmissionLoss -and -not $VerifyReconnectWithinGrace) { throw 'Pending admission loss requires reconnect-within-grace mode.' }
if ($VerifyReconnectWithinGrace -and -not $VerifyUnexpectedDisconnect) { throw 'Reconnect mode requires unexpected-disconnect and remaining-party settlement modes.' }
if ($VerifyUnexpectedDisconnect -and (-not $VerifyRemainingPartySettlement -or $VerifyAbandonmentResponseLoss -or
    $VerifyAbandonmentClientRetry -or $VerifyAbandonmentReauthentication -or $VerifyAbandonedClientLobbyReturn -or
    $VerifyAbandonedClientReceipt -or $VerifyDungeonClientInitialAbandonment)) {
    throw 'Unexpected-disconnect mode requires remaining-party settlement without any explicit-abandonment scenario.'
}
if ($VerifyAbandonmentResponseLoss -and -not $VerifyRemainingPartySettlement) {
    throw 'Response-loss verification requires remaining-party settlement mode.'
}
if ($VerifyAbandonmentClientRetry -and (-not $VerifyAbandonmentResponseLoss -or $ClientCount -ne 2)) {
    throw 'Live lobby retry requires response-loss mode with exactly two admitted clients.'
}
if ($VerifyAbandonmentReauthentication -and -not $VerifyAbandonmentClientRetry) {
    throw 'Live abandonment reauthentication requires the live lobby retry mode.'
}
if ($VerifyAbandonedClientLobbyReturn -and ($ClientCount -ne 2 -or -not $VerifyRemainingPartySettlement -or $VerifyAbandonmentResponseLoss)) {
    throw 'Same-process abandoned client return requires the two-player settlement mode without response-loss injection.'
}
if ($VerifyAbandonedClientReceipt -and -not $VerifyAbandonedClientLobbyReturn) {
    throw 'Same-process receipt confirmation requires abandoned client lobby return verification.'
}
if ($VerifyDungeonClientInitialAbandonment -and -not $VerifyAbandonedClientReceipt) {
    throw 'Dungeon-client initial POST verification requires the same-process lobby receipt mode.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$compose = Join-Path $projectRoot 'Backend\compose.postgres.yml'
$baseUrl = $BackendUrl.TrimEnd('/')
$uri = [Uri]$baseUrl
if ($uri.Scheme -ne 'http' -or $uri.Host -ne '127.0.0.1' -or $uri.AbsolutePath -ne '/' -or $uri.Port -eq $Port) { throw 'Use distinct loopback HTTP/UE ports.' }
foreach ($file in @($project,$editor,$module,$compose)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing prerequisite: $file" } }
$listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
if (@($listeners.GetActiveTcpListeners() | Where-Object Port -eq $uri.Port).Count -or
    @($listeners.GetActiveUdpListeners() | Where-Object Port -eq $Port).Count) { throw 'A requested port is already in use.' }
if ($ObservePartyCombatHud) {
    $hudPorts=@(($PartyHudMcpBasePort+1),($PartyHudMcpBasePort+2),($PartyHudMcpBasePort+3))
    if ($uri.Port -in $hudPorts -or $Port -in $hudPorts -or
        @($listeners.GetActiveTcpListeners() | Where-Object Port -in $hudPorts).Count) {
        throw 'Use three unused, distinct loopback MCP ports for the owned HUD clients.'
    }
    . (Join-Path $PSScriptRoot 'DungeonPartyHudObservation.ps1')
    $script:PartyHudNextMcpPort=$PartyHudMcpBasePort
    $script:PartyHudClients=@{}
    $script:PartyHudObservers=@{}
    $script:PartyHudEvidence=[Collections.Generic.List[object]]::new()
}
try { $dockerVersion = & docker info --format '{{.ServerVersion}}' 2>&1 } catch { throw 'Start Docker Desktop before PostgreSQL E2E.' }
if ($LASTEXITCODE -ne 0) { throw 'Start Docker Desktop before PostgreSQL E2E.' }
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
# Start-Process on Windows rejects duplicate environment keys differing by case.
$environment = [Environment]::GetEnvironmentVariables('Process')
$pathKeys = @($environment.Keys | Where-Object { $_.ToString() -ieq 'Path' })
if ($pathKeys.Count -gt 1) {
    $pathValue = [string]$environment[$pathKeys[0]]
    foreach ($key in $pathKeys) { [Environment]::SetEnvironmentVariable($key.ToString(),$null,'Process') }
    [Environment]::SetEnvironmentVariable('Path',$pathValue,'Process')
}
$runId = [Guid]::NewGuid().ToString('N')
$schema = "rpg_membership_e2e_$runId"
if ($schema -cnotmatch '^rpg_membership_e2e_[0-9a-f]{32}$') { throw 'Invalid owned PostgreSQL schema.' }
$runRoot = Join-Path $projectRoot "Saved\Tests\DungeonMembershipBackend\$runId"
$runtimeUserRoot = Join-Path $projectRoot "Saved\Tests\DU\$($runId.Substring(0,12))"
# CreateDirectoryW can reject a 248..259-character directory before UE adds its
# long-path prefix. Keep both journal directories AND record .tmp paths short.
$maximumJournalPath=Join-Path $runtimeUserRoot "s\Saved\Online\DungeonDisconnections\$('a'*32)\$('b'*32)\$([Guid]::Empty)\ConsumeRecoveries"
if ($maximumJournalPath.Length+46 -ge 260) { throw 'Workspace path is too long for this isolated journal fixture.' }
if (Test-Path -LiteralPath $runtimeUserRoot) { throw 'Owned runtime directory already exists; refusing to reuse another run.' }
New-Item -ItemType Directory -Path $runtimeUserRoot | Out-Null
$buildRoot = Join-Path $runRoot 'backend-build'
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$proofParser = Join-Path $PSScriptRoot $(if ($VerifyRemainingPartySettlement) {'DungeonAbandonmentSettlementProof.ps1'} else {'DungeonEjectionNetworkProof.ps1'})
if ($VerifyReconnectWithinGrace) { $proofParser=Join-Path $PSScriptRoot 'DungeonReconnectSettlementProof.ps1' }
$abandonProxyScript = Join-Path $PSScriptRoot 'Fixtures\DungeonAbandonmentHttpFaultProxy.mjs'
$victoryGate = Join-Path $runRoot 'remaining-party-ready.gate'
$lobbyStartGate = Join-Path $runRoot 'abandonment-ui-start.gate'
$lobbyRetryGate = Join-Path $runRoot 'abandonment-ui-retry.gate'
$lobbyRenewedRetryGate = Join-Path $runRoot 'abandonment-ui-renewed-retry.gate'
$initialAbandonmentGate = Join-Path $runRoot 'dungeon-client-initial-abandonment.gate'
$gameplayConsentGate=Join-Path $runRoot 'gameplay-consent.gate'
$gameplayConfirmGate=Join-Path $runRoot 'gameplay-confirm.gate'
. $proofParser
$disconnectScenario=Join-Path $PSScriptRoot 'DungeonUnexpectedDisconnectScenario.ps1'
if ($VerifyUnexpectedDisconnect) { . $disconnectScenario }
$reconnectScenario=Join-Path $PSScriptRoot 'DungeonReconnectScenario.ps1'
if ($VerifyReconnectWithinGrace) { . $reconnectScenario }
$pendingScenario=Join-Path $PSScriptRoot 'DungeonPendingReconnectScenario.ps1'
$confirmationProxy=Join-Path $PSScriptRoot 'Fixtures\DungeonConfirmationHoldProxy.mjs'
if ($VerifyReconnectAdmissionLoss) { . $pendingScenario }
$gameplayScenario=Join-Path $PSScriptRoot 'DungeonGameplayAbandonmentScenario.ps1'
if ($VerifyGameplayAbandonment) { . $gameplayScenario }
$sourceFiles = @('Private\Tests\RPGDungeonEjectionNetworkProbe.cpp','Private\Tests\RPGDungeonEjectionNetworkProbe.h',
    'Private\Tests\RPGDungeonEjectionTestFixture.cpp','Private\Tests\RPGDungeonEjectionTestFixture.h',
    'Private\Tests\RPGDungeonFlowE2EProbe.cpp','Private\GameMode\RPGGameModeBase.cpp','Public\GameMode\RPGGameModeBase.h',
    'Private\Manager\HttpWebManager.cpp','Public\Manager\HttpWebManager.h',
    'Private\Online\RPGDungeonMembershipSnapshot.cpp','Public\Online\RPGDungeonMembershipSnapshot.h',
    'Private\Online\RPGDungeonSessionSubsystem.cpp','Private\Player\RPGPlayerState.cpp',
    'Private\Character\RPGPlayer.cpp','Private\Security\RPGDungeonGameplayAuthority.cpp')
if ($VerifyRemainingPartySettlement) {
    $sourceFiles += @('Private\Tests\RPGDungeonFlowE2EProbe.h','Private\GameMode\RPGBossBattleGameMode.cpp',
        'Private\GameState\RPGBossBattleGameState.cpp','Private\AI\RPGBossThreatComponent.cpp',
        'Private\Economy\RPGDungeonRewardSnapshot.cpp','Private\Component\RPGInventoryProjectionComponent.cpp')
}
$files = @($sourceFiles | ForEach-Object { Join-Path $projectRoot "Source\Project_RPG\$_" }) + @(
    $PSCommandPath,$proofParser,(Join-Path $projectRoot 'Source\Project_RPG\Project_RPG.Build.cs'),
    (Join-Path $projectRoot 'Backend\Database\schema.sql'),
    (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Api\DungeonAbandonmentApiEndpoints.cs'),
    (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Api\DungeonMembershipSnapshotApiEndpoints.cs'),
    (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Domain\StarterEquipment.cs'),
    (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Data\PostgresGameRepository.cs'))
if ($VerifyRemainingPartySettlement) {
    $files += @((Join-Path $projectRoot 'Content\Maps\bossmap.umap'),
        (Join-Path $projectRoot 'Config\DefaultGame.ini'),
        (Join-Path $projectRoot 'Source\Project_RPG\Public\Online\RPGDungeonServerDefinition.h'),
        (Join-Path $projectRoot 'Source\Project_RPG\Private\Online\RPGDungeonServerDefinition.cpp'),
        (Join-Path $projectRoot 'Content\System\Dungeons\DA_DungeonServer_RaidSwordMan.uasset'),
        (Join-Path $projectRoot 'Content\Blueprints\GameMode\BP_BossBattleGameMode.uasset'),
        (Join-Path $projectRoot 'Content\Blueprints\Character\Shared\GameplayAbility\GA_NPC_DrawBossHpBar_Base.uasset'),
        (Join-Path $projectRoot 'Content\Blueprints\Character\NPC\SwordMan\GameplayAbility\GA_NPC_DrawBossHpBar_SwordMan.uasset'),
        (Join-Path $projectRoot 'Scripts\Editor\GuardBossBattleStartupUI.py'),
        (Join-Path $projectRoot 'Scripts\Editor\GuardBossHealthBarUI.py'),
        (Join-Path $projectRoot 'Backend\seed-game-economy.ps1'),
        (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Data\PostgresDungeonRewardCommitRepository.cs'),
        (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Application\DungeonRewardSettlementWorker.cs'))
}
if ($VerifyAbandonmentResponseLoss -or $VerifyGameplayAbandonmentResponseLoss) {
    $files += @($abandonProxyScript,(Join-Path $PSScriptRoot 'Fixtures\DungeonAbandonmentHttpFaultProxy.test.mjs'))
}
if ($VerifyUnexpectedDisconnect) {
    $files += @($disconnectScenario,(Join-Path $projectRoot 'Source\Project_RPG\Private\Controller\RPGPlayerController.cpp'),
        (Join-Path $projectRoot 'Source\Project_RPG\Public\Controller\RPGPlayerController.h'),
        (Join-Path $projectRoot 'Source\Project_RPG\Private\Online\RPGDungeonDisconnectionJournal.cpp'),
        (Join-Path $projectRoot 'Source\Project_RPG\Public\Online\RPGDungeonDisconnectionJournal.h'),
        (Join-Path $projectRoot 'Backend\ProjectRpg.Backend\Api\DungeonAdmissionApiEndpoints.cs'))
}
if ($VerifyReconnectWithinGrace) { $files+=@($reconnectScenario,(Join-Path $projectRoot 'Scripts\Tests\Test-DungeonReconnectSettlementProof.ps1')) }
if ($VerifyReconnectAdmissionLoss) { $files+=@($pendingScenario,$confirmationProxy,(Join-Path $PSScriptRoot 'Fixtures\DungeonConfirmationHoldProxy.test.mjs')) }
if ($VerifyGameplayAbandonment) {
    $gameplaySources=@('Private\Tests\RPGDungeonGameplayAbandonmentE2E.cpp',
        'Private\Controller\RPGPlayerController.cpp','Public\Controller\RPGPlayerController.h',
        'Public\Online\RPGDungeonSessionSubsystem.h','Private\UI\Content\RPGDungeonResultActionsWidget.cpp',
        'Public\UI\Content\RPGDungeonResultActionsWidget.h','Private\UI\RPGWindowInputState.cpp','Public\UI\RPGWindowInputState.h',
        'Private\UI\Inventory\Projection\RPGInventoryProjectionWidget.cpp')
    $sourceFiles+=$gameplaySources
    $files+=@($gameplaySources | ForEach-Object { Join-Path $projectRoot "Source\Project_RPG\$_" })
    $files+=@($gameplayScenario,(Join-Path $projectRoot 'Scripts\Tests\Test-DungeonAbandonmentSettlementProof.ps1'))
}
if ($VerifyAbandonmentClientRetry -or $VerifyAbandonedClientReceipt -or $VerifyGameplayAbandonment) {
    $sourceFiles += @('Private\UI\Lobby\RPGOnlineLobbyWidget.cpp','Public\UI\Lobby\RPGOnlineLobbyWidget.h')
    $files += @($sourceFiles[-2..-1] | ForEach-Object { Join-Path $projectRoot "Source\Project_RPG\$_" })
}
$moduleHash = (Get-FileHash -LiteralPath $module).Hash
if ($ObservePartyCombatHud) {
    $files+=@((Join-Path $PSScriptRoot 'DungeonPartyHudObservation.ps1'),
        (Join-Path $projectRoot 'Content/Blueprints/Controller/BP_PlayerController.uasset'),
        (Join-Path $projectRoot 'Content/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter.uasset'))
    $hudSources=@('Public/Online/RPGDungeonPartyCombatTypes.h','Private/Online/RPGDungeonPartyCombatTypes.cpp',
        'Public/Component/RPGDungeonPartyCombatComponent.h','Private/Component/RPGDungeonPartyCombatComponent.cpp',
        'Public/UI/Content/RPGDungeonPartyCombatWidget.h','Private/UI/Content/RPGDungeonPartyCombatWidget.cpp',
        'Private/Controller/RPGPlayerControllerPartyCombat.cpp','Private/GameMode/RPGGameModePartyPresentation.cpp')
    $sourceFiles+=$hudSources
    $files+=@($hudSources | ForEach-Object {Join-Path $projectRoot "Source/Project_RPG/$_"})
}
if ($ObservePartyCombatVitals) {
    $vitalsSources=@('Private/Tests/RPGDungeonPartyVitalsProbe.cpp',
        'Private/FunctionLibrary/RPGSecurityBlueprintLibrary.cpp','Private/Security/RPGDamageExecutionGuard.cpp',
        'Private/GameplayEffect/GE_ExecCalc_TakeDamage.cpp','Private/Attribute/RPGAttributeSet.cpp')
    $sourceFiles+=$vitalsSources
    $files+=@($vitalsSources | ForEach-Object {Join-Path $projectRoot "Source/Project_RPG/$_"})
}
if ($ObservePartyCombatInput) {
    $inputSources=@('Private/Tests/RPGDungeonPartyHudInputProbe.cpp','Private/Controller/RPGPlayerController.cpp',
        'Public/Controller/RPGPlayerController.h','Private/Skill/QuickSlot/RPGQuickSlotInputBindings.cpp',
        'Private/Controller/RPGInputMappingClone.h')
    $sourceFiles+=$inputSources
    $files+=@($inputSources | ForEach-Object {Join-Path $projectRoot "Source/Project_RPG/$_"})
    $files+=@((Join-Path $projectRoot 'Content/Blueprints/Character/Player/Input/DA_InputConfig.uasset'),
        (Join-Path $projectRoot 'Content/Blueprints/Character/Player/Input/IMC_Default.uasset'))
}
$hashes = @($files | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} })
foreach ($relative in $sourceFiles) {
    if ((Get-Item -LiteralPath (Join-Path $projectRoot "Source\Project_RPG\$relative")).LastWriteTimeUtc -gt (Get-Item -LiteralPath $module).LastWriteTimeUtc) { throw 'Build the current Project_RPG Editor module first.' }
}
[ordered]@{schemaVersion=1;passed=$false;scope='Pre-runtime attempt only';runId=$runId;
    startedAtUtc=[DateTime]::UtcNow.ToString('o');postgresSchema=$schema;clients=$ClientCount;
    runtimeUserRoot=$runtimeUserRoot;
    moduleSHA256=$moduleHash;sourceAndRunnerHashes=$hashes;remainingPartyFixture=[bool]$VerifyRemainingPartySettlement;
    remainingPartySettlement=[bool]($VerifyRemainingPartySettlement -and -not $ObservePartyCombatInput -and -not $ObservePartyCombatDeparture -and -not $ObservePartyCombatExpiry);
    partyCombatInput=[bool]$ObservePartyCombatInput;partyCombatDeparture=[bool]$ObservePartyCombatDeparture;
    partyCombatExpiry=[bool]$ObservePartyCombatExpiry;
    unexpectedDisconnect=[bool]$VerifyUnexpectedDisconnect;
    reconnectWithinGrace=[bool]$VerifyReconnectWithinGrace;
    partyCombatVitals=[bool]$ObservePartyCombatVitals;
    reconnectAdmissionLoss=[bool]$VerifyReconnectAdmissionLoss;
    reconnectConfirmationResponseLoss=[bool]$VerifyReconnectConfirmationResponseLoss;
    gameplayAbandonment=[bool]$VerifyGameplayAbandonment;
    gameplayAbandonmentResponseLoss=[bool]$VerifyGameplayAbandonmentResponseLoss;
    gameplayAbandonmentReauthentication=[bool]$VerifyGameplayAbandonmentReauthentication;
    abandonmentResponseLoss=[bool]$VerifyAbandonmentResponseLoss;liveLobbyRetry=[bool]$VerifyAbandonmentClientRetry;
    abandonmentReauthentication=[bool]$VerifyAbandonmentReauthentication;
    abandonedClientLobbyReturn=[bool]$VerifyAbandonedClientLobbyReturn;
    abandonedClientReceipt=[bool]$VerifyAbandonedClientReceipt;
    dungeonClientInitialAbandonment=[bool]$VerifyDungeonClientInitialAbandonment} | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath (Join-Path $runRoot 'Attempt.proof.json') -Encoding UTF8
$serverEnv = @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')
$envNames = @('ASPNETCORE_ENVIRONMENT','PROJECT_RPG_BACKEND_ADMIN_TOKEN','POSTGRES_CONNECTION_STRING','Kestrel__Endpoints__Http__Url','UE_SKIP_UBT_SDK_SETUP',
    'PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN','PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT','PROJECT_RPG_ABANDON_PROXY_TRANSPARENT') + $serverEnv
$previousEnv = @{}
if ($VerifyReconnectAdmissionLoss) { $envNames+=@('PROJECT_RPG_CONFIRM_HOLD_MODE','PROJECT_RPG_CONFIRM_HOLD_UPSTREAM','PROJECT_RPG_CONFIRM_HOLD_ROOT',
    'PROJECT_RPG_CONFIRM_HOLD_CHARACTER','PROJECT_RPG_CONFIRM_HOLD_STEAM','PROJECT_RPG_CONFIRM_HOLD_AUTH_HASH') }
foreach ($name in $envNames) { $previousEnv[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$logs = [Collections.Generic.List[string]]::new()
$adminToken = "membership-$runId"
$adminHeaders = @{Authorization="Bearer $adminToken"}
$maintainStartupLease = $false
$nextStartupHeartbeatUtc = [DateTime]::MinValue
$startupLeaseHeartbeatCount = 0
$failure = 'RPG_EJECTION_NET FAILED|DUNGEON_FLOW_E2E_FAIL|ABANDON_PROXY_FAIL|DUNGEON_CONFIRM_HOLD_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|LogScript: Error:|LogScript: Warning:[^\r\n]*(?:Accessed None|Attempted to access|Infinite loop|Array index out of bounds)|RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED'
$completed = $false
$failureMessage = $null
$expectedExitedProcess = $null
$disconnectEvidence = $null
$departureEvidence = $null
$reconnectedClientLog = $null
$pendingReconnectClientLog = $null
$expectedExitedProcesses=[Collections.Generic.List[Diagnostics.Process]]::new()

function Invoke-Api {
    param([string]$Path,[string]$Method='Get',[hashtable]$Headers=@{},[object]$Body=$null,[int]$ExpectedStatus=200)
    $request = @{Uri="$baseUrl/api/$Path";Method=$Method;Headers=$Headers;TimeoutSec=10;UseBasicParsing=$true}
    if ($null -ne $Body) { $request.ContentType='application/json'; $request.Body=$Body | ConvertTo-Json -Depth 10 -Compress }
    try { $response=Invoke-WebRequest @request }
    catch {
        if ($ExpectedStatus -in @(401,403,409) -and $null -ne $_.Exception.Response -and
            [int]$_.Exception.Response.StatusCode -eq $ExpectedStatus) { return }
        throw "HTTP $Method $Path failed; credentials/body omitted."
    }
    if ([int]$response.StatusCode -ne $ExpectedStatus) { throw "Unexpected HTTP status for $Method $Path" }
    if ($response.Content) { return $response.Content | ConvertFrom-Json }
}
function Assert-Healthy {
    foreach ($proc in $processes) {
        $proc.Refresh()
        if ($proc.HasExited -and -not [object]::ReferenceEquals($proc,$expectedExitedProcess) -and -not $expectedExitedProcesses.Contains($proc)) { throw "Owned process $($proc.Id) exited; inspect $runRoot" }
    }
    foreach ($log in $logs) { if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Pattern $failure -Quiet)) { throw "Runtime failure; inspect $log" } }
    if ($VerifyRemainingPartySettlement -and $backendOut -and (Test-Path -LiteralPath $backendOut) -and
        (Select-String -LiteralPath $backendOut -Pattern 'Dungeon reward settlement .* failed permanently:' -Quiet)) {
        throw "Permanent backend reward settlement failure; inspect $backendOut"
    }
    if ($maintainStartupLease -and [DateTime]::UtcNow -ge $nextStartupHeartbeatUtc) {
        $beat=Invoke-Api "dungeon-sessions/$sessionId/heartbeat" 'Post' $serverHeaders @{serverId=$serverId}
        if ($beat.dungeonSessionId -ne $sessionId -or $beat.serverId -ne $serverId -or $beat.state -notin @('Loading','InProgress')) { throw 'Assigned startup heartbeat scope mismatch.' }
        $script:startupLeaseHeartbeatCount++
        $script:nextStartupHeartbeatUtc=[DateTime]::UtcNow.AddSeconds(15)
    }
}
function Wait-Marker {
    param([string]$Log,[string]$Marker)
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        if ((Test-Path -LiteralPath $Log) -and (Select-String -LiteralPath $Log -SimpleMatch $Marker -Quiet)) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Missing $Marker; inspect $Log"
}
function Start-Unreal {
    param([string]$Name,[string]$Destination,[string[]]$Extra,[string]$WebApiBase=$baseUrl,[switch]$LobbyAbandonment)
    $env:UE_SKIP_UBT_SDK_SETUP='1'
    $log=Join-Path $runRoot "$Name.log"
    $runtimeUser=Join-Path $runtimeUserRoot $(if ($Name -eq 'server') {'s'} else {$Name})
    $hudClient=$ObservePartyCombatHud -and $Name.StartsWith('client-')
    $executable=$editor
    if ($hudClient) {
        $Destination='/Engine/Maps/Entry'
        $Extra=@($Extra | Where-Object {$_ -ne '-game'})+@('-editor')
        $executable=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
    }
    $runtimeArgs=@(('"'+$project+'"'),$Destination)+$Extra+@(
        '-UseIrisReplication=1','-RPGRequireIris','-unattended','-nop4','-nosplash','-nosound','-nosteam',
        '-NoLiveCoding','-NoLoadingScreen','-DisablePython','-DDC=(EnginePak,Local)',
        ('-UserDir="'+$runtimeUser+'"'),('-abslog="'+$log+'"'),'-ExecCmds="t.MaxFPS 30"',
        '-PktLagMin=80','-PktLagMax=160','-PktLoss=5',"-RPGMembershipE2ETimeout=$TimeoutSeconds",
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$WebApiBase/api",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$baseUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$baseUrl/api")
    if ($hudClient) {
        $script:PartyHudNextMcpPort++
        $hudPort=$script:PartyHudNextMcpPort
        $runtimeArgs+=@('-culture=ko','-windowed','-ResX=1280','-ResY=720',
            '-ModelContextProtocolStartServer',"-ModelContextProtocolPort=$hudPort",
            '-ini:Engine:HTTPServer.Listeners:DefaultBindAddress=127.0.0.1',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:PlayNumberOfClients=1',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:PlayNetMode=PIE_Standalone',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:bLaunchSeparateServer=False',
            '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=True',
            "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:ServerPortNumber=$hudPort")
    } else {
        $runtimeArgs+=@('-nullrhi',
            '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    }
    if (-not $LobbyAbandonment) { $runtimeArgs += @('-RPGDungeonMembershipBackendE2E',"-RPGExpectedClients=$ClientCount") }
    if ($VerifyRemainingPartySettlement -and -not $LobbyAbandonment) {
        $runtimeArgs += @('-RPGDungeonAbandonmentSettlementE2E',"-RPGAbandonCharacter=$($members[0].CharacterId)")
    }
    if ($VerifyUnexpectedDisconnect -and -not $LobbyAbandonment) { $runtimeArgs+='-RPGDungeonUnexpectedDisconnectE2E' }
    if ($VerifyReconnectWithinGrace -and -not $LobbyAbandonment) { $runtimeArgs+='-RPGDungeonReconnectE2E' }
    if ($ObservePartyCombatVitals -and $Name -eq 'server') { $runtimeArgs+='-RPGPartyCombatVitalsE2E' }
    if ($ObservePartyCombatInput -and $Name -eq 'client-1') {
        $runtimeArgs+=@('-RPGPartyHudInputE2E',('-RPGPartyHudInputGate="'+$victoryGate+'.input"'))
    }
    if ($VerifyReconnectAdmissionLoss -and -not $LobbyAbandonment) { $runtimeArgs+='-RPGReconnectAdmissionLossE2E' }
    if ($VerifyGameplayAbandonment -and -not $LobbyAbandonment) { $runtimeArgs+='-RPGGameplayAbandonmentSettlementE2E' }
    $proc=Start-Process -FilePath $executable -ArgumentList $runtimeArgs -WorkingDirectory $projectRoot -PassThru -WindowStyle Hidden
    $processes.Add($proc); $logs.Add($log)
    if ($Name -eq 'server' -and ($ObservePartyCombatInput -or $ObservePartyCombatDeparture -or $ObservePartyCombatExpiry)) {
        $script:PartyHudObservationServer=$proc
    }
    Write-Host "Started $Name (PID $($proc.Id)): $log"
    if ($hudClient) {
        $script:PartyHudClients[$Name]=@{Port=$hudPort;Process=$proc;Log=$log}
        Start-PartyHudOwnedClientPIE $hudPort $proc
    }
    return $log
}
function Start-DungeonReconnectClient {
    param([string]$Name='client-reconnect')
    $member=$members[0]
    return (Start-Unreal $Name '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' @(
        '-game','-RPGDungeonFlowE2E','-RPGFlowE2EReconnectReplacement',
        "-RPGFlowE2ESteam=$($member.SteamId)","-RPGFlowE2ECharacter=$($member.CharacterId)","-RPGFlowE2ESession=$sessionId"))
}
function Assert-Unchanged {
    if ((Get-FileHash -LiteralPath $module).Hash -cne $moduleHash) { throw 'Shared UE module changed during runtime.' }
    foreach ($file in $hashes) { if ((Get-FileHash -LiteralPath $file.path).Hash -cne $file.SHA256) { throw "Runtime source changed: $($file.path)" } }
}

try {
    if (-not $SkipPostgresStart) { & docker compose -f $compose up -d --wait; if ($LASTEXITCODE -ne 0) { throw 'Development PostgreSQL startup failed.' } }
    # A fresh, owned search-path schema prevents this fixture from touching
    # public data or previous tests. Intentionally retained for audit, not dropped.
    & docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -c "CREATE SCHEMA $schema;"
    if ($LASTEXITCODE -ne 0) { throw 'Owned PostgreSQL schema creation failed.' }
    $buildArgs=@('build',(Join-Path $projectRoot 'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj'),'--output',$buildRoot)
    if ($NoBackendRestore) { $buildArgs+='--no-restore' }
    & $dotnet @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 'Backend build failed.' }
    $backendDll = Join-Path $buildRoot 'ProjectRpg.Backend.dll'
    $backendModuleHash=(Get-FileHash -LiteralPath $backendDll).Hash
    $env:ASPNETCORE_ENVIRONMENT='Development'; $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN=$adminToken
    $env:POSTGRES_CONNECTION_STRING="Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only;Search Path=$schema"
    [Environment]::SetEnvironmentVariable('Kestrel__Endpoints__Http__Url',$baseUrl,'Process')
    foreach ($name in $serverEnv) { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    $backendOut=Join-Path $runRoot 'backend.stdout.log'; $backendErr=Join-Path $runRoot 'backend.stderr.log'
    $backendArguments = @(('"'+$backendDll+'"'),
        '--DungeonContent:Entries:1:DungeonId=Dungeon.MembershipE2E',
        '--DungeonContent:Entries:1:MapPackage=/Game/Maps/testmap',
        '--DungeonContent:Entries:1:Difficulties:0=Normal')
    $backend=Start-Process -FilePath $dotnet -ArgumentList $backendArguments -WorkingDirectory $buildRoot -RedirectStandardOutput $backendOut -RedirectStandardError $backendErr -PassThru -WindowStyle Hidden
    $processes.Add($backend)
    $deadline=[DateTime]::UtcNow.AddSeconds(45); $ready=$false
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        try { $health=Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 2; if ($health.status -eq 'ready' -and $health.storageProvider -eq 'Postgres') { $ready=$true; break } } catch { }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw "PostgreSQL readiness failed; inspect $backendOut and $backendErr" }
    if ($VerifyRemainingPartySettlement) {
        $seeded = @(& (Join-Path $projectRoot 'Backend\seed-game-economy.ps1') -BaseUrl $baseUrl -AdminToken $adminToken)
        if ($seeded.Count -ne 1 -or $seeded[0].CurrencyCode -ne 'RosterGold' -or
            $seeded[0].Scope -ne 'Roster' -or $seeded[0].MaxBalance -ne 1000000000 -or
            -not $seeded[0].Enabled) { throw 'Fresh-schema RosterGold definition seed mismatch.' }
    }
    $members=@(for ($index=1;$index -le $ClientCount;$index++) {
        $steamId=([uint64]76561420000000000+[uint64][Convert]::ToUInt32($runId.Substring(0,8),16)*4+[uint64]$index).ToString()
        $auth=Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steamId"}
        $headers=@{Authorization="Bearer $($auth.accessToken)"}
        $character=Invoke-Api 'characters' 'Post' $headers @{name="Member$($runId.Substring(0,10))$index"} 201
        $starter=$null
        if ($VerifyRemainingPartySettlement) {
            $initialItems=Invoke-Api "items?ownerType=Character&ownerId=$($character.characterId)&includeTerminal=false&limit=500" 'Get' $headers
            $initialWallet=Invoke-Api "economy/wallets/$($character.characterId)" 'Get' $headers
            $initialEquipment=@($initialItems.items)
            if ($initialEquipment.Count -ne 1 -or $initialEquipment[0].definitionName -cne 'GameItem.Equipment.Weapon.GreatSword' -or
                $initialEquipment[0].location.containerType -cne 'Equipment' -or $initialEquipment[0].location.slotIndex -ne 9 -or
                $initialEquipment[0].state.quantity -ne 1 -or $initialEquipment[0].revision -ne 1 -or
                @($initialWallet.balances | Where-Object { $_.currencyCode -eq 'RosterGold' -and $_.balance -ne 0 }).Count) {
                throw 'Fresh character starter equipment/wallet baseline mismatch.'
            }
            $starter=$initialEquipment[0]
        }
        [pscustomobject]@{SteamId=$steamId;CharacterId=[string]$character.characterId;PublicName=[string]$character.name;Headers=$headers;Log=$null;Process=$null;StarterItem=$starter}
    })
    if ($VerifyRemainingPartySettlement) {
        @($members | ForEach-Object { [ordered]@{characterId=$_.CharacterId;gold=0;starterItem=$_.StarterItem} }) |
            ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $runRoot 'InitialEquipment.proof.json') -Encoding UTF8
    }
    # Settlement assertions use the authored Hard reward (500 gold, 3 potions, 1 helm).
    $fixtureDifficulty=if ($VerifyRemainingPartySettlement) {'Hard'} else {'Normal'}
    $fixtureDungeonId=if ($VerifyRemainingPartySettlement) {'Dungeon.RaidSwordMan'} else {'Dungeon.MembershipE2E'}
    $session=Invoke-Api 'dungeon-sessions' 'Post' $members[0].Headers @{characterId=$members[0].CharacterId;dungeonId=$fixtureDungeonId;difficulty=$fixtureDifficulty} 201
    $sessionId=[string]$session.dungeonSessionId
    foreach ($member in @($members | Select-Object -Skip 1)) { $session=Invoke-Api "dungeon-sessions/$sessionId/members" 'Post' $member.Headers @{characterId=$member.CharacterId} }
    if ($session.state -ne 'Waiting' -or @($session.members).Count -ne $ClientCount) { throw 'Waiting fixture party mismatch.' }
    $serverId="membership-$($runId.Substring(0,12))"
    . (Join-Path $projectRoot 'Backend/Tools/Request-DungeonDeparture.ps1')
    $null = Request-RPGDungeonDeparture $baseUrl $sessionId $members[0].CharacterId $members[0].Headers
    $assignment=Invoke-Api "dungeon-sessions/$sessionId/activate" 'Post' $adminHeaders @{serverId=$serverId;serverAddress="127.0.0.1:$Port"}
    $serverHeaders=@{Authorization="Bearer $($assignment.gameServerAccessToken)"}
    $maintainStartupLease=$true
    try {
        $env:PROJECT_RPG_GAME_SERVER_ID=$serverId; $env:PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId
        $env:PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken
        $serverWebBase=$baseUrl
        if ($VerifyReconnectAdmissionLoss) {
            $env:PROJECT_RPG_CONFIRM_HOLD_MODE=if ($VerifyReconnectConfirmationResponseLoss) {'after-commit'} else {'before-commit'}
            $env:PROJECT_RPG_CONFIRM_HOLD_UPSTREAM=$uri.Port.ToString()
            $env:PROJECT_RPG_CONFIRM_HOLD_ROOT=$runRoot
            $env:PROJECT_RPG_CONFIRM_HOLD_CHARACTER=$members[0].CharacterId
            $env:PROJECT_RPG_CONFIRM_HOLD_STEAM=$members[0].SteamId
            $sha=[Security.Cryptography.SHA256]::Create()
            try { $env:PROJECT_RPG_CONFIRM_HOLD_AUTH_HASH=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($serverHeaders.Authorization))).Replace('-','').ToLowerInvariant() }
            finally { $sha.Dispose() }
            $proxyOut=Join-Path $runRoot 'confirmation-proxy.stdout.log'; $proxyErr=Join-Path $runRoot 'confirmation-proxy.stderr.log'
            $proxyProcess=Start-Process -FilePath (Get-Command node -ErrorAction Stop).Source -ArgumentList ('"'+$confirmationProxy+'"') `
                -WorkingDirectory $projectRoot -RedirectStandardOutput $proxyOut -RedirectStandardError $proxyErr -PassThru -WindowStyle Hidden
            $processes.Add($proxyProcess); $logs.Add($proxyOut); $logs.Add($proxyErr)
            foreach($name in @('PROJECT_RPG_CONFIRM_HOLD_MODE','PROJECT_RPG_CONFIRM_HOLD_UPSTREAM','PROJECT_RPG_CONFIRM_HOLD_ROOT','PROJECT_RPG_CONFIRM_HOLD_CHARACTER','PROJECT_RPG_CONFIRM_HOLD_STEAM','PROJECT_RPG_CONFIRM_HOLD_AUTH_HASH')) {
                [Environment]::SetEnvironmentVariable($name,$previousEnv[$name],'Process')
            }
            $proxyDeadline=[DateTimeOffset]::UtcNow.AddSeconds(15)
            $ready=[regex]::Match('','DUNGEON_CONFIRM_HOLD_READY Port=(\d+)')
            do {
                Assert-Healthy
                $proxyText=Get-Content -LiteralPath $proxyOut -Raw -ErrorAction SilentlyContinue
                if (-not [string]::IsNullOrEmpty($proxyText)) { $ready=[regex]::Match($proxyText,'DUNGEON_CONFIRM_HOLD_READY Port=(\d+)') }
                if ($ready.Success) { break }
                Start-Sleep -Milliseconds 100
            } while([DateTimeOffset]::UtcNow -lt $proxyDeadline)
            if (-not $ready.Success) { throw 'Confirmation hold proxy did not start.' }
            $serverWebBase="http://127.0.0.1:$($ready.Groups[1].Value)"
        }
        if ($VerifyRemainingPartySettlement) {
            $serverLog=Start-Unreal 'server' '/Game/Maps/bossmap' @('-server',"-port=$Port",'-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity','-RPGDungeonFlowE2E',"-RPGFlowE2EPlayers=$ClientCount",('-RPGFlowE2EGate="'+$victoryGate+'"')) -WebApiBase $serverWebBase
        } else {
            $serverLog=Start-Unreal 'server' '/Game/Maps/testmap?listen?game=/Script/Project_RPG.RPGDungeonEjectionNetworkGameMode' @('-server',"-port=$Port",'-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity',"-RPGAbandonCharacter=$($members[0].CharacterId)")
        }
    } finally { foreach ($name in $serverEnv) { [Environment]::SetEnvironmentVariable($name,$null,'Process') } }
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds); $started=$false
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        $state=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
        if ($state.state -eq 'InProgress') { $started=$true; break }
        if ($state.state -ne 'Loading') { throw 'Session left Loading before UE startup.' }
        Start-Sleep -Milliseconds 250
    }
    if (-not $started) { throw 'UE server did not start its backend session.' }
    if ($VerifyGameplayAbandonmentResponseLoss) { Start-DungeonGameplayAbandonmentProxy }
    for ($index=0;$index -lt $members.Count;$index++) {
        $member=$members[$index]
        $clientArgs=@('-game','-RPGDungeonFlowE2E',"-RPGFlowE2ESteam=$($member.SteamId)","-RPGFlowE2ECharacter=$($member.CharacterId)","-RPGFlowE2ESession=$sessionId")
        if ($VerifyGameplayAbandonment -and $index -eq 0) {
            $clientArgs+=@('-RPGFlowE2EGameplayAbandonment','-RPGDungeonAbandonmentTesting','-RPGDungeonGameplayAbandonmentTesting',
                ('-RPGFlowE2EGameplayConsentGate="'+$gameplayConsentGate+'"'),('-RPGFlowE2EGameplayConfirmGate="'+$gameplayConfirmGate+'"'))
            if ($VerifyGameplayAbandonmentResponseLoss) {
                $clientArgs+=@('-RPGFlowE2EGameplayAbandonmentResponseLoss','-RPGDevelopmentLogin',
                    ('-RPGFlowE2EAbandonmentRetryFile="'+$lobbyRetryGate+'"'))
                if ($VerifyGameplayAbandonmentReauthentication) {
                    $clientArgs+=@('-RPGFlowE2EAbandonmentReauthenticate',
                        ('-RPGFlowE2EAbandonmentRenewedRetryFile="'+$lobbyRenewedRetryGate+'"'))
                }
            }
        }
        if ($VerifyAbandonedClientLobbyReturn -and $index -eq 0) { $clientArgs+='-RPGFlowE2EVerifyAbandonedReturn' }
        if ($VerifyAbandonedClientReceipt -and $index -eq 0) {
            $clientArgs+=@('-RPGFlowE2EVerifyAbandonedReceipt','-RPGDungeonAbandonmentTesting','-RPGDevelopmentLogin')
        }
        if ($VerifyDungeonClientInitialAbandonment -and $index -eq 0) {
            $clientArgs+=@('-RPGFlowE2EInitialAbandonmentFromDungeon',
                ('-RPGFlowE2EInitialAbandonmentGateFile="'+$initialAbandonmentGate+'"'))
        }
        $clientWebBase=if ($VerifyGameplayAbandonmentResponseLoss -and $index -eq 0) {$gameplayProxyUrl} else {$baseUrl}
        $member.Log=Start-Unreal "client-$($index+1)" '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' $clientArgs -WebApiBase $clientWebBase
        $member.Process=$processes[$processes.Count-1]
    }
    foreach ($member in $members) { Wait-Marker $member.Log 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1' }
    $partyReadyMarker=if ($VerifyRemainingPartySettlement) {'DUNGEON_ABANDON_SETTLEMENT PARTY_READY '} else {'RPG_EJECTION_NET BACKEND_READY '}
    Wait-Marker $serverLog $partyReadyMarker
    if ($ObservePartyCombatHud) {
        Wait-PartyHudRenderedState 'client-1' 'initial' $members[0].PublicName
        Wait-PartyHudRenderedState 'client-2' 'initial' $members[1].PublicName
    }
    if ($ObservePartyCombatVitals) { $partyVitalsEvidence=Invoke-PartyHudVitalsScenario }
    if ($ObservePartyCombatInput) {
        $null=New-Item -ItemType File -Path ($victoryGate+'.input') -ErrorAction Stop
        Wait-Marker $members[0].Log 'PARTY_HUD_INPUT COMPLETE Baseline=1 Visible=1 SamePawn=1 SameSession=1 CursorUnchanged=1 Physical=0'
        Wait-PartyHudRenderedState 'client-1' 'input-passed' $members[0].PublicName
        $inputMembership=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $serverHeaders
        if ($inputMembership.state -ne 'InProgress' -or @($inputMembership.members).Count -ne 2 -or
            @($inputMembership.abandonments).Count -ne 0) { throw 'Input observation changed authoritative party membership or ended the encounter.' }
    } else {
    $maintainStartupLease=$false
    $abandonPath="dungeon-sessions/$sessionId/members/$($members[0].CharacterId)/abandon"
    $expectedRemaining=@($members | Select-Object -Skip 1 | ForEach-Object CharacterId | Sort-Object)
    if ($VerifyReconnectWithinGrace) { $expectedRemaining=@($members | ForEach-Object CharacterId | Sort-Object) }
    if ($VerifyUnexpectedDisconnect) {
        if ($VerifyReconnectWithinGrace) { $disconnectEvidence=Invoke-DungeonReconnectWithinGraceScenario }
        else { $disconnectEvidence=Invoke-DungeonUnexpectedDisconnectScenario }
    } else {
    $null=Invoke-Api $abandonPath 'Post' $members[1].Headers $null 403
    $before=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $serverHeaders
    if (@($before.abandonments).Count -ne 0 -or @($before.members).Count -ne $ClientCount -or $before.state -ne 'InProgress') { throw 'Rejected request changed membership.' }
    if ($VerifyAbandonmentResponseLoss) {
        # Mint an unused ticket before the irreversible POST. Its value stays in
        # memory and must become unusable even while the caller lacks a receipt.
        $staleTicket=Invoke-Api 'join-tickets' 'Post' $members[0].Headers @{dungeonSessionId=$sessionId;characterId=$members[0].CharacterId}
        if ($staleTicket.dungeonSessionId -ne $sessionId -or $staleTicket.characterId -ne $members[0].CharacterId -or
            [string]::IsNullOrWhiteSpace($staleTicket.joinTicket)) { throw 'Pre-abandonment admission ticket mismatch.' }
        $node=(Get-Command node -ErrorAction Stop).Source
        $controlToken=([guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N'))
        $authorization=[string]$members[0].Headers.Authorization
        $sha=[Security.Cryptography.SHA256]::Create()
        try {
            $authorizationSHA256=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($authorization))).Replace('-','').ToLowerInvariant()
        } finally { $sha.Dispose() }
        $proxyOut=Join-Path $runRoot 'abandon-proxy.stdout.log'
        $proxyErr=Join-Path $runRoot 'abandon-proxy.stderr.log'
        try {
            $env:PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN=$controlToken
            $env:PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT=[string]$uri.Port
            $env:PROJECT_RPG_ABANDON_PROXY_TRANSPARENT=$(if ($VerifyAbandonmentClientRetry) {'1'} else {'0'})
            $proxy=Start-Process -FilePath $node -ArgumentList ('"'+$abandonProxyScript+'"') -WorkingDirectory $projectRoot `
                -RedirectStandardOutput $proxyOut -RedirectStandardError $proxyErr -PassThru -WindowStyle Hidden
        } finally {
            foreach ($name in @('PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN','PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT','PROJECT_RPG_ABANDON_PROXY_TRANSPARENT')) {
                [Environment]::SetEnvironmentVariable($name,$previousEnv[$name],'Process')
            }
        }
        $processes.Add($proxy)
        $logs.Add($proxyOut); $logs.Add($proxyErr)
        $proxyReady=[regex]::Match('', 'ABANDON_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)')
        $proxyDeadline=[DateTime]::UtcNow.AddSeconds(15)
        while ([DateTime]::UtcNow -lt $proxyDeadline) {
            Assert-Healthy
            if ((Test-Path -LiteralPath $proxyOut) -and (Get-Item -LiteralPath $proxyOut).Length -gt 0) {
                $proxyReady=[regex]::Match([string](Get-Content -LiteralPath $proxyOut -Raw -Encoding UTF8),
                    'ABANDON_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)')
                if ($proxyReady.Success) { break }
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $proxyReady.Success) { throw 'Abandonment response-loss proxy did not start.' }
        $controlUrl="http://127.0.0.1:$($proxyReady.Groups[1].Value)"
        $proxyUrl="http://127.0.0.1:$($proxyReady.Groups[2].Value)"
        $controlHeaders=@{Authorization="Bearer $controlToken"}
        $armed=Invoke-RestMethod -Method Post -Uri "$controlUrl/arm" -Headers $controlHeaders -ContentType 'application/json' `
            -Body (@{sessionId=$sessionId;characterId=$members[0].CharacterId;
                authorizationSHA256=$(if ($VerifyAbandonmentClientRetry) {$null} else {$authorizationSHA256})} | ConvertTo-Json -Compress)
        if ($armed.phase -ne 'armed') { throw 'Abandonment proxy did not arm the exact player request.' }
        if ($VerifyAbandonmentClientRetry) {
            $lobbyExtra=@('-game','-RPGDevelopmentLogin',
                '-RPGDungeonFlowE2E','-RPGFlowE2ELobbyUI','-RPGFlowE2EAbandonmentUI','-RPGDungeonAbandonmentTesting',
                "-RPGFlowE2ESteam=$($members[0].SteamId)","-RPGFlowE2ECharacter=$($members[0].CharacterId)",
                "-RPGFlowE2ESession=$sessionId",('-RPGFlowE2EAbandonmentStartFile="'+$lobbyStartGate+'"'),
                ('-RPGFlowE2EAbandonmentRetryFile="'+$lobbyRetryGate+'"'))
            if ($VerifyAbandonmentReauthentication) {
                $lobbyExtra+=@('-RPGFlowE2EAbandonmentReauthenticate',
                    ('-RPGFlowE2EAbandonmentRenewedRetryFile="'+$lobbyRenewedRetryGate+'"'))
            }
            $lobbyAbandonLog=Start-Unreal 'lobby-abandon' '/Game/Maps/LobbyMap' $lobbyExtra -WebApiBase $proxyUrl -LobbyAbandonment
            Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI READY LiveLobby=1 OwnedSelection=1 ResumedSession=1'
            New-Item -ItemType File -Path $lobbyStartGate | Out-Null
            Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI SUBMITTED LiveConfirmation=1 PlayerPost=1'
            $proxyDeadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                Assert-Healthy
                $proxyEvidence=Invoke-RestMethod -Method Get -Uri "$controlUrl/state" -Headers $controlHeaders
                if ($proxyEvidence.phase -eq 'dropped' -or $proxyEvidence.failure) { break }
                Start-Sleep -Milliseconds 250
            } while ([DateTime]::UtcNow -lt $proxyDeadline)
            Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1'
            $proxyEvidence=Invoke-RestMethod -Method Get -Uri "$controlUrl/state" -Headers $controlHeaders
            if ($proxyEvidence.blockedReplays -lt 1 -or $proxyEvidence.replayForwarded -ne 0) {
                throw 'Automatic UE HTTP replay was not held before the manual retry gate.'
            }
        } else {
            $proxyDelivered=$true
            try {
                $null=Invoke-WebRequest -Method Post -Uri "$proxyUrl/api/$abandonPath" -Headers $members[0].Headers -UseBasicParsing -TimeoutSec 20
            } catch {
                if ($null -ne $_.Exception.Response) { throw 'Fault proxy returned HTTP status instead of losing the response.' }
                $proxyDelivered=$false
            }
            if ($proxyDelivered) { throw 'Committed abandonment response unexpectedly reached the caller.' }
            $proxyEvidence=Invoke-RestMethod -Method Get -Uri "$controlUrl/state" -Headers $controlHeaders
        }
        if ($proxyEvidence.phase -ne 'dropped' -or $proxyEvidence.forwarded -ne 1 -or
            $proxyEvidence.droppedAcknowledgements -ne 1 -or
            $proxyEvidence.receiptSHA256 -cnotmatch '^[0-9a-f]{64}$' -or $proxyEvidence.failure) {
            throw 'Abandonment proxy did not observe exactly one durable response loss.'
        }
        foreach ($id in @($sessionId,$members[0].CharacterId)) {
            if ($id -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Noncanonical proxy fixture UUID.' }
        }
        $preRetrySql="SELECT json_build_object('receipts',(SELECT COUNT(*) FROM $schema.dungeon_session_abandonments WHERE dungeon_session_id='$sessionId' AND character_id='$($members[0].CharacterId)'),'members',(SELECT COUNT(*) FROM $schema.dungeon_session_members WHERE dungeon_session_id='$sessionId' AND character_id='$($members[0].CharacterId)'),'tickets',(SELECT COUNT(*) FROM $schema.game_join_tickets WHERE dungeon_session_id='$sessionId' AND character_id='$($members[0].CharacterId)'));"
        $preRetryRows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $preRetrySql
        if ($LASTEXITCODE -ne 0) { throw 'Pre-retry durable abandonment read failed.' }
        $preRetry=$preRetryRows | ConvertFrom-Json
        if ($preRetry.receipts -ne 1 -or $preRetry.members -ne 0 -or $preRetry.tickets -ne 0) {
            throw 'Lost response was not followed by durable receipt/member/ticket revocation.'
        }
        $null=Invoke-Api 'join-tickets/consume' 'Post' $serverHeaders @{joinTicket=$staleTicket.joinTicket;serverId=$serverId} 401
        $null=Invoke-Api 'join-tickets' 'Post' $members[0].Headers @{dungeonSessionId=$sessionId;characterId=$members[0].CharacterId} 409
        $null=Invoke-Api "dungeon-sessions/$sessionId/members" 'Post' $members[0].Headers @{characterId=$members[0].CharacterId} 409
        if ($VerifyAbandonmentReauthentication) {
            $fixtureSteamId=[string]$members[0].SteamId
            if ($fixtureSteamId -cnotmatch '^[0-9]{17}$') { throw 'Noncanonical owned fixture account.' }
            $expirySql="WITH expired AS (UPDATE $schema.auth_sessions SET expires_at=CURRENT_TIMESTAMP - INTERVAL '1 second' WHERE steam_id='$fixtureSteamId' AND expires_at>CURRENT_TIMESTAMP RETURNING 1) SELECT COUNT(*) FROM expired;"
            $expiryRows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $expirySql
            if ($LASTEXITCODE -ne 0 -or @($expiryRows).Count -ne 1 -or [string](@($expiryRows)[0]) -cnotmatch '^[0-9]+$') {
                throw 'Run-owned account authentication expiry fixture failed.'
            }
            $expiredSessionCount=[int](@($expiryRows)[0])
            if ($expiredSessionCount -lt 2) { throw 'UE and harness sessions were not both expired in the owned schema.' }
        }
    }
    if ($VerifyAbandonmentResponseLoss) {
        if ($VerifyAbandonmentClientRetry) {
            $released=Invoke-RestMethod -Method Post -Uri "$controlUrl/release-replay" -Headers $controlHeaders
            if (-not $released.replayReleased -or $released.replayForwarded -ne 0) {
                throw 'Manual UE retry was not released from the fault proxy.'
            }
            New-Item -ItemType File -Path $lobbyRetryGate | Out-Null
            if ($VerifyAbandonmentReauthentication) {
                Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI AUTH_EXPIRED Real401=1 AccountPinned=1 IdentityRetained=1 NewDungeonLocked=1 LiveLogin=1'
                $proxyAfter401=Invoke-RestMethod -Method Get -Uri "$controlUrl/state" -Headers $controlHeaders
                if ($proxyAfter401.unauthorizedForwarded -ne 1 -or $proxyAfter401.replayForwarded -ne 0) {
                    throw 'The expired UE retry did not receive exactly one authenticated backend 401.'
                }
                $renewedAuth=Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$fixtureSteamId"}
                if ([string]::IsNullOrWhiteSpace([string]$renewedAuth.accessToken)) { throw 'Harness account reauthentication failed.' }
                $members[0].Headers=@{Authorization="Bearer $($renewedAuth.accessToken)"}
                Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI AUTH_RESTORED SameAccount=1 OwnedRoster=1 IdentityRetained=1 RetryEnabled=1'
                New-Item -ItemType File -Path $lobbyRenewedRetryGate | Out-Null
                Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI RENEWED_RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1'
            }
            Wait-Marker $lobbyAbandonLog 'DUNGEON_ABANDON_UI RECOVERED LiveButton=1 ReceiptApplied=1 SelectionRetained=1 NewDungeonEnabled=1'
            $proxyReplay=Invoke-RestMethod -Method Get -Uri "$controlUrl/state" -Headers $controlHeaders
            if ($proxyReplay.replayForwarded -ne 1 -or $proxyReplay.replayReceiptSHA256 -cne $proxyEvidence.receiptSHA256) {
                throw 'Live UE retry did not receive the same durable receipt.'
            }
        }
        $retryHttp=Invoke-WebRequest -Method Post -Uri "$baseUrl/api/$abandonPath" -Headers $members[0].Headers -UseBasicParsing -TimeoutSec 10
        if ([int]$retryHttp.StatusCode -ne 200) { throw 'Lost abandonment acknowledgement did not recover with 200.' }
        $sha=[Security.Cryptography.SHA256]::Create()
        try {
            $retrySHA256=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]$retryHttp.Content))).Replace('-','').ToLowerInvariant()
        } finally { $sha.Dispose() }
        if ($retrySHA256 -cne $proxyEvidence.receiptSHA256) { throw 'Retried abandonment response changed from the lost upstream receipt.' }
        $receipt=$retryHttp.Content | ConvertFrom-Json
    } elseif ($VerifyGameplayAbandonment) {
        $receipt=Invoke-DungeonGameplayAbandonmentScenario
    } elseif ($VerifyDungeonClientInitialAbandonment) {
        New-Item -ItemType File -Path $initialAbandonmentGate | Out-Null
        Wait-Marker $members[0].Log 'DUNGEON_FLOW_E2E_INITIAL_ABANDONMENT_SUBMITTED SameProcess=1 AdmittedClient=1 PlayerPost=1'
        Wait-Marker $members[0].Log 'DUNGEON_FLOW_E2E_INITIAL_ABANDONMENT_RECEIPT SameProcess=1 UEPlayerPost=1 '
        $receipt=Invoke-Api $abandonPath 'Post' $members[0].Headers
        $receiptLine=@(Select-String -LiteralPath $members[0].Log -Pattern 'DUNGEON_FLOW_E2E_INITIAL_ABANDONMENT_RECEIPT SameProcess=1 UEPlayerPost=1 Session=([^ ]+) Character=([^ ]+) AbandonedAt=([^ ]+) RewardExcluded=1' | Select-Object -Last 1)
        if ($receiptLine.Count -ne 1 -or $receiptLine[0].Matches[0].Groups[1].Value -cne $sessionId -or
            $receiptLine[0].Matches[0].Groups[2].Value -cne $members[0].CharacterId -or
            $receiptLine[0].Matches[0].Groups[3].Value -cne $receipt.abandonedAt) {
            throw 'UE dungeon client did not receive the same immutable initial abandonment receipt.'
        }
    } else {
        $receipt=Invoke-Api $abandonPath 'Post' $members[0].Headers
    }
    # In combat response-loss mode the UE must remain unconfirmed through party
    # settlement. Do not obtain an owner replay here; recover via the live UI later.
    if (-not $VerifyGameplayAbandonmentResponseLoss) { $replay=Invoke-Api $abandonPath 'Post' $members[0].Headers }
    if ($receipt.dungeonSessionId -ne $sessionId -or $receipt.characterId -ne $members[0].CharacterId -or
        $receipt.sessionStateAtAbandonment -ne 'InProgress' -or $receipt.rewardExcluded -ne $true -or
        (-not $VerifyGameplayAbandonmentResponseLoss -and
            ($receipt | ConvertTo-Json -Compress) -cne ($replay | ConvertTo-Json -Compress))) { throw 'Wrong/nonimmutable player abandonment receipt.' }
    $snapshot=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $serverHeaders
    $snapshotReceipt=@($snapshot.abandonments)
    if ($snapshot.dungeonSessionId -ne $sessionId -or $snapshot.serverId -ne $serverId -or $snapshot.state -ne 'InProgress' -or
        @($snapshot.members).Count -ne ($ClientCount-1) -or $snapshotReceipt.Count -ne 1 -or
        $snapshotReceipt[0].characterId -ne $members[0].CharacterId -or $snapshotReceipt[0].steamId -ne $members[0].SteamId -or
        $snapshotReceipt[0].abandonedAt -ne $receipt.abandonedAt -or $snapshotReceipt[0].sessionStateAtAbandonment -ne 'InProgress' -or
        $snapshotReceipt[0].rewardExcluded -ne $true) { throw 'Assigned membership read did not contain the exact receipt.' }
    if ($ObservePartyCombatDeparture) {
        # Observe the real remaining recipient only after the backend committed
        # the immutable receipt. Actor absence or a countdown is not evidence.
        Wait-PartyHudRenderedState 'client-2' 'departure-confirmed' $members[1].PublicName -RemovedNames @($members[0].PublicName)
    }
    # Independent durable rows, not only API/UE observations. IDs originate from
    # the API; UUID validation below prevents SQL interpolation of arbitrary input.
    foreach ($id in @($sessionId,$members[0].CharacterId)) { if ($id -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Noncanonical API UUID.' } }
    $sql="SELECT json_build_object('receipts',(SELECT json_agg(row_to_json(r)) FROM (SELECT character_id,steam_id,abandoned_at,session_state_at_abandonment FROM $schema.dungeon_session_abandonments WHERE dungeon_session_id='$sessionId') r),'members',(SELECT json_agg(character_id ORDER BY character_id) FROM $schema.dungeon_session_members WHERE dungeon_session_id='$sessionId'));"
    $dbRows = & docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
    if ($LASTEXITCODE -ne 0) { throw 'Independent PostgreSQL read failed.' }
    $db=$dbRows | ConvertFrom-Json
    $dbReceipt=@($db.receipts)
    $expectedRemaining=@($members | Select-Object -Skip 1 | ForEach-Object CharacterId | Sort-Object)
    if ($dbReceipt.Count -ne 1 -or $dbReceipt[0].character_id -ne $members[0].CharacterId -or $dbReceipt[0].steam_id -ne $members[0].SteamId -or
        $dbReceipt[0].session_state_at_abandonment -ne 'InProgress' -or
        [DateTimeOffset]::Parse($dbReceipt[0].abandoned_at) -ne [DateTimeOffset]::Parse($receipt.abandonedAt) -or
        @($db.members).Count -ne ($ClientCount-1) -or (Compare-Object $expectedRemaining @($db.members | Sort-Object))) { throw 'Durable abandonment/remaining-party mismatch.' }
    if (-not $VerifyGameplayAbandonment) { Wait-Marker $members[0].Log 'RPG_EJECTION_NET CLIENT_DISCONNECTED ' }
    if ($VerifyAbandonedClientLobbyReturn) {
        Wait-Marker $members[0].Log 'DUNGEON_FLOW_E2E_ABANDONED_LOBBY_RETURN SameProcess=1 ActualMap=1 Standalone=1 AuthSelectionSessionRetained=1 NoImplicitReceipt=1 NoJoinTicket=1'
        if ($VerifyAbandonedClientReceipt) {
            Wait-Marker $members[0].Log 'DUNGEON_FLOW_E2E_ABANDONED_RECEIPT_SUBMITTED SameProcess=1 LiveLobbyConfirmation=1 PlayerPost=1'
            Wait-Marker $members[0].Log 'DUNGEON_FLOW_E2E_ABANDONED_RECEIPT_APPLIED SameProcess=1 ReceiptAccepted=1 SelectionAuthRetained=1 SessionCleared=1 NewDungeonEnabled=1'
        }
    }
    }
    if ($ObservePartyCombatExpiry) {
        Wait-PartyHudRenderedState 'client-2' 'expiry-confirmed' $members[1].PublicName -RemovedNames @($members[0].PublicName)
        Wait-Marker $serverLog 'DUNGEON_ABANDON_SETTLEMENT REMAINING_READY '
        $null=Invoke-Api "dungeon-sessions/$sessionId/reward-snapshot/$($members[0].CharacterId)" 'Get' $serverHeaders $null 403
        $departureEvidence=[ordered]@{serverRemovedTargetActors=$true;sameRemainingPeerAndEncounter=$true;
            unfinishedRewardSnapshotDenied=$true;encounterState='InProgress';remainingMembers=1;
            explicitAbandonment=$false;durableReceipt=$disconnectEvidence.durableReceipt;httpReceipt=$disconnectEvidence.httpReceipt}
        # This is not the remaining-party reward/return scenario. Keep its gate closed.
    } elseif ($ObservePartyCombatDeparture) {
        Wait-Marker $serverLog 'DUNGEON_ABANDON_SETTLEMENT REMAINING_READY '
        $null=Invoke-Api "dungeon-sessions/$sessionId/reward-snapshot/$($members[0].CharacterId)" 'Get' $serverHeaders $null 403
        $departureMembership=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $serverHeaders
        if ($departureMembership.state -ne 'InProgress' -or @($departureMembership.members).Count -ne 1 -or
            $departureMembership.members[0].characterId -cne $members[1].CharacterId -or
            @($departureMembership.abandonments).Count -ne 1 -or
            $departureMembership.abandonments[0].abandonedAt -cne $receipt.abandonedAt) {
            throw 'Remaining encounter or immutable departure changed during HUD observation.'
        }
        $departureEvidence=[ordered]@{receipt=$receipt;independentDurableState=$db;
            serverRemovedTargetActors=$true;sameRemainingPeerAndEncounter=$true;
            unfinishedRewardSnapshotDenied=$true;encounterState='InProgress';remainingMembers=1}
        # Do not release the victory gate or replay the already-covered reward /
        # lobby pipeline in a row-removal-only observation.
    } elseif ($VerifyRemainingPartySettlement) {
        if (-not $VerifyReconnectWithinGrace) { Wait-Marker $serverLog 'DUNGEON_ABANDON_SETTLEMENT REMAINING_READY ' }
        # Enable only the existing server GAS hit. The runner never calls the
        # settlement API, sets Dead/Victory, or injects a player reward snapshot.
        New-Item -ItemType File -Path $victoryGate | Out-Null
        Wait-Marker $serverLog $(if ($VerifyReconnectWithinGrace) {'DUNGEON_RECONNECT_SETTLEMENT VICTORY '} else {'DUNGEON_ABANDON_SETTLEMENT VICTORY '})
        $rewardedMembers=if ($VerifyReconnectWithinGrace) {@($members)} else {@($members | Select-Object -Skip 1)}
        foreach ($member in $rewardedMembers) {
            $memberLog=if ($VerifyReconnectWithinGrace -and $member.CharacterId -eq $members[0].CharacterId) {$reconnectedClientLog} else {$member.Log}
            Wait-Marker $memberLog 'DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0'
            Wait-Marker $memberLog 'DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1'
        }
        $terminal=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
        if ($terminal.state -ne 'Cleared' -or (Compare-Object $expectedRemaining @($terminal.members | ForEach-Object characterId | Sort-Object))) { throw 'Wrong terminal party membership.' }
        if ($VerifyReconnectWithinGrace) {
            $afterSettlement=Get-DungeonDisconnectDurableState
            if (@($afterSettlement.receipts | Where-Object { $null -ne $_ }).Count -or $afterSettlement.abandonments -ne 0 -or
                @($afterSettlement.members | Where-Object { $_.disconnected_at }).Count -or
                (Compare-Object $expectedRemaining @($afterSettlement.members | ForEach-Object character_id | Sort-Object))) {
                throw 'Recovered membership was expelled or disconnected during settlement/return.'
            }
        } elseif ($VerifyUnexpectedDisconnect) {
            $afterSettlement=Get-DungeonDisconnectDurableState
            $terminalReceipt=@($afterSettlement.receipts)
            if ($terminalReceipt.Count -ne 1 -or $afterSettlement.abandonments -ne 0 -or
                ($disconnectEvidence.durableReceipt | ConvertTo-Json -Compress) -cne ($terminalReceipt[0] | ConvertTo-Json -Compress)) {
                throw 'Disconnect receipt changed after settlement.'
            }
        } else {
            if ($VerifyGameplayAbandonmentResponseLoss) { Complete-DungeonGameplayAbandonmentRecovery $receipt }
            $terminalReplay=Invoke-Api $abandonPath 'Post' $members[0].Headers
            if (($receipt | ConvertTo-Json -Compress) -cne ($terminalReplay | ConvertTo-Json -Compress)) { throw 'Receipt changed after settlement.' }
        }
        $null=Invoke-Api "dungeon-sessions/$sessionId/reward-snapshot/$($members[0].CharacterId)" 'Get' $serverHeaders $null $(if ($VerifyReconnectWithinGrace) {200} else {403})
        $rewardObservations=@(foreach ($member in $members) {
            $wallet=Invoke-Api "economy/wallets/$($member.CharacterId)" 'Get' $member.Headers
            $gold=@($wallet.balances | Where-Object currencyCode -eq 'RosterGold')
            $itemPage=Invoke-Api "items?ownerType=Character&ownerId=$($member.CharacterId)&includeTerminal=false&limit=500" 'Get' $member.Headers
            $items=@($itemPage.items)
            $excluded= -not $VerifyReconnectWithinGrace -and $member.CharacterId -eq $members[0].CharacterId
            $starter=@($items | Where-Object { $_.state.instanceId -eq $member.StarterItem.state.instanceId })
            if ($starter.Count -ne 1 -or ($starter[0] | ConvertTo-Json -Depth 20 -Compress) -cne
                ($member.StarterItem | ConvertTo-Json -Depth 20 -Compress)) { throw 'Original starter equipment was changed or removed.' }
            $rewardItems=@($items | Where-Object { $_.state.instanceId -ne $member.StarterItem.state.instanceId })
            if ($excluded) {
                if ($rewardItems.Count -ne 0 -or @($gold | Where-Object balance -ne 0).Count) { throw 'Excluded character received rewards beyond its initial equipment.' }
            } else {
                $potion=@($rewardItems | Where-Object definitionName -eq 'GameItem.Consume.Potion.Red.Large')
                $helm=@($rewardItems | Where-Object definitionName -eq 'GameItem.Equipment.Helm.Default')
                if ($gold.Count -ne 1 -or $gold[0].balance -ne 500 -or $rewardItems.Count -ne 2 -or $potion.Count -ne 1 -or $helm.Count -ne 1 -or
                    $potion[0].state.quantity -ne 3 -or $helm[0].state.quantity -ne 1 -or
                    @($rewardItems | Where-Object { $_.location.containerType -ne 'Inventory' -or $_.revision -ne 1 }).Count -ne 0) { throw 'Remaining character reward mismatch.' }
            }
            [ordered]@{characterId=$member.CharacterId;excluded=$excluded;gold=$(if ($excluded) {0} else {500});inventoryQuantity=$(if ($excluded) {0} else {4});mailQuantity=0}
        })
        foreach ($member in $members) { if ($member.CharacterId -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Noncanonical character UUID.' } }
        $characterSql=@($members | ForEach-Object { "'$($_.CharacterId)'" }) -join ','
        $sql="SELECT json_build_object('settlement',(SELECT row_to_json(s) FROM (SELECT state,character_ids FROM $schema.dungeon_reward_settlements WHERE dungeon_session_id='$sessionId') s),'characters',(SELECT json_agg(json_build_object('characterId',c.character_id,'gold',COALESCE(b.balance,0),'itemCount',(SELECT COUNT(*) FROM $schema.item_records i WHERE i.owner_type='Character' AND i.owner_id=c.character_id::text),'quantity',(SELECT COALESCE(SUM(quantity),0) FROM $schema.item_records i WHERE i.owner_type='Character' AND i.owner_id=c.character_id::text))) FROM $schema.characters c LEFT JOIN $schema.currency_balances b ON b.owner_type='Roster' AND b.owner_id=c.roster_id::text AND b.currency_code='RosterGold' WHERE c.character_id IN ($characterSql)));"
        $persistedRows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
        if ($LASTEXITCODE -ne 0) { throw 'Independent settlement SQL read failed.' }
        $persisted=$persistedRows | ConvertFrom-Json
        if ($persisted.settlement.state -ne 'Completed' -or (Compare-Object $expectedRemaining @($persisted.settlement.character_ids | Sort-Object)) -or
            @($persisted.characters).Count -ne $ClientCount) { throw 'Durable settlement recipient mismatch.' }
        foreach ($character in $persisted.characters) {
            $excluded= -not $VerifyReconnectWithinGrace -and $character.characterId -eq $members[0].CharacterId
            if ($character.gold -ne $(if ($excluded) {0} else {500}) -or $character.itemCount -ne $(if ($excluded) {1} else {3}) -or
                $character.quantity -ne $(if ($excluded) {1} else {5})) { throw 'Durable reward exclusion or delivery mismatch.' }
        }
    } else {
        Wait-Marker $serverLog 'RPG_EJECTION_NET SERVER_COMPLETE '
        foreach ($member in @($members | Select-Object -Skip 1)) { Wait-Marker $member.Log 'RPG_EJECTION_NET CLIENT_COMPLETE ' }
    }
    }
    Assert-Healthy
    Assert-Unchanged
    $completed=$true
} catch {
    $failureMessage=$_.Exception.Message
} finally {
    if ($ObservePartyCombatHud) { Stop-PartyHudObservers }
    # Only processes started by this run. Close log writers before hashing.
    # Early-stop HUD modes deliberately leave the encounter InProgress. Stop
    # its owned server before clients, without waiting between kill requests:
    # otherwise the fixture interprets runner cleanup as a new party failure.
    $cleanupProcesses=@($processes | Sort-Object Id -Descending)
    if (($ObservePartyCombatInput -or $ObservePartyCombatDeparture -or $ObservePartyCombatExpiry) -and $script:PartyHudObservationServer) {
        $cleanupProcesses=@($script:PartyHudObservationServer)+@($cleanupProcesses |
            Where-Object { -not [object]::ReferenceEquals($_,$script:PartyHudObservationServer) })
        foreach ($proc in $cleanupProcesses) {
            $proc.Refresh()
            if (-not $proc.HasExited) { $proc.Kill() }
        }
    }
    foreach ($proc in $cleanupProcesses) {
        $proc.Refresh()
        if (-not $proc.HasExited) { $proc.Kill() }
        # Also wait for processes already signalled by the early-stop batch.
        if (-not $proc.WaitForExit(10000)) { throw 'Owned test process did not terminate; logs are not closed evidence.' }
    }
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name,$previousEnv[$name],'Process') }
}
if (-not $completed) {
    [ordered]@{passed=$false;runId=$runId;postgresSchema=$schema;failure=$failureMessage;moduleSHA256=$moduleHash;sourceAndRunnerHashes=$hashes;
        unexpectedDisconnect=$disconnectEvidence;
        partyCombatHud=$(if ($ObservePartyCombatHud) { [ordered]@{observations=@($script:PartyHudEvidence.ToArray());vitals=$partyVitalsEvidence} } else { $null });
        closedLogHashes=@(@($logs)+@($backendOut,$backendErr) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
            ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} })} |
        ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runRoot 'Failed.proof.json') -Encoding UTF8
    throw "$failureMessage; preserved attempt: $runRoot"
}
Assert-Unchanged
if ($ObservePartyCombatDeparture -or $ObservePartyCombatExpiry) {
    $closedHudLogHashes=@(Get-PartyHudClosedLogHashes (@($logs)+@($backendOut,$backendErr)))
    if (@($logs | Where-Object { Select-String -LiteralPath $_ -Pattern $failure -Quiet }).Count) {
        throw 'Closed departure/expiry-only logs contain a runtime or cleanup failure; do not promote the observation.'
    }
    $observations=@($script:PartyHudEvidence.ToArray())
    $initial=@($observations | Where-Object phase -eq 'initial')
    $removedPhase=if ($ObservePartyCombatExpiry) {'expiry-confirmed'} else {'departure-confirmed'}
    $removed=@($observations | Where-Object phase -eq $removedPhase)
    if ($initial.Count -ne 2 -or $removed.Count -ne 1 -or $removed[0].client -cne 'client-2' -or
        $removed[0].headerCount -ne 1 -or @($removed[0].rows).Count -ne 1 -or
        $removed[0].rows[0].name -cne $members[1].PublicName -or -not $removed[0].rows[0].self -or
        @($removed[0].removedNames).Count -ne 1 -or $removed[0].removedNames[0] -cne $members[0].PublicName -or
        -not $departureEvidence) { throw 'Missing actual retained/removed party row evidence.' }
    if ($ObservePartyCombatExpiry) {
        if (-not $disconnectEvidence.naturalTenMinuteWait -or $disconnectEvidence.playerAbandonment -or
            $disconnectEvidence.clockOverride -or $disconnectEvidence.harnessExpiryPost) { throw 'Missing natural authoritative expiry evidence.' }
        foreach ($phase in 'grace','grace-near-expiry') {
            $grace=@($observations | Where-Object phase -eq $phase)
            if ($grace.Count -ne 1 -or $grace[0].client -cne 'client-2' -or $grace[0].headerCount -ne 2 -or
                @($grace[0].rows | Where-Object { $_.name -ceq $members[0].PublicName -and $_.reconnectGrace -and -not $_.healthBarVisible }).Count -ne 1 -or
                [DateTimeOffset]::Parse($grace[0].observedAtUtc) -ge [DateTimeOffset]::Parse($disconnectEvidence.expiresAt)) {
                throw 'Missing rendered member retention before natural expiry.'
            }
        }
    }
    $proof=[ordered]@{schemaVersion=1;passed=$true;
        scope=$(if ($ObservePartyCombatExpiry) {'Natural ten-minute disconnect expiry to real rendered remaining-party HUD only'} else {'Explicit HTTP abandonment to real rendered remaining-party HUD only'});
        runId=$runId;postgresSchema=$schema;postgresSchemaRetained=$true;moduleSHA256=$moduleHash;
        sourceAndRunnerHashes=$hashes;departure=$departureEvidence;observations=$observations;
        closedLogsFailureFree=$true;
        snapshotInjection=$false;receiptInjection=$false;clockAcceleration=$false;naturalTenMinuteExpiry=[bool]$ObservePartyCombatExpiry;
        unexpectedDisconnect=$(if ($ObservePartyCombatExpiry) {$disconnectEvidence} else {$null});
        gameplayConsentUi=$false;rewardSettlement=$false;lobbyReturn=$false;steam=$false;shipping=$false;
        closedLogHashes=$closedHudLogHashes}
    $proofPath=Join-Path $runRoot $(if ($ObservePartyCombatExpiry) {'PartyHudExpiry.proof.json'} else {'PartyHudDeparture.proof.json'})
    $proof | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: immutable departure/expiry removes only the departed member's actual party HUD row; natural-expiry=$([bool]$ObservePartyCombatExpiry): $proofPath"
    return
}
if ($ObservePartyCombatInput) {
    $closedHudLogHashes=@(Get-PartyHudClosedLogHashes (@($logs)+@($backendOut,$backendErr)))
    $inputText=[IO.File]::ReadAllText($members[0].Log,[Text.Encoding]::UTF8)
    $phases=@(foreach($phase in 'baseline','visible') {
        $matches=[regex]::Matches($inputText,('PARTY_HUD_INPUT Phase='+$phase+' MoveDistance=([0-9.eE+-]+) AbilityActivated=1 ViewportHitTest=1 FocusRetained=1 Physical=0'))
        if ($matches.Count -ne 1) { throw 'Missing or duplicate native routed-input evidence.' }
        $distance=[double]::Parse($matches[0].Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
        if ([double]::IsNaN($distance) -or [double]::IsInfinity($distance) -or $distance -lt 3) { throw 'Input observation did not move the actual pawn.' }
        [ordered]@{phase=$phase;moveDistance=$distance;localAbilityActivated=$true;viewportHitTest=$true;focusRetained=$true}
    })
    $proof=[ordered]@{schemaVersion=1;passed=$true;scope='Authenticated two-client rendered HUD; local Slate-routed input only';
        runId=$runId;postgresSchema=$schema;postgresSchemaRetained=$true;moduleSHA256=$moduleHash;
        sourceAndRunnerHashes=$hashes;phases=$phases;observations=@($script:PartyHudEvidence.ToArray());
        inputFunctionsCalledDirectly=$false;actionInjection=$false;forceFocusOrInputMode=$false;physicalInput=$false;
        serverMovementOrAbilityApprovalVerified=$false;partyMembershipPreserved=$true;reconnect=$false;rewardSettlement=$false;
        steam=$false;shipping=$false;
        closedLogHashes=$closedHudLogHashes}
    $proofPath=Join-Path $runRoot 'PartyHudInput.proof.json'
    $proof | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: rendered party HUD preserves local Slate-routed movement and basic attack: $proofPath"
    return
}
$proofArgs=@{ServerText=(Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8);ClientTexts=@($members | ForEach-Object { Get-Content -LiteralPath $_.Log -Raw -Encoding UTF8 });ClientCount=$ClientCount}
if ($VerifyReconnectWithinGrace) { $proofArgs.ClientTexts+=@(Get-Content -LiteralPath $reconnectedClientLog -Raw -Encoding UTF8) }
if ($VerifyRemainingPartySettlement) {
    if ($VerifyReconnectWithinGrace) {
        if ($VerifyReconnectAdmissionLoss) {
            $proofArgs.AdmissionLoss=$true
            $proofArgs.ConfirmationResponseLoss=[bool]$VerifyReconnectConfirmationResponseLoss
            $proofArgs.PendingClientText=Get-Content -LiteralPath $pendingReconnectClientLog -Raw -Encoding UTF8
        }
        $proof=Get-DungeonReconnectSettlementProof @proofArgs
    }
    else { $proof=Get-DungeonAbandonmentSettlementProof @proofArgs -UnexpectedDisconnect:$VerifyUnexpectedDisconnect -GameplayAbandonment:$VerifyGameplayAbandonment -GameplayResponseLoss:$VerifyGameplayAbandonmentResponseLoss -GameplayReauthentication:$VerifyGameplayAbandonmentReauthentication }
    if ($VerifyReconnectWithinGrace) {
        $proof.reconnectedCharacterRewardIncluded=$true
        $proof.noExpiryAfterReconnection=$true
    } elseif ($VerifyUnexpectedDisconnect) {
        $proof.disconnectedRewardSnapshotRejected=$true
        $proof.disconnectedRewardExcluded=$true
    } else {
        $proof.abandonedRewardSnapshotRejected=$true
        $proof.abandonedRewardExcluded=$true
    }
    $proof.durableSettlement=$persisted
    $proof.rewards=$rewardObservations
    $proof.originalStarterEquipmentPreserved=$true
    $proof.initialEquipmentSnapshotSHA256=(Get-FileHash -LiteralPath (Join-Path $runRoot 'InitialEquipment.proof.json')).Hash
    if (-not $VerifyReconnectWithinGrace) { $proof.immutableReceiptAfterSettlement=$true }
    if (-not $VerifyUnexpectedDisconnect) { $proof.receiptReplayAfterSettlement=$true }
    if ($VerifyUnexpectedDisconnect) { $proof.unexpectedDisconnect=$disconnectEvidence }
    if ($VerifyGameplayAbandonment) { $proof.gameplayAbandonment=$gameplayEvidence }
    if ($VerifyAbandonedClientLobbyReturn) {
        $proof.abandonedClientSameProcessLobbyReturn=$true
        $proof.abandonedClientRetainedIdentityWithoutImplicitReceipt=$true
        if ($VerifyAbandonedClientReceipt) {
            $proof.abandonedClientSameProcessLiveReceiptReplay=$true
        }
        if ($VerifyDungeonClientInitialAbandonment) {
            $proof.dungeonClientInitialPlayerPostAndReceipt=$true
        }
    }
    if ($VerifyAbandonmentResponseLoss) {
        $proof.responseLoss=[ordered]@{upstream200AcknowledgementDropped=$true;forwarded=1;
            lostReceiptSHA256=$proxyEvidence.receiptSHA256;retriedReceiptSHA256=$retrySHA256;
            preRetryDurable=$preRetry;staleTicketConsumeRejected401=$true;
            freshTicketRejected409=$true;partyRejoinRejected409=$true}
        if ($VerifyAbandonmentClientRetry) {
            $proof.responseLoss.liveLobbyRetry=[ordered]@{liveLoginAndResume=$true;firstPostFromUE=$true;
                unresolvedIdentityAndNewDungeonLock=$true;secondPostFromUE=$true;
                automaticHttpReplaysHeld=$proxyEvidence.blockedReplays;
                replayForwarded=$proxyReplay.replayForwarded;replayReceiptSHA256=$proxyReplay.replayReceiptSHA256;
                acceptedReceiptAndLobbyUnlocked=$true;clientLog=$lobbyAbandonLog}
            if ($VerifyAbandonmentReauthentication) {
                $proof.responseLoss.liveLobbyRetry.reauthentication=[ordered]@{
                    expiredOwnedSessions=$expiredSessionCount;backend401Forwarded=$proxyAfter401.unauthorizedForwarded;
                    sameAccountLiveLoginAndRoster=$true;oldMutationIdentityRetained=$true;
                    renewedUEPostAcceptedSameReceipt=$true}
            }
        }
    }
} else {
    $proof=Get-DungeonEjectionNetworkProof @proofArgs -PacketLagMinMs 80 -PacketLagMaxMs 160 -PacketLossPercent 5 -RequireBackendMembership
}
if ($proof.backendSessionId -ne $sessionId -or $proof.backendServerId -ne $serverId) { throw 'Borrowed backend network scope.' }
foreach ($peer in $proof.backendPeers) {
    if (@($members | Where-Object { $_.CharacterId -eq $peer.characterId -and $_.SteamId -eq $peer.steamId }).Count -ne 1 -or
        ($peer.playerId -eq $proof.targetPlayerId -and $peer.characterId -ne $members[0].CharacterId)) { throw 'Borrowed actual admission identity.' }
}
$proof.database=$true
$proof.passed=$true
$proof.postgresSchema=$schema
$proof.postgresSchemaRetained=$true
$proof.runId=$runId
$proof.runtimeUserRoot=$runtimeUserRoot
if (-not $VerifyUnexpectedDisconnect) {
    $proof.wrongOwnerRejected=$true
    $proof.immutableReceiptReplay=$true
    $proof.playerHttpReceipt=$receipt
    $proof.durableReceipt=$dbReceipt[0]
}
$proof.startupLeaseHeartbeatCount=$startupLeaseHeartbeatCount
$proof.moduleSHA256=$moduleHash
if ($ObservePartyCombatHud) {
    $proof.partyCombatHud=[ordered]@{renderedEditorClients=$true;productionAdmission=$true;
        snapshotInjection=$false;healthChangesInjected=[bool]$ObservePartyCombatVitals;directHealthAttributeWrites=$false;
        naturalCombatVerified=$false;hardwareInputVerified=$false;
        observations=@($script:PartyHudEvidence.ToArray())}
    if ($ObservePartyCombatVitals) { $proof.partyCombatHud.vitals=$partyVitalsEvidence }
}
$proof.backendModuleSHA256=$backendModuleHash
$proof.sourceAndRunnerHashes=$hashes
$proof.closedLogHashes=@(@($logs)+@($backendOut,$backendErr) | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} })
$proofPath=Join-Path $runRoot $(if ($VerifyGameplayAbandonmentResponseLoss) {'DungeonGameplayAbandonmentRecovery.proof.json'} elseif ($VerifyGameplayAbandonment) {'DungeonGameplayAbandonment.proof.json'} elseif ($VerifyReconnectWithinGrace) {'DungeonReconnectWithinGrace.proof.json'} elseif ($VerifyUnexpectedDisconnect) {'DungeonUnexpectedDisconnect.proof.json'} elseif ($VerifyDungeonClientInitialAbandonment) {'DungeonClientInitialAbandonment.proof.json'} elseif ($VerifyAbandonedClientReceipt) {'DungeonAbandonedClientReceipt.proof.json'} elseif ($VerifyAbandonedClientLobbyReturn) {'DungeonAbandonedClientLobbyReturn.proof.json'} elseif ($VerifyAbandonmentReauthentication) {'DungeonAbandonmentReauthentication.proof.json'} elseif ($VerifyAbandonmentClientRetry) {'DungeonAbandonmentClientRetry.proof.json'} elseif ($VerifyAbandonmentResponseLoss) {'DungeonAbandonmentResponseLoss.proof.json'} elseif ($VerifyRemainingPartySettlement) {'DungeonAbandonmentSettlement.proof.json'} else {'DungeonMembershipBackend.proof.json'})
$proof | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $proofPath -Encoding UTF8
Write-Host "PASS: PostgreSQL/HTTP membership; unexpected-disconnect=$([bool]$VerifyUnexpectedDisconnect) remaining-party settlement=$([bool]$VerifyRemainingPartySettlement) response-loss=$([bool]$VerifyAbandonmentResponseLoss) live-lobby-retry=$([bool]$VerifyAbandonmentClientRetry) reauthentication=$([bool]$VerifyAbandonmentReauthentication) same-process-return=$([bool]$VerifyAbandonedClientLobbyReturn) live-receipt=$([bool]$VerifyAbandonedClientReceipt) dungeon-client-first-post=$([bool]$VerifyDungeonClientInitialAbandonment) ($ClientCount clients): $proofPath"
