[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024, 65535)][int]$Port = 18827,
    [ValidateRange(1, 4)][int]$ClientCount = 2,
    [ValidateRange(60, 600)][int]$TimeoutSeconds = 300,
    [string]$BackendUrl = 'http://127.0.0.1:3027',
    [string]$ConnectionString = 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only',
    [switch]$SkipPostgresStart,
    [switch]$NoBackendRestore,
    [switch]$LobbyUI,
    [switch]$LobbyDifficultyOnly,
    [switch]$VerifyRegisteredDungeonDefinition,
    [switch]$VerifyServerDungeonContent,
    [ValidateSet('Easy', 'Normal', 'Hard', 'Hell')][string]$LobbyDifficulty = 'Hard',
    [switch]$CreateCharactersInUI,
    [switch]$ReauthenticateInLobby,
    [switch]$ReauthenticateAfterAdmission,
    [switch]$NaturalPlayerAttack,
    [switch]$LiveBossAI,
    [switch]$VerifyResultAbandonment,
    [switch]$VerifyResultAbandonmentResponseLoss,
    [switch]$VerifyResultAbandonmentReauthentication,
    [switch]$SlatePointerInput,
    [switch]$ConfirmationNavigation,
    [switch]$InventoryStackMerge,
    [switch]$InventoryStackSplit,
    [switch]$InventorySplitQuantity,
    [switch]$InventoryMoveSwap,
    [switch]$InventoryConcurrency,
    [switch]$InventoryRecovery,
    [switch]$InventoryRecoveryRetryFailure,
    [switch]$InventoryRecoveryCredentialExpiry,
    [switch]$InventoryRecoveryRevocation,
    [switch]$InventoryRecoveryRestart,
    [ValidateSet('Move', 'Swap', 'Merge', 'Split', 'ClaimMail', 'Equip', 'Unequip', 'Consume')][string]$InventoryRecoveryOperation = 'Move',
    [switch]$InventoryRecoveryConsumeQuickSlot,
    [switch]$InventoryRecoveryConsumeDeath
)

# Development PostgreSQL + authored bossmap. LiveBossAI adds authored boss BT/GAS
# after the authenticated party is ready; Steam transport remains out of scope.
$ErrorActionPreference = 'Stop'
$InventoryRecoveryOperation = switch ($InventoryRecoveryOperation) { 'Swap' {'Swap'} 'Merge' {'Merge'} 'Split' {'Split'} 'ClaimMail' {'ClaimMail'} 'Equip' {'Equip'} 'Unequip' {'Unequip'} 'Consume' {'Consume'} default {'Move'} }
if ($InventoryRecoveryConsumeDeath -and (-not $InventoryRecovery -or $InventoryRecoveryOperation -cne 'Consume' -or $ClientCount -ne 1 -or
    $InventoryRecoveryConsumeQuickSlot -or $InventoryRecoveryRestart -or $InventoryRecoveryRevocation -or $InventoryRecoveryRetryFailure -or
    $InventoryRecoveryCredentialExpiry -or $LobbyUI -or $NaturalPlayerAttack -or $LiveBossAI -or $VerifyResultAbandonment -or
    $VerifyRegisteredDungeonDefinition -or $VerifyServerDungeonContent)) {
    throw 'Consume death recovery requires one current-session bag Consume, without other gameplay or recovery modes.'
}
if ($InventoryRecoveryConsumeDeath -and $NoBackendRestore) {
    throw 'Consume death recovery uses fresh private backend inputs and requires package restore.'
}
if ($InventoryRecoveryOperation -cnotin @('Move','ClaimMail','Consume') -and (-not $InventoryRecovery -or -not $InventoryRecoveryRestart)) {
    throw "$InventoryRecoveryOperation recovery requires InventoryRecovery and InventoryRecoveryRestart."
}
if ($InventoryRecoveryOperation -ceq 'Consume' -and (-not $InventoryRecovery -or $ClientCount -ne 1 -or $InventoryRecoveryRestart -or
    $InventoryRecoveryRevocation -or $InventoryRecoveryCredentialExpiry -or $InventoryRecoveryRetryFailure)) {
    throw 'Consume recovery requires InventoryRecovery, one client and no restart or other recovery failure mode.'
}
if ($InventoryRecoveryConsumeQuickSlot -and $InventoryRecoveryOperation -cne 'Consume') { throw 'Consume quickslot input requires Consume recovery.' }
if ($InventoryRecoveryOperation -ceq 'ClaimMail' -and (-not $InventoryRecovery -or $ClientCount -ne 1 -or
    $InventoryRecoveryRevocation -or $InventoryRecoveryCredentialExpiry -or $InventoryRecoveryRetryFailure)) {
    throw 'ClaimMail recovery requires InventoryRecovery, one client and no other recovery failure mode.'
}
if ($InventoryRecoveryRestart -and (-not $InventoryRecovery -or $ClientCount -ne 1 -or
    $InventoryRecoveryRevocation -or $InventoryRecoveryCredentialExpiry -or $InventoryRecoveryRetryFailure)) {
    throw 'Inventory recovery restart requires InventoryRecovery, one client and no other recovery failure mode.'
}
$inventoryRecoveryIsSwap = $InventoryRecoveryOperation -ceq 'Swap'
$inventoryRecoveryIsMerge = $InventoryRecoveryOperation -ceq 'Merge'
$inventoryRecoveryIsSplit = $InventoryRecoveryOperation -ceq 'Split'
$inventoryRecoveryIsClaimMail = $InventoryRecoveryOperation -ceq 'ClaimMail'
$inventoryRecoveryIsEquip = $InventoryRecoveryOperation -ceq 'Equip'
$inventoryRecoveryIsUnequip = $InventoryRecoveryOperation -ceq 'Unequip'
$inventoryRecoveryIsEquipment = $inventoryRecoveryIsEquip -or $inventoryRecoveryIsUnequip
$inventoryRecoveryIsConsume = $InventoryRecoveryOperation -ceq 'Consume'
$inventoryRecoveryExpectedOperation = if($inventoryRecoveryIsMerge){'Item.StackTransfer'}else{'Item.' + $InventoryRecoveryOperation}
$inventoryRecoveryExpectedCounts = if($inventoryRecoveryIsMerge -or $inventoryRecoveryIsSplit -or $inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsEquipment -or $inventoryRecoveryIsConsume){'0|0|1'}elseif($inventoryRecoveryIsSwap){'0|1|1'}else{'1|0|1'}
if ($InventoryRecoveryRevocation -and (-not $InventoryRecovery -or $ClientCount -ne 2 -or
    $InventoryRecoveryCredentialExpiry -or $InventoryRecoveryRetryFailure)) {
    throw 'Inventory recovery revocation requires InventoryRecovery, exactly two clients and no other recovery failure mode.'
}
if ($InventoryRecoveryCredentialExpiry -and (-not $InventoryRecovery -or $InventoryRecoveryRetryFailure)) {
    throw 'Inventory credential expiry requires InventoryRecovery and excludes synthetic retry failure.'
}
if ($InventoryRecoveryRetryFailure -and -not $InventoryRecovery) {
    throw 'Inventory recovery retry failure requires InventoryRecovery.'
}
if ($VerifyServerDungeonContent -and (-not $LobbyUI -or $ClientCount -ne 2 -or $LobbyDifficultyOnly -or
    $CreateCharactersInUI -or $ReauthenticateInLobby -or $ReauthenticateAfterAdmission -or
    $NaturalPlayerAttack -or $LiveBossAI -or $VerifyResultAbandonment)) {
    throw 'Server content verification requires the standard two-client LobbyUI full flow only.'
}
if ($VerifyRegisteredDungeonDefinition -and (-not $LobbyUI -or -not $LobbyDifficultyOnly)) {
    throw 'Registered dungeon definition verification requires LobbyUI and LobbyDifficultyOnly.'
}
$splitFixtureMode = $InventoryStackSplit -or $InventorySplitQuantity
$inventoryFixtureMode = $InventoryStackMerge -or $splitFixtureMode -or $InventoryMoveSwap -or $InventoryRecovery
if ($InventoryConcurrency -and ($ClientCount -ne 2 -or $inventoryFixtureMode -or $LobbyUI -or $LobbyDifficultyOnly -or
    $VerifyRegisteredDungeonDefinition -or $VerifyServerDungeonContent -or $CreateCharactersInUI -or
    $ReauthenticateInLobby -or $ReauthenticateAfterAdmission -or $NaturalPlayerAttack -or $LiveBossAI -or
    $VerifyResultAbandonment -or $VerifyResultAbandonmentResponseLoss -or $VerifyResultAbandonmentReauthentication -or
    $SlatePointerInput -or $ConfirmationNavigation)) {
    throw 'Inventory concurrency requires exactly two standalone headless clients and excludes other fixture modes.'
}
if ($InventoryConcurrency -and $ConnectionString -cne 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only') {
    throw 'Inventory concurrency requires the default local development PostgreSQL container.'
}
if (@(@($InventoryStackMerge, $InventoryStackSplit, $InventorySplitQuantity, $InventoryMoveSwap, $InventoryRecovery) | Where-Object { $_ }).Count -gt 1) { throw 'Select only one inventory stack fixture.' }
if ($inventoryFixtureMode -and ($ClientCount -ne $(if($InventoryRecoveryRevocation){2}else{1}) -or $LobbyUI -or $LobbyDifficultyOnly -or $VerifyRegisteredDungeonDefinition -or $VerifyServerDungeonContent -or
    $CreateCharactersInUI -or $ReauthenticateInLobby -or $ReauthenticateAfterAdmission -or $NaturalPlayerAttack -or
    $LiveBossAI -or $VerifyResultAbandonment -or $VerifyResultAbandonmentResponseLoss -or
    $VerifyResultAbandonmentReauthentication -or $SlatePointerInput -or $ConfirmationNavigation)) {
    throw 'Inventory modes require their standalone client count and exclude lobby/combat/result fixtures.'
}
if ($inventoryFixtureMode -and $ConnectionString -cne 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only') {
    throw 'Inventory stack durable assertions require the default local development PostgreSQL container.'
}
if ($ConfirmationNavigation -and -not $SlatePointerInput) {
    throw 'Confirmation navigation requires the rendered Slate pointer fixture.'
}
if ($SlatePointerInput -and -not $VerifyResultAbandonment) {
    throw 'Slate pointer input requires the rendered two-client result-abandonment fixture.'
}
if ($VerifyResultAbandonmentResponseLoss -and -not $VerifyResultAbandonment) {
    throw 'Result abandonment response loss requires VerifyResultAbandonment.'
}
if ($VerifyResultAbandonmentReauthentication -and -not $VerifyResultAbandonmentResponseLoss) {
    throw 'Result abandonment reauthentication requires the response-loss mode.'
}
if ($VerifyResultAbandonment -and ($ClientCount -ne 2 -or $LobbyUI -or $LobbyDifficultyOnly -or
    $CreateCharactersInUI -or $ReauthenticateInLobby -or $ReauthenticateAfterAdmission -or $NaturalPlayerAttack -or $LiveBossAI)) {
    throw 'VerifyResultAbandonment requires exactly two non-lobby-UI clients and excludes other fixture modes.'
}
if ($VerifyResultAbandonment -and $ConnectionString -cne 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only') {
    throw 'Result abandonment durable assertions require the default local development PostgreSQL container.'
}
if ($LobbyDifficulty -cnotin @('Easy', 'Normal', 'Hard', 'Hell')) { throw 'LobbyDifficulty must use an exact backend difficulty name.' }
if ($LobbyDifficultyOnly -and (-not $LobbyUI -or $ReauthenticateInLobby -or $ReauthenticateAfterAdmission -or $NaturalPlayerAttack -or $LiveBossAI)) {
    throw 'LobbyDifficultyOnly requires LobbyUI and excludes combat/reauthentication modes.'
}
# Expectations mirror the saved reward definitions; never inject them into gameplay.
$expectedRewards = @{
    Easy = @{ Gold = 100; Potions = 1; Helms = 0; Version = 'pve_easy_v1'; AbilityLevel = 1 }
    Normal = @{ Gold = 250; Potions = 2; Helms = 0; Version = 'pve_normal_v1'; AbilityLevel = 2 }
    Hard = @{ Gold = 500; Potions = 3; Helms = 1; Version = 'pve_hard_v1'; AbilityLevel = 3 }
    Hell = @{ Gold = 1000; Potions = 5; Helms = 1; Version = 'pve_hell_v1'; AbilityLevel = 4 }
}[$LobbyDifficulty]
$expectedInventoryQuantity = $expectedRewards.Potions + $expectedRewards.Helms
$expectedItemRecords = 4 + [int]($expectedRewards.Helms -gt 0) # potion + three newly created starters + optional helm
if ($LiveBossAI -and -not $NaturalPlayerAttack) { throw 'LiveBossAI requires NaturalPlayerAttack.' }
if ($CreateCharactersInUI -and -not $LobbyUI) { throw 'CreateCharactersInUI requires LobbyUI.' }
if ($ReauthenticateInLobby -and -not $LobbyUI) { throw 'ReauthenticateInLobby requires LobbyUI.' }
if ($ReauthenticateAfterAdmission -and -not $LobbyUI) { throw 'ReauthenticateAfterAdmission requires LobbyUI.' }
if (($ReauthenticateInLobby -or $ReauthenticateAfterAdmission) -and $ConnectionString -cne 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only') {
    throw 'Authentication expiry fixture is restricted to the default local development PostgreSQL container.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$backendProject = Join-Path $projectRoot 'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj'
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
$baseUrl = $BackendUrl.TrimEnd('/')
$backendUri = [Uri]$baseUrl
if ($backendUri.Scheme -ne 'http' -or $backendUri.Host -ne '127.0.0.1' -or $backendUri.AbsolutePath -ne '/') {
    throw 'Use a loopback-only http://127.0.0.1:<port> backend.'
}
if ($backendUri.Port -eq $Port) { throw 'Backend and UE ports must differ.' }
$listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
if (@($listeners.GetActiveTcpListeners() | Where-Object { $_.Port -eq $backendUri.Port }).Count -or
    @($listeners.GetActiveUdpListeners() | Where-Object { $_.Port -eq $Port }).Count) { throw 'A requested port is already in use.' }
foreach ($path in @($editor, $project, $backendProject, (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'))) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing prerequisite: $path" }
}
if ($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge -or $inventoryRecoveryIsSplit -or $inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsEquipment -or $inventoryRecoveryIsConsume) {
    # A compile-only object is not a linked runnable probe. Refuse before fixture records/processes are created.
    $recoveryProbeStrings=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes((Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-Project_RPG.dll')))
    $recoveryPassMarker=if($inventoryRecoveryIsClaimMail){
        if($InventoryRecoveryRestart){'RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_RESTART_PASS'}else{'RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_PASS'}
    }elseif($inventoryRecoveryIsConsume){if($InventoryRecoveryConsumeDeath){'RPG_INVENTORY_RECOVERY_UI CONSUME_DEATH_PASS'}else{'RPG_INVENTORY_RECOVERY_UI CONSUME_PASS'}}else{"RPG_INVENTORY_RECOVERY_UI $($InventoryRecoveryOperation.ToUpperInvariant())_RESTART_PASS"}
    if(-not $recoveryProbeStrings.Contains("RPGInventoryRecovery${InventoryRecoveryOperation}E2E") -or
        -not $recoveryProbeStrings.Contains($recoveryPassMarker) -or
        ($inventoryRecoveryIsConsume -and -not $recoveryProbeStrings.Contains('RPG_CONSUME_RECOVERY_SERVER ARMED')) -or
        ($InventoryRecoveryConsumeDeath -and (-not $recoveryProbeStrings.Contains('RPGInventoryRecoveryConsumeDeathE2E') -or
            -not $recoveryProbeStrings.Contains('RPG_CONSUME_RECOVERY_SERVER DEATH_RESOLVED')))) {
        throw "Build Project_RPGEditor with the $($InventoryRecoveryOperation.ToLowerInvariant()) recovery probe before running $InventoryRecoveryOperation recovery verification."
    }
    $recoveryProbeStrings=$null
}
# Some launchers duplicate PATH's casing; Windows Start-Process rejects that environment.
$processEnvironment = [Environment]::GetEnvironmentVariables('Process')
$pathKeys = @($processEnvironment.Keys | Where-Object { $_.ToString() -ieq 'Path' })
if ($pathKeys.Count -gt 1) {
    $pathValue = [string]$processEnvironment[$pathKeys[0]]
    foreach ($key in $pathKeys) { [Environment]::SetEnvironmentVariable($key.ToString(), $null, 'Process') }
    [Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
}
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $projectRoot ("Saved\Tests\DungeonBossReturn\$runId")
# Journals add backend/server hashes, session GUIDs and record GUIDs below
# UserDir. Keep the per-process prefix short enough for Win32 .json.tmp writes.
$runtimeRoot = Join-Path $projectRoot ("Saved\Tests\DBR\$($runId.Substring(0, 12))")
$inventoryJournalDirectory = Join-Path $runtimeRoot 'j'
$inventoryRestartRequest = $null
if (Test-Path -LiteralPath $runtimeRoot) { throw 'Runtime fixture directory already exists; choose a fresh run.' }
New-Item -ItemType Directory -Path $runtimeRoot | Out-Null
$buildRoot = Join-Path $runRoot 'backend-build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$backendOutputRoot = if ($InventoryRecoveryConsumeDeath) { Join-Path $buildRoot 'runtime' } else { $buildRoot }
$backendAssemblyPath = Join-Path $backendOutputRoot 'ProjectRpg.Backend.dll'
$gate = Join-Path $runRoot 'party-admitted.gate'
$naturalAttackGate = Join-Path $runRoot 'natural-attack-ready.gate'
$resultRetryGate = Join-Path $runRoot 'result-abandonment-retry.gate'
$resultSettleGate = Join-Path $runRoot 'result-abandonment-settle.gate'
$resultPostRetryGate = Join-Path $runRoot 'result-abandonment-post-retry.gate'
$resultRenewedPostGate = Join-Path $runRoot 'result-abandonment-renewed-post.gate'
$sharedSessionFile = Join-Path $runRoot 'lobby-session.txt'
$serverReadyFile = Join-Path $runRoot 'server-ready.gate'
$authExpiredFile = Join-Path $runRoot 'auth-expired.gate'
$dungeonAuthExpiredFile = Join-Path $runRoot 'dungeon-auth-expired.gate'
$adminToken = "dungeon-flow-e2e-$runId"
$adminHeaders = @{ Authorization = "Bearer $adminToken" }
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$logs = [Collections.Generic.List[string]]::new()
$environmentNames = @('ASPNETCORE_ENVIRONMENT', 'PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'POSTGRES_CONNECTION_STRING',
    'Kestrel__Endpoints__Http__Url', 'PROJECT_RPG_GAME_SERVER_ID', 'PROJECT_RPG_DUNGEON_SESSION_ID',
    'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN', 'UE_SKIP_UBT_SDK_SETUP',
    'PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN', 'PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT', 'PROJECT_RPG_ABANDON_PROXY_TRANSPARENT')
$previousEnvironment = @{}
foreach ($name in $environmentNames) { $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$runtimeFailurePattern = 'DUNGEON_FLOW_E2E_FAIL|ABANDON_PROXY_FAIL|INVENTORY_RECOVERY_PROXY_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*):\s*Error:|RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED'
$sessionId = $null
$succeeded = $false
$unrealModulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$unrealModuleHash = (Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash
$proxyUrl = $null
$controlUrl = $null
$controlHeaders = $null
$resultSourcePaths = @(
    'Config/DefaultEngine.ini',
    'Source/Project_RPG/Private/GameState/RPGGameStateBase.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowPointerInput.cpp',
    'Source/Project_RPG/Private/UI/RPGConfirmationInput.h',
    'Source/Project_RPG/Private/UI/RPGMenuFocus.h',
    'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyWidget.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
    'Scripts/Network/Run-DungeonBossReturnE2ETest.ps1',
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.h',
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonResultAbandonmentE2E.cpp',
    'Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h',
    'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
    'Source/Project_RPG/Public/Manager/HttpWebManager.h',
    'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
    'Source/Project_RPG/Private/Online/RPGDungeonDisconnectionJournal.cpp',
    'Source/Project_RPG/Private/Online/RPGDungeonConsumeJournal.cpp',
    'Source/Project_RPG/Public/UI/Content/RPGDungeonResultActionsWidget.h',
    'Source/Project_RPG/Private/UI/Content/RPGDungeonResultActionsWidget.cpp',
    'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp',
    'Source/Project_RPG/Private/GameMode/RPGBossBattleGameMode.cpp')
if ($VerifyResultAbandonmentResponseLoss) { $resultSourcePaths += 'Scripts/Network/Fixtures/DungeonAbandonmentHttpFaultProxy.mjs' }
if ($inventoryFixtureMode -or $InventoryConcurrency) {
    $resultSourcePaths += @(
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Policy/RPGItemStackPolicy.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Policy/RPGItemStackPolicy.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemActionCommandPlanner.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemAsyncCommandOrchestrator.cpp',
        'Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp',
        'Source/Project_RPG/Public/Component/RPGItemCommandComponent.h',
        'Source/Project_RPG/Private/Component/RPGInventoryProjectionComponent.cpp',
        'Source/Project_RPG/Private/Component/UI/QuickSlotComponent.cpp',
        'Source/Project_RPG/Private/UI/MVVM/RPGInventoryProjectionViewModel.cpp',
        'Source/Project_RPG/Public/UI/MVVM/RPGInventoryProjectionViewModel.h',
        'Source/Project_RPG/Private/UI/Inventory/Projection/RPGInventoryProjectionWidget.cpp',
        'Source/Project_RPG/Private/UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.cpp')
    if ($InventoryStackMerge) {
        . (Join-Path $PSScriptRoot 'Fixtures\InventoryStackMerge.ps1')
        $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryStackMerge.ps1',
            'Source/Project_RPG/Private/Tests/RPGInventoryStackMergeE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryStackMergeE2E.cpp')
    } elseif ($InventoryRecovery) {
        . (Join-Path $PSScriptRoot 'Fixtures\InventoryMoveSwap.ps1')
        . (Join-Path $PSScriptRoot 'Fixtures\InventoryRecovery.ps1')
        if ($InventoryRecoveryConsumeDeath) {
            . (Join-Path $PSScriptRoot 'Fixtures\InventoryConsumeBackendSnapshot.ps1')
            $resultSourcePaths += 'Scripts/Network/Fixtures/InventoryConsumeBackendSnapshot.ps1'
        }
        if ($InventoryRecoveryRestart -or $inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsConsume) { . (Join-Path $PSScriptRoot 'Fixtures\InventoryRecoveryRestart.ps1') }
        if ($InventoryRecoveryCredentialExpiry) { . (Join-Path $PSScriptRoot 'Fixtures\InventoryRecoveryExpiry.ps1') }
        if ($InventoryRecoveryRevocation) { . (Join-Path $PSScriptRoot 'Fixtures\InventoryRecoveryRevocation.ps1') }
        $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryMoveSwap.ps1', 'Scripts/Network/Fixtures/InventoryRecovery.ps1',
            'Scripts/Network/Fixtures/InventoryRecoveryHttpProxy.mjs', 'Scripts/Network/Fixtures/InventoryRecoveryHttpProxy.test.mjs',
            'Source/Project_RPG/Private/Tests/RPGInventoryRecoveryE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryRecoveryE2E.cpp',
            'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.cpp',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemAsyncCommandOrchestrator.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendTypes.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendGateway.cpp',
            'Source/Project_RPG/Public/UI/Inventory/Projection/RPGInventoryProjectionWidget.h',
            'Content/Blueprints/GameData/Item/V2/DA_Item_Potion_Red_Large.uasset',
            'Content/Blueprints/GameData/Item/V2/DA_Item_Helm_Leather.uasset')
        if ($InventoryRecoveryRestart) { $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryRecoveryRestart.ps1',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGInventoryCommandJournal.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGInventoryCommandJournal.cpp',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendSubsystem.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendSubsystem.cpp') }
        if ($inventoryRecoveryIsSplit) { $resultSourcePaths += 'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemActionCommandPlanner.h' }
        if ($inventoryRecoveryIsEquipment) { $resultSourcePaths += @(
            'Source/Project_RPG/Public/Component/Equipment/RPGAuthoritativeEquipmentComponent.h',
            'Source/Project_RPG/Private/Component/Equipment/RPGAuthoritativeEquipmentComponent.cpp',
            'Source/Project_RPG/Public/Item/Equipment/RPGEquipmentSnapshot.h',
            'Source/Project_RPG/Private/Item/Equipment/RPGEquipmentSnapshot.cpp',
            'Content/Blueprints/UI/Equipment/Projection/WBP_EquipmentProjection.uasset') }
        if ($inventoryRecoveryIsConsume) { $resultSourcePaths += @(
            'Scripts/Network/Fixtures/InventoryRecoveryRestart.ps1',
            'Source/Project_RPG/Private/Tests/RPGConsumeRecoveryE2E.h',
            'Source/Project_RPG/Private/Tests/RPGConsumeRecoveryE2E.cpp',
            'Source/Project_RPG/Public/Component/RPGAbilitySystemComponent.h',
            'Source/Project_RPG/Private/Component/RPGAbilitySystemComponent.cpp',
            'Source/Project_RPG/Public/Attribute/RPGAttributeSet.h',
            'Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp',
            'Source/Project_RPG/Public/UI/RPGQuickSlotWidget.h',
            'Source/Project_RPG/Private/UI/RPGQuickSlotWidget.cpp',
            'Source/Project_RPG/Public/Component/UI/QuickSlotComponent.h',
            'Source/Project_RPG/Public/Controller/RPGPlayerController.h',
            'Source/Project_RPG/Public/Item/Definition/RPGItemDefinitionFragments.h') }
        if ($inventoryRecoveryIsClaimMail) {
            $resultSourcePaths += @(
            'Source/Project_RPG/Public/UI/Inventory/Projection/RPGRewardMailWidget.h',
            'Source/Project_RPG/Private/UI/Inventory/Projection/RPGRewardMailWidget.cpp')
            if (-not $InventoryRecoveryRestart) { $resultSourcePaths += @(
                'Scripts/Network/Fixtures/InventoryRecoveryRestart.ps1',
                'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGInventoryCommandJournal.h',
                'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGInventoryCommandJournal.cpp',
                'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendSubsystem.h',
                'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendSubsystem.cpp') }
        }
        if ($InventoryRecoveryCredentialExpiry) { $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryRecoveryExpiry.ps1',
            'Backend/ProjectRpg.Backend/Authentication/BearerAuthenticator.cs',
            'Backend/ProjectRpg.Backend/Authentication/ApiAuthenticationMiddleware.cs',
            'Backend/ProjectRpg.Backend/Authentication/AccessTokenService.cs',
            'Backend/Database/schema.sql',
            'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.cs') }
        if ($InventoryRecoveryRevocation) { $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryRecoveryRevocation.ps1',
            'Source/Project_RPG/Private/Tests/RPGDungeonEjectionNetworkProbe.cpp',
            'Source/Project_RPG/Private/Tests/RPGDungeonEjectionNetworkProbe.h',
            'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp', 'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
            'Source/Project_RPG/Private/Player/RPGPlayerState.cpp', 'Source/Project_RPG/Public/Player/RPGPlayerState.h',
            'Source/Project_RPG/Private/Online/RPGDungeonMembershipSnapshot.cpp',
            'Source/Project_RPG/Public/Online/RPGDungeonMembershipSnapshot.h',
            'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.cs', 'Backend/ProjectRpg.Backend/Contracts/ApiContracts.cs') }
    } elseif ($InventoryMoveSwap) {
        . (Join-Path $PSScriptRoot 'Fixtures\InventoryMoveSwap.ps1')
        $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryMoveSwap.ps1',
            'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.cpp',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemActionCommandPlanner.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemTransactionTypes.h',
            'Source/Project_RPG/Public/UI/Inventory/Projection/RPGItemIdDragDropOperation.h',
            'Source/Project_RPG/Private/UI/RPGQuickSlotWidget.cpp',
            'Content/Blueprints/GameData/Item/V2/DA_Item_Potion_Red_Large.uasset',
            'Content/Blueprints/GameData/Item/V2/DA_Item_Helm_Leather.uasset')
    } elseif ($InventoryConcurrency) {
        . (Join-Path $PSScriptRoot 'Fixtures\InventoryConcurrency.ps1')
        $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryConcurrency.ps1',
            'Source/Project_RPG/Private/Tests/RPGInventoryConcurrencyE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryConcurrencyE2E.cpp',
            'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.cpp',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemActionCommandPlanner.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemTransactionTypes.h',
            'Content/Blueprints/GameData/Item/V2/DA_Item_Potion_Red_Large.uasset',
            'Content/Blueprints/GameData/Item/V2/DA_Item_Helm_Leather.uasset')
    } else {
        . (Join-Path $PSScriptRoot 'Fixtures\InventoryStackSplit.ps1')
        $resultSourcePaths += @('Scripts/Network/Fixtures/InventoryStackSplit.ps1',
            'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.h', 'Source/Project_RPG/Private/Tests/RPGInventoryStackSplitE2E.cpp',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemActionCommandPlanner.h',
            'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemTransactionTypes.h',
            'Source/Project_RPG/Public/UI/Inventory/Projection/RPGItemIdDragDropOperation.h',
            'Source/Project_RPG/Private/UI/RPGQuickSlotWidget.cpp')
    }
    # Resolve the actual authored asset rather than assuming a folder layout.
    if ($InventorySplitQuantity -or $InventoryMoveSwap) {
        $resultSourcePaths += 'Source/Project_RPG/Public/UI/Inventory/Projection/RPGInventoryProjectionWidget.h'
    }
    if ($inventoryFixtureMode) {
        $inventoryAssets = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Content') -Recurse -Filter 'WBP_InventoryProjection.uasset')
        if ($inventoryAssets.Count -ne 1) { throw 'Expected one authored inventory WBP.' }
        $resultSourcePaths += $inventoryAssets[0].FullName.Substring($projectRoot.Length + 1)
    }
}
$resultSourceHashes = @{}
$automaticTransportReplayObserved = $false
if ($VerifyRegisteredDungeonDefinition) {
    $resultSourcePaths+=@('Config/DefaultGame.ini',
        'Content/System/Dungeons/DA_Dungeon_RaidSwordMan.uasset',
        'Source/Project_RPG/Public/Online/RPGDungeonDefinition.h',
        'Source/Project_RPG/Private/Online/RPGDungeonDefinition.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyDungeonSelection.cpp',
        'Scripts/Editor/CreateDungeonDefinitionAssets.py')
}
if ($VerifyServerDungeonContent) {
    $resultSourcePaths+=@('Config/DefaultGame.ini',
        'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
        'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
        'Source/Project_RPG/Public/Online/RPGDungeonServerDefinition.h',
        'Source/Project_RPG/Private/Online/RPGDungeonServerDefinition.cpp',
        'Content/System/Dungeons/DA_DungeonServer_RaidSwordMan.uasset',
        'Content/Blueprints/GameMode/BP_BossBattleGameMode.uasset',
        'Source/Project_RPG/Public/Online/RPGDungeonDefinition.h',
        'Source/Project_RPG/Private/Online/RPGDungeonDefinition.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyDungeonSelection.cpp',
        'Content/System/Dungeons/DA_Dungeon_RaidSwordMan.uasset',
        'Scripts/Editor/CreateDungeonServerDefinitionAssets.py')
}
if ($VerifyResultAbandonment -or $inventoryFixtureMode -or $InventoryConcurrency -or $VerifyRegisteredDungeonDefinition -or $VerifyServerDungeonContent) {
    foreach ($path in $resultSourcePaths) {
        $resultSourceHashes[$path] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $path) -Algorithm SHA256).Hash
    }
    $resultSourceHashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'source-before.json') -Encoding UTF8
}

function Invoke-Api {
    param([string]$Path, [string]$Method = 'Get', [hashtable]$Headers = @{}, [object]$Body = $null)
    $parameters = @{ Uri = "$baseUrl/api/$Path"; Method = $Method; Headers = $Headers; TimeoutSec = 10; UseBasicParsing = $true }
    if ($null -ne $Body) { $parameters.ContentType = 'application/json'; $parameters.Body = $Body | ConvertTo-Json -Depth 20 -Compress }
    try { $response = Invoke-WebRequest @parameters }
    catch {
        # Never surface the request body or bearer token in a thrown diagnostic.
        throw "HTTP $Method $Path failed: $($_.Exception.Message)"
    }
    if (-not [string]::IsNullOrWhiteSpace($response.Content)) { return $response.Content | ConvertFrom-Json }
}

function Assert-DungeonFirstSliceStarterEquipment {
    param([object[]]$Items, [string]$CharacterId)
    # This runner creates fresh characters. Legacy records remain unchanged, but
    # must not be accepted as substitutes for the new three-item creation recipe.
    $expected = [Collections.Generic.Dictionary[string,int]]::new([StringComparer]::Ordinal)
    $expected.Add('GameItem.Equipment.Progression.Starter.GreatSword', 9)
    $expected.Add('GameItem.Equipment.Progression.Starter.Chest', 1)
    $expected.Add('GameItem.Equipment.Progression.Starter.Gloves', 4)
    $equipped = @($Items | Where-Object { $_.location.containerType -ceq 'Equipment' })
    if ($equipped.Count -ne 3) { throw 'New-character starter equipment requires exactly three equipped records.' }
    $identities = [Collections.Generic.HashSet[Guid]]::new()
    $slots = [Collections.Generic.HashSet[int]]::new()
    foreach ($item in $equipped) {
        $growth = $item.state.equipmentProgression
        $identity = [Guid]::Empty
        if (-not $expected.ContainsKey([string]$item.definitionName) -or
            $item.definitionType -cne 'RPGItemDefinition' -or $item.definitionVersion -is [bool] -or $item.definitionVersion -ne 1 -or
            $item.state.quantity -is [bool] -or $item.state.quantity -ne 1 -or [string]$item.revision -cne '1' -or
            $item.owner.type -cne 'Character' -or $item.owner.ownerId -cne $CharacterId -or $item.location.containerId -cne $CharacterId -or
            $item.location.slotIndex -is [bool] -or $item.location.slotIndex -ne $expected[[string]$item.definitionName] -or
            $item.lifecycleState -cne 'Active' -or $item.metadata.bindState -cne 'CharacterBound' -or
            $item.metadata.creationSource -cne 'CharacterCreation' -or $item.metadata.isLocked -isnot [bool] -or $item.metadata.isLocked -or $null -ne $item.metadata.expiresAtUtc -or
            $null -eq $growth -or $growth.growthBandId -cne 'FirstSlice' -or $growth.ruleVersion -is [bool] -or $growth.ruleVersion -ne 1 -or
            $growth.level -is [bool] -or $growth.level -ne 0 -or
            -not [Guid]::TryParse([string]$item.state.instanceId, [ref]$identity) -or $identity -eq [Guid]::Empty -or
            -not $identities.Add($identity) -or -not $slots.Add([int]$item.location.slotIndex)) {
            throw 'New-character starter identity, ownership, slot, metadata or explicit +0 state is invalid.'
        }
    }
}

function Assert-Healthy {
    foreach ($process in $processes) {
        $process.Refresh()
        if ($process.HasExited) { throw "Owned process $($process.Id) exited ($($process.ExitCode)); inspect $runRoot" }
    }
    foreach ($log in $logs) {
        if (-not (Test-Path -LiteralPath $log) -or -not (Select-String -LiteralPath $log -Pattern $runtimeFailurePattern -Quiet)) { continue }
        foreach ($failure in @(Select-String -LiteralPath $log -Pattern $runtimeFailurePattern)) {
            if ($InventoryRecoveryRevocation -and $script:inventoryRevocationReceiptVerified -and
                (Test-InventoryRevocationExpectedNetworkClose $log $failure.Line)) { continue }
            # The server deliberately closes the control channel after a completed
            # return request. UE still labels that client-side event ConnectionLost.
            # The client can log ConnectionLost before the server flushes its planned
            # close. Defer server confirmation until all clients finish local travel.
            if ($log -in @($clientLogs) -and $failure.Line -match 'LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost, ErrorString = Your connection to the host has been lost\., Driver = Name:GameNetDriver' -and
                $serverLog) {
                $clicked = Select-String -LiteralPath $log -Pattern 'DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1|DUNGEON_RESULT_ABANDON QUEUED LiveResultConfirmation=1' | Select-Object -Last 1
                if ($clicked -and $clicked.LineNumber -lt $failure.LineNumber) { continue }
            }
            throw "Runtime failure; inspect $log at line $($failure.LineNumber)"
        }
    }
}

function Wait-Marker {
    param([string]$Log, [string]$Marker)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        if ((Test-Path -LiteralPath $Log) -and (Select-String -LiteralPath $Log -SimpleMatch $Marker -Quiet)) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Missing '$Marker'; inspect $Log"
}

function Expire-FixtureAuthentication {
    param([object[]]$Members, [string]$WindowGate)
    # Only this run's generated accounts; no expiry rule changes or service-credential writes.
    $fixtureIds = @($Members | ForEach-Object {
        if ($_.SteamId -cnotmatch '^[0-9]{17}$') { throw 'Unsafe authentication fixture account ID.' }
        "'$($_.SteamId)'"
    }) -join ','
    if (-not $fixtureIds) { throw 'Authentication expiry requires generated fixture accounts.' }
    New-Item -ItemType File -Path $WindowGate | Out-Null
    $expireSql = "UPDATE auth_sessions SET expires_at = CURRENT_TIMESTAMP - INTERVAL '1 second' WHERE steam_id IN ($fixtureIds);"
    & docker exec backend-postgres-1 psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -c $expireSql | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Local authentication expiry fixture failed.' }
}

function Renew-HarnessAuthentication {
    param([object[]]$Members)
    # Renew independently; never pass harness tokens into UE.
    foreach ($member in $Members) {
        $auth = Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ ticket = "dev:$($member.SteamId)" }
        $member.Headers = @{ Authorization = "Bearer $($auth.accessToken)" }
    }
}

function Start-Unreal {
    param([string]$Name, [string]$Destination, [string[]]$Extra, [string]$Stage = '')
    if ($unrealModuleHash -ne (Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash) {
        throw 'Shared Editor module changed before UE launch; build and run must use the same module.'
    }
    if ($Stage -and (-not $InventoryRecoveryRestart -or $Stage -cne 'restart')) { throw 'Unknown inventory restart stage.' }
    $log = Join-Path $runRoot $(if($Stage){"$Name-$Stage.log"}else{"$Name.log"})
    $runtimeName = if ($Name -ceq 'server') { 's' } elseif ($Name -cmatch '^client([1-4])$') { "c$($Matches[1])" } else { throw 'Unknown UE fixture role.' }
    if ($Stage -and $Name -cne 'server') { $runtimeName += 'r' } # a fresh client directory, no saved UI state
    $webApiBase = $baseUrl
    $itemApiBase = $baseUrl
    if ($InventoryRecovery -and $Name -ceq 'server') {
        if (-not $inventoryProxyUrl) { throw 'Inventory recovery proxy must start before server launch.' }
        $itemApiBase = $inventoryProxyUrl
        $Extra += '-RPGInventoryConcurrencyServerE2E' # existing read-only outcome evidence
        if ($inventoryRecoveryIsEquipment) { $Extra += '-RPGInventoryEquipmentRecoveryServerE2E' }
        if ($inventoryRecoveryIsConsume) { $Extra += @('-RPGInventoryConsumeRecoveryServerE2E',
            ('-RPGConsumeScopeFile="' + (Join-Path $runRoot 'consume-scope.json') + '"'),
            ('-RPGConsumeObservationStopFile="' + (Join-Path $runRoot 'consume-observation-stop.gate') + '"')) }
        if ($InventoryRecoveryConsumeDeath) { $Extra += @('-RPGInventoryRecoveryConsumeDeathE2E',
            ('-RPGConsumeDeathFile="' + (Join-Path $runRoot 'consume-death.gate') + '"')) }
        if ($InventoryRecoveryRestart -or $inventoryRecoveryIsClaimMail) {
            $Extra += ('-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:InventoryJournalDirectory="' + $inventoryJournalDirectory + '"')
        }
        if ($InventoryRecoveryRevocation) {
            $Extra += @('-RPGInventoryRecoveryRevocationE2E', '-RPGDungeonAbandonmentSettlementE2E', '-RPGDungeonMembershipBackendE2E',
                '-MULTIHOME=127.0.0.1', '-PktLagMin=80', '-PktLagMax=160', '-PktLoss=5',
                "-RPGAbandonCharacter=$($members[0].CharacterId)")
        }
    }
    if ($VerifyRegisteredDungeonDefinition) { $Extra+= '-RPGFlowE2ERegisteredDungeon=Dungeon.RaidSwordMan' }
    if ($VerifyServerDungeonContent) { $Extra+= '-RPGVerifyServerDungeonContent' }
    if ($VerifyResultAbandonmentResponseLoss -and $Name -ceq 'client1') {
        if (-not $proxyUrl) { throw 'Result abandonment proxy must be armed before target client launch.' }
        $webApiBase = $proxyUrl
    }
    $arguments = @(('"' + $project + '"'), $Destination) + $Extra + @(
        '-RPGDungeonFlowE2E', "-RPGFlowE2ELobbyDifficulty=$LobbyDifficulty", '-UseIrisReplication=1', '-RPGRequireIris', '-unattended', '-nop4', '-nosplash',
        '-nosound', '-nosteam', '-NoLiveCoding', '-NoLoadingScreen', '-DisablePython',
        ('-UserDir="' + (Join-Path $runtimeRoot $runtimeName) + '"'), ('-abslog="' + $log + '"'),
        '-ExecCmds="t.MaxFPS 30"',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$webApiBase/api",
        "-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:ApiUrl=$itemApiBase/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$baseUrl/api")
    if (($SlatePointerInput -or ($inventoryFixtureMode -and (-not $InventoryRecoveryRevocation -or $Name -ceq 'client1'))) -and $Name -cne 'server') {
        $arguments += @('-RenderOffscreen', '-windowed', '-ResX=1600', '-ResY=1000')
        if ($SlatePointerInput) { $arguments += '-RPGFlowE2ESlatePointerInput' }
        if ($ConfirmationNavigation) { $arguments += '-RPGFlowE2EConfirmationNavigation' }
    } else {
        $arguments += @('-nullrhi', '-DDC=(EnginePak,Local)',
            ('-LocalDataCachePath="' + (Join-Path $projectRoot 'Saved\Tests\BossState\DerivedDataCache') + '"'))
    }
    if ($NaturalPlayerAttack) {
        $arguments += @('-RPGFlowE2ENaturalPlayerAttack', ('-RPGFlowE2ENaturalGateFile="' + $naturalAttackGate + '"'))
    }
    if ($LiveBossAI) { $arguments += '-RPGFlowE2ELiveBossAI' }
    if ($InventoryStackMerge -and $Name -cne 'server') {
        $arguments += @('-RPGInventoryStackMergeE2E', "-RPGStackMergeSource=$($stackMergeFixture.Source)",
            "-RPGStackMergeDestination=$($stackMergeFixture.Destination)", "-RPGStackMergeExtra=$($stackMergeFixture.Extra)",
            ('-RPGStackMergeContinueFile="' + (Join-Path $runRoot 'stack-partial-verified.gate') + '"'))
    }
    if ($splitFixtureMode -and $Name -cne 'server') {
        $arguments += @('-RPGInventoryStackSplitE2E', "-RPGStackSplitSource=$($stackSplitFixture.Source)")
        if ($InventorySplitQuantity) {
            $arguments += @('-RPGInventorySplitQuantityE2E',
                ('-RPGSplitQuantityContinueFile="' + (Join-Path $runRoot 'stack-quantity-cancel-verified.gate') + '"'))
        }
    }
    if ($InventoryMoveSwap -and $Name -cne 'server') {
        $arguments += @('-RPGInventoryStackSplitE2E', '-RPGInventoryMoveSwapE2E',
            "-RPGStackSplitSource=$($moveSwapFixture.Source)", "-RPGInventorySwapOther=$($moveSwapFixture.Other)",
            ('-RPGInventoryMoveContinueFile="' + (Join-Path $runRoot 'inventory-move-verified.gate') + '"'))
    }
    if ($InventoryRecovery -and $Name -ceq 'client1') {
        $arguments += @('-RPGInventoryStackSplitE2E', '-RPGInventoryRecoveryE2E',
            "-RPGRecoverySource=$($recoveryFixture.Source)", "-RPGRecoveryOther=$($recoveryFixture.Other)",
            ('-RPGRecoveryContinueFile="' + (Join-Path $runRoot $(if($Stage){'inventory-recovery-restart.gate'}else{'inventory-recovery-verified.gate'})) + '"'))
        if ($Stage) {
            if (-not $inventoryRestartRequest) { throw 'Restored client requires the original observed request.' }
            $arguments += @('-RPGInventoryRecoveryRestoredE2E', "-RPGRecoveryOriginalRequest=$inventoryRestartRequest")
        }
        if ($InventoryRecoveryRetryFailure) { $arguments += '-RPGInventoryRecoveryRetryFailureE2E' }
        if ($InventoryRecoveryCredentialExpiry) { $arguments += '-RPGInventoryRecoveryCredentialExpiryE2E' }
        if ($InventoryRecoveryRevocation) { $arguments += '-RPGInventoryRecoveryRevocationE2E' }
        if ($InventoryRecoveryRestart) { $arguments += '-RPGInventoryRecoveryRestartE2E' }
        if ($inventoryRecoveryIsSwap) { $arguments += '-RPGInventoryRecoverySwapE2E' }
        if ($inventoryRecoveryIsMerge) { $arguments += '-RPGInventoryRecoveryMergeE2E' }
        if ($inventoryRecoveryIsSplit) { $arguments += '-RPGInventoryRecoverySplitE2E' }
        if ($inventoryRecoveryIsClaimMail) { $arguments += '-RPGInventoryRecoveryClaimMailE2E' }
        if ($inventoryRecoveryIsEquip) { $arguments += '-RPGInventoryRecoveryEquipE2E' }
        if ($inventoryRecoveryIsUnequip) { $arguments += '-RPGInventoryRecoveryUnequipE2E' }
        if ($inventoryRecoveryIsConsume) { $arguments += '-RPGInventoryRecoveryConsumeE2E' }
        if ($InventoryRecoveryConsumeQuickSlot) { $arguments += '-RPGInventoryRecoveryConsumeQuickSlotE2E' }
        if ($InventoryRecoveryConsumeDeath) { $arguments += '-RPGInventoryRecoveryConsumeDeathE2E' }
    }
    if ($InventoryConcurrency) {
        if ($Name -ceq 'server') { $arguments += '-RPGInventoryConcurrencyServerE2E' }
        else {
            $role = [int]$Name.Substring(6)
            $own = $concurrencyFixture[$role-1]; $foreign = $concurrencyFixture[2-$role]
            $arguments += @('-RPGInventoryStackSplitE2E', '-RPGInventoryConcurrencyE2E',
                "-RPGInventoryConcurrencyRole=$role", "-RPGInventoryConcurrencyOwnA=$($own.Ids[0])",
                "-RPGInventoryConcurrencyForeignA=$($foreign.Ids[0])",
                ('-RPGInventoryConcurrencyGate="' + (Join-Path $runRoot 'inventory-concurrency-start.gate') + '"'))
            if ($role -eq 1) { $arguments += "-RPGInventoryConcurrencyOwnB=$($own.Ids[1])" }
            else { $arguments += "-RPGInventoryConcurrencyForeignB=$($foreign.Ids[1])" }
        }
    }
    if ($VerifyResultAbandonment) {
        $arguments += @('-RPGDungeonAbandonmentTesting', '-RPGDungeonResultAbandonmentTesting',
            '-RPGFlowE2EResultAbandonment', "-RPGFlowE2EResultAbandonCharacter=$($members[0].CharacterId)",
            ('-RPGFlowE2EResultRetryGate="' + $resultRetryGate + '"'),
            ('-RPGFlowE2EResultSettleGate="' + $resultSettleGate + '"'))
    }
    if ($VerifyResultAbandonmentResponseLoss -and $Name -ceq 'client1') {
        $arguments += @('-RPGFlowE2EResultAbandonmentResponseLoss', '-RPGDevelopmentLogin',
            ('-RPGFlowE2EAbandonmentRetryFile="' + $resultPostRetryGate + '"'))
        if ($VerifyResultAbandonmentReauthentication) {
            $arguments += @('-RPGFlowE2EAbandonmentReauthenticate',
                ('-RPGFlowE2EAbandonmentRenewedRetryFile="' + $resultRenewedPostGate + '"'))
        }
    }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden -WorkingDirectory $projectRoot
    $processes.Add($process)
    $logs.Add($log)
    Write-Host "Started $Name (PID $($process.Id)): $log"
    return $log
}

function Read-ResultAbandonmentDatabase {
    # Read only this run's rows. No fixture mutation of membership, receipts or settlement.
    foreach ($id in @($sessionId, $members[0].CharacterId, $members[1].CharacterId)) {
        if ($id -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Noncanonical result abandonment UUID.' }
    }
    $sql = "SELECT json_build_object('state',(SELECT state FROM public.dungeon_sessions WHERE dungeon_session_id='$sessionId'),'receipts',(SELECT COALESCE(json_agg(row_to_json(r)),'[]'::json) FROM (SELECT character_id,steam_id,abandoned_at,session_state_at_abandonment FROM public.dungeon_session_abandonments WHERE dungeon_session_id='$sessionId') r),'members',(SELECT COALESCE(json_agg(character_id ORDER BY character_id),'[]'::json) FROM public.dungeon_session_members WHERE dungeon_session_id='$sessionId'),'settlements',(SELECT COALESCE(json_agg(row_to_json(s)),'[]'::json) FROM (SELECT state,character_ids FROM public.dungeon_reward_settlements WHERE dungeon_session_id='$sessionId') s));"
    $rows = & docker compose -f (Join-Path $projectRoot 'Backend\compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
    if ($LASTEXITCODE -ne 0) { throw 'Result abandonment PostgreSQL read failed.' }
    return ($rows | ConvertFrom-Json)
}

function Start-ResultAbandonmentProxy {
    $node = (Get-Command node -ErrorAction Stop).Source
    $proxyScript = Join-Path $projectRoot 'Scripts\Network\Fixtures\DungeonAbandonmentHttpFaultProxy.mjs'
    $controlToken = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
    $proxyOut = Join-Path $runRoot 'abandon-proxy.stdout.log'
    $proxyErr = Join-Path $runRoot 'abandon-proxy.stderr.log'
    try {
        $env:PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN = $controlToken
        $env:PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT = [string]$backendUri.Port
        $env:PROJECT_RPG_ABANDON_PROXY_TRANSPARENT = '1'
        $proxy = Start-Process -FilePath $node -ArgumentList ('"' + $proxyScript + '"') -WorkingDirectory $projectRoot `
            -RedirectStandardOutput $proxyOut -RedirectStandardError $proxyErr -PassThru -WindowStyle Hidden
    }
    finally {
        foreach ($name in @('PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN','PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT','PROJECT_RPG_ABANDON_PROXY_TRANSPARENT')) {
            [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
        }
    }
    $processes.Add($proxy)
    $logs.Add($proxyOut); $logs.Add($proxyErr)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $ready = $null
    do {
        Assert-Healthy
        if ((Test-Path -LiteralPath $proxyOut) -and (Get-Item -LiteralPath $proxyOut).Length -gt 0) {
            $readyText = [string](Get-Content -Raw -LiteralPath $proxyOut)
            if ([string]::IsNullOrEmpty($readyText)) { continue }
            $ready = [regex]::Match($readyText, 'ABANDON_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)')
            if ($ready.Success) { break }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $ready -or -not $ready.Success) { throw 'Result abandonment response-loss proxy did not start.' }
    $script:controlUrl = "http://127.0.0.1:$($ready.Groups[1].Value)"
    $script:proxyUrl = "http://127.0.0.1:$($ready.Groups[2].Value)"
    $script:controlHeaders = @{ Authorization = "Bearer $controlToken" }
    $armed = Invoke-RestMethod -Method Post -Uri "$controlUrl/arm" -Headers $controlHeaders -ContentType 'application/json' `
        -Body (@{sessionId=$sessionId;characterId=$members[0].CharacterId;authorizationSHA256=$null} | ConvertTo-Json -Compress)
    if ($armed.phase -cne 'armed') { throw 'Result abandonment proxy did not arm the exact player POST.' }
}

function Read-ResultAbandonmentProxy {
    $state = Invoke-RestMethod -Uri "$controlUrl/state" -Headers $controlHeaders
    if ($state.failure) { throw 'Result abandonment fault proxy reported a failure.' }
    return $state
}

function Complete-ResultAbandonmentRecovery {
    # Party settlement has completed while UE still has no acknowledgement.
    Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1'
    if ($VerifyResultAbandonmentReauthentication) { Expire-FixtureAuthentication @($members[0]) $authExpiredFile }
    $released = Invoke-RestMethod -Method Post -Uri "$controlUrl/release-replay" -Headers $controlHeaders
    if (-not $released.replayReleased -or $released.replayForwarded -ne 0) { throw 'Result abandonment manual replay release mismatch.' }
    New-Item -ItemType File -Path $resultPostRetryGate | Out-Null
    Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1'
    if ($VerifyResultAbandonmentReauthentication) {
        Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI AUTH_EXPIRED Real401=1 AccountPinned=1 IdentityRetained=1 NewDungeonLocked=1 LiveLogin=1'
        $after401 = Read-ResultAbandonmentProxy
        if ($after401.unauthorizedForwarded -ne 1 -or $after401.replayForwarded -ne 0) {
            throw 'Result abandonment expired retry did not produce exactly one real backend 401.'
        }
        Renew-HarnessAuthentication @($members[0]) # independently renewed; never injected into UE
        Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI AUTH_RESTORED SameAccount=1 OwnedRoster=1 IdentityRetained=1 RetryEnabled=1'
        New-Item -ItemType File -Path $resultRenewedPostGate | Out-Null
        Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI RENEWED_RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1'
    }
    Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI RECOVERED LiveButton=1 ReceiptApplied=1 SelectionRetained=1 NewDungeonEnabled=1'
    Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON RECEIPT SameProcess=1 ActualLobby=1 ValidatedPlayerPost=1 RewardExcluded=1'
    Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON LOBBY_COMPLETE Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1'
    $state = Read-ResultAbandonmentProxy
    if ($state.forwarded -ne 1 -or $state.droppedAcknowledgements -ne 1 -or $state.blockedReplays -lt 0 -or
        $state.replayForwarded -ne 1 -or $state.replayReceiptSHA256 -cne $state.receiptSHA256 -or
        $state.unauthorizedForwarded -ne [int][bool]$VerifyResultAbandonmentReauthentication) {
        throw 'Result abandonment recovery did not return the same immutable receipt exactly once.'
    }
    $state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'abandonment-proxy-final.json') -Encoding UTF8
}

try {
    # Process-local compiled runtime SDK revalidation skip, restored in finally.
    $env:UE_SKIP_UBT_SDK_SETUP = '1'
    if ($InventoryRecoveryConsumeDeath) {
        # Keep all application sources, SQL and intermediate outputs inside the
        # already-owned backend-build cleanup root. Never build the original csproj.
        $consumeBackendSnapshot = New-InventoryConsumeBackendSnapshot $projectRoot (Join-Path $buildRoot 'inputs')
        $backendProject = $consumeBackendSnapshot.ProjectPath
        $consumeBackendSnapshot | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'backend-source-snapshot.json') -Encoding UTF8
    }
    if (-not $SkipPostgresStart) {
        # Windows PowerShell treats compose's informational stderr as an error
        # record. Preserve the log, but determine native success by its exit code.
        $composeErrorPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & docker compose -f (Join-Path $projectRoot 'Backend\compose.postgres.yml') up -d --wait *> (Join-Path $runRoot 'postgres-startup.log')
            $composeExitCode = $LASTEXITCODE
        }
        finally { $ErrorActionPreference = $composeErrorPreference }
        if ($composeExitCode -ne 0) { throw 'Development PostgreSQL startup failed.' }
    }
    # OutputPath must not contain the copied project: the SDK excludes output
    # descendants from Compile globs. Keep inputs and runtime as siblings.
    $buildArguments = @('build', $backendProject, '--output', $backendOutputRoot)
    if ($NoBackendRestore) { $buildArguments += '--no-restore' }
    & $dotnet @buildArguments
    if ($LASTEXITCODE -ne 0) { throw 'Backend build failed.' }
    if ($InventoryRecoveryConsumeDeath) { Assert-InventoryConsumeBackendSnapshot $projectRoot $consumeBackendSnapshot }
    $env:ASPNETCORE_ENVIRONMENT = 'Development'
    $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN = $adminToken
    $env:POSTGRES_CONNECTION_STRING = $ConnectionString
    [Environment]::SetEnvironmentVariable('Kestrel__Endpoints__Http__Url', $baseUrl, 'Process')
    foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID', 'PROJECT_RPG_DUNGEON_SESSION_ID', 'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $backendOut = Join-Path $runRoot 'backend.stdout.log'
    $backendErr = Join-Path $runRoot 'backend.stderr.log'
    $backend = Start-Process -FilePath $dotnet -ArgumentList ('"' + $backendAssemblyPath + '"') `
        -WorkingDirectory $backendOutputRoot -RedirectStandardOutput $backendOut -RedirectStandardError $backendErr -PassThru -WindowStyle Hidden
    $processes.Add($backend)
    $ready = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        try {
            $health = Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 2
            if ($health.status -eq 'ready' -and $health.storageProvider -eq 'Postgres') { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw "Backend readiness failed; inspect $backendOut and $backendErr" }
    & (Join-Path $projectRoot 'Backend\seed-game-economy.ps1') -BaseUrl $baseUrl -AdminToken $adminToken | Out-Null
    $runNumber = [Convert]::ToUInt32($runId.Substring(0, 8), 16)
    $members = @(for ($index = 1; $index -le $ClientCount; $index++) {
        $steamId = ([uint64]76561300000000000 + [uint64]$runNumber * 4 + [uint64]$index).ToString()
        $auth = Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ ticket = "dev:$steamId" }
        $headers = @{ Authorization = "Bearer $($auth.accessToken)" }
        $characterName = "Flow$($runId.Substring(0, 12))$index"
        $characterId = ''
        if ($CreateCharactersInUI) {
            $rosterResponse = Invoke-Api 'characters' 'Get' $headers
            $roster = @($rosterResponse) # PS 5 can emit an empty JSON array as one pipeline object
            if ($roster.Count -ne 0) { throw 'UI character creation requires a fresh account with an empty roster.' }
        }
        else {
            $character = Invoke-Api 'characters' 'Post' $headers @{ name = $characterName }
            $characterId = [string]$character.characterId
        }
        [pscustomobject]@{ SteamId = $steamId; CharacterId = $characterId; CharacterName = $characterName; Headers = $headers }
    })
    if ($LobbyUI) {
        $clientLogs = @(for ($index = 0; $index -lt $members.Count; $index++) {
            $member = $members[$index]
            $extra = @('-game', '-RPGDevelopmentLogin', '-RPGFlowE2ELobbyUI',
                "-RPGFlowE2ESteam=$($member.SteamId)",
                ('-RPGFlowE2ESessionFile="' + $sharedSessionFile + '"'), ('-RPGFlowE2EServerReadyFile="' + $serverReadyFile + '"'))
            if ($index -eq 0) { $extra += '-RPGFlowE2ELobbyLeader' }
            if ($LobbyDifficultyOnly) { $extra += '-RPGFlowE2ELobbyDifficultyOnly' }
            if ($CreateCharactersInUI) {
                $extra += @('-RPGFlowE2ECreateCharacter', "-RPGFlowE2ECharacterName=$($member.CharacterName)")
            }
            else { $extra += "-RPGFlowE2ECharacter=$($member.CharacterId)" }
            if ($ReauthenticateInLobby) {
                $extra += @('-RPGFlowE2EReauthenticate', ('-RPGFlowE2EAuthExpiredFile="' + $authExpiredFile + '"'))
            }
            if ($ReauthenticateAfterAdmission) {
                $extra += @('-RPGFlowE2EReauthenticateAfterAdmission', ('-RPGFlowE2EDungeonAuthExpiredFile="' + $dungeonAuthExpiredFile + '"'))
            }
            Start-Unreal ("client" + ($index + 1)) '/Game/Maps/LobbyMap' $extra
        })
        if ($CreateCharactersInUI) {
            for ($index = 0; $index -lt $members.Count; $index++) {
                $log = $clientLogs[$index]
                Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_CHARACTER CreatedInUI=1 Character='
                Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_CHARACTER_GUARDS InvalidNames=3 Trimmed=1 DuplicateClickLocked=1'
                Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_CHARACTER_ROSTER OwnedCharacter=1 Refreshed=1 InputCleared=1'
                $createdCharacter = Select-String -LiteralPath $log -Pattern 'DUNGEON_FLOW_E2E_LOBBY_CHARACTER CreatedInUI=1 Character=([0-9a-fA-F-]{36})' | Select-Object -Last 1
                if (-not $createdCharacter) { throw 'UI did not return a canonical created character.' }
                $members[$index].CharacterId = $createdCharacter.Matches[0].Groups[1].Value
                $rosterResponse = Invoke-Api 'characters' 'Get' $members[$index].Headers
                $roster = @($rosterResponse)
                if ($roster.Count -ne 1 -or $roster[0].characterId -cne $members[$index].CharacterId -or
                    $roster[0].name -cne $members[$index].CharacterName) {
                    throw 'Owned backend roster does not match the single UI-created character.'
                }
            }
        }
        Wait-Marker $clientLogs[0] 'DUNGEON_FLOW_E2E_LOBBY_SESSION Created=1 Session='
        $created = Select-String -LiteralPath $clientLogs[0] -Pattern 'DUNGEON_FLOW_E2E_LOBBY_SESSION Created=1 Session=([0-9a-fA-F-]{36})' | Select-Object -Last 1
        if (-not $created) { throw 'Lobby did not create a canonical backend session.' }
        $sessionId = $created.Matches[0].Groups[1].Value
        $sessionId | Set-Content -LiteralPath $sharedSessionFile -Encoding UTF8
        foreach ($log in @($clientLogs | Select-Object -Skip 1)) {
            Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_SESSION Joined=1 Session='
        }
        $session = Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
        if ($session.state -ne 'Waiting' -or $session.difficulty -cne $LobbyDifficulty -or @($session.members).Count -ne $ClientCount -or
            @($members | Where-Object { $_.CharacterId -cnotin @($session.members | ForEach-Object { $_.characterId }) }).Count) {
            throw 'UI-selected difficulty or authenticated Waiting party did not reach the backend.'
        }
        Wait-Marker $clientLogs[0] "DUNGEON_FLOW_E2E_LOBBY_DIFFICULTY_SELECTED Difficulty=$LobbyDifficulty Options=4 LocalOnly=1 InvalidRejected=1"
        foreach ($log in $clientLogs) {
            Wait-Marker $log "DUNGEON_FLOW_E2E_LOBBY_DIFFICULTY_SESSION Difficulty=$LobbyDifficulty BackendSnapshot=1 SelectorLocked=1 NativeGuards=1"
        }
        if ($LobbyDifficultyOnly) {
            for ($check = 0; $check -lt 8; $check++) { Start-Sleep -Milliseconds 250; Assert-Healthy }
            $unrealModuleHashAfter = (Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash
            $sameUnrealModule = $unrealModuleHash -eq $unrealModuleHashAfter
            $sameDefinitionSources=$true
            if ($VerifyRegisteredDungeonDefinition) {
                if ($session.dungeonId -cne 'Dungeon.RaidSwordMan') { throw 'Saved dungeon definition identity did not reach the authenticated backend session.' }
                Wait-Marker $clientLogs[0] 'DUNGEON_FLOW_E2E_DUNGEON_DEFINITION_SELECTED Dungeon=Dungeon.RaidSwordMan ConfiguredDefinition=1 AuthoredLabel=1 LiveSelector=1 LocalOnly=1 InvalidRejected=1'
                foreach($log in $clientLogs) {
                    Wait-Marker $log 'DUNGEON_FLOW_E2E_DUNGEON_DEFINITION_SESSION Dungeon=Dungeon.RaidSwordMan BackendSnapshot=1 SelectorLocked=1 SameIdentity=1'
                }
                foreach($path in $resultSourcePaths) {
                    if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameDefinitionSources=$false }
                }
            }
            [pscustomobject]@{ Passed = ($sameUnrealModule -and $sameDefinitionSources); FunctionalFlowObserved = $true; SessionId = $sessionId;
                Clients = $ClientCount; DifficultySelectionOnly = $true; Difficulty = $LobbyDifficulty;
                ActualLobbyMap = $true; WaitingSessionCreatedInUE = $true; BackendPartyJoinFromUI = ($ClientCount -gt 1);
                FourLocalOptionsVerified = $true; InvalidSelectionRejected = $true; RequestAndSessionLocksVerified = $true;
                BackendSnapshotDisplayedForEachMember = $true; ModuleUnchangedDuringRun = $sameUnrealModule;
                RegisteredDungeonDefinitionVerified = [bool]$VerifyRegisteredDungeonDefinition;
                DungeonId = $session.dungeonId; DefinitionSourcesUnchanged = $sameDefinitionSources;
                DefinitionSourceHashes = $resultSourceHashes;
                EditorModuleSHA256Before = $unrealModuleHash; EditorModuleSHA256After = $unrealModuleHashAfter;
                ServerAssignmentVerified = $false; BossCombatVerified = $false; RewardSettlementVerified = $false;
                SteamTransportVerified = $false; BlueprintAssetSaved = $false } |
                ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
            if (-not $sameUnrealModule) { throw 'Shared Editor module changed during lobby selection; observations retained without current-binary proof.' }
            if (-not $sameDefinitionSources) { throw 'Dungeon definition source/asset/Config changed during lobby selection.' }
            $succeeded = $true
            Write-Host "PASS: live lobby difficulty selection -> authenticated Waiting session -> party snapshot and immutable selection. Logs: $runRoot"
            return
        }
        if ($ReauthenticateInLobby) {
            Expire-FixtureAuthentication $members $authExpiredFile
            foreach ($log in $clientLogs) {
                Wait-Marker $log 'DUNGEON_FLOW_E2E_AUTH_EXPIRED TokenInvalidated=1 AccountPinned=1 SelectionRetained=1 DuplicateLoginLocked=1'
                Wait-Marker $log 'Dungeon session load request failed (HTTP 401).'
                Wait-Marker $log 'DUNGEON_FLOW_E2E_REAUTH_RESTORED SameAccount=1 LiveLoginButton=1 OwnedRoster=1 CharacterSessionRetained=1'
            }
            Renew-HarnessAuthentication $members
        }
    }
    else {
        $session = Invoke-Api 'dungeon-sessions' 'Post' $members[0].Headers @{
            characterId = $members[0].CharacterId; dungeonId = 'Dungeon.RaidSwordMan'; difficulty = $LobbyDifficulty
        }
        $sessionId = [string]$session.dungeonSessionId
        foreach ($member in @($members | Select-Object -Skip 1)) {
            $session = Invoke-Api "dungeon-sessions/$sessionId/members" 'Post' $member.Headers @{ characterId = $member.CharacterId }
        }
    }
    $serverId = "boss-return-$($runId.Substring(0, 12))"
    . (Join-Path $projectRoot 'Backend/Tools/Request-DungeonDeparture.ps1')
    $null = Request-RPGDungeonDeparture $baseUrl $sessionId $members[0].CharacterId $members[0].Headers
    $assignment = Invoke-Api "dungeon-sessions/$sessionId/activate" 'Post' $adminHeaders @{
        serverId = $serverId; serverAddress = "127.0.0.1:$Port"
    }
    if ($InventoryRecovery) { Start-InventoryRecoveryProxy }
    try {
        $env:PROJECT_RPG_GAME_SERVER_ID = $serverId
        $env:PROJECT_RPG_DUNGEON_SESSION_ID = $sessionId
        $env:PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN = [string]$assignment.gameServerAccessToken
        # Existing non-Shipping no-Steam identity fixture only; ticket consumption/membership stay enforced.
        $serverLog = Start-Unreal 'server' '/Game/Maps/bossmap' @('-server', "-port=$Port",
            '-RPGItemE2EServer', '-RPGItemE2EAllowUnverifiedIdentity', "-RPGFlowE2EPlayers=$ClientCount", ('-RPGFlowE2EGate="' + $gate + '"'))
    }
    finally {
        foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID', 'PROJECT_RPG_DUNGEON_SESSION_ID', 'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
    }
    Wait-Marker $serverLog 'DUNGEON_FLOW_E2E_CONFIG_WAITING BackendReady=0 NoBoss=1 Waiting=1 LocalDifficultyWrite=0'
    Wait-Marker $serverLog "DUNGEON_FLOW_E2E_CONFIG_CONFIRMED BackendReady=1 Difficulty=$LobbyDifficulty LocalDifficultyWrite=0 BossStartupReady=1 StartupAbilityLevel=$($expectedRewards.AbilityLevel)"
    if ($VerifyServerDungeonContent) {
        Wait-Marker $serverLog "DUNGEON_SERVER_CONTENT CONFIRMED Dungeon=Dungeon.RaidSwordMan Map=/Game/Maps/bossmap Difficulty=$LobbyDifficulty RewardVersion=$($expectedRewards.Version) AuthenticatedConfiguration=1 PinnedPayload=1"
    }
    Wait-Marker $serverLog 'DUNGEON_FLOW_E2E_SERVER_READY ActualBossMap=1 Iris=1 Assignment=1 AdmissionBypass=0'
    $session = Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if ($session.state -cne 'InProgress' -or $session.difficulty -cne $LobbyDifficulty -or $session.serverId -cne $serverId) {
        throw 'Backend did not confirm the exact server assignment and difficulty.'
    }
    $waitingLine = (Select-String -LiteralPath $serverLog -SimpleMatch 'DUNGEON_FLOW_E2E_CONFIG_WAITING' | Select-Object -First 1).LineNumber
    $confirmedLine = (Select-String -LiteralPath $serverLog -SimpleMatch 'DUNGEON_FLOW_E2E_CONFIG_CONFIRMED' | Select-Object -First 1).LineNumber
    $readyLine = (Select-String -LiteralPath $serverLog -SimpleMatch 'DUNGEON_FLOW_E2E_SERVER_READY' | Select-Object -First 1).LineNumber
    if (-not ($waitingLine -lt $confirmedLine -and $confirmedLine -lt $readyLine)) {
        throw 'Boss startup did not follow the backend configuration waiting/confirmation boundary.'
    }
    if ($InventoryStackMerge) { $stackMergeFixture = Initialize-StackMergeFixture }
    if ($splitFixtureMode) { $stackSplitFixture = Initialize-StackSplitFixture }
    if ($InventoryMoveSwap) { $moveSwapFixture = Initialize-InventoryMoveSwapFixture }
    if ($InventoryRecovery) {
        $recoveryFixture = Initialize-InventoryMoveSwapFixture -Merge:$inventoryRecoveryIsMerge -Mail:$inventoryRecoveryIsClaimMail `
            -Equipment $(if($inventoryRecoveryIsEquipment){$InventoryRecoveryOperation}else{'None'})
        if($inventoryRecoveryIsConsume) {
            $scopeTemp=Join-Path $runRoot 'consume-scope.tmp'
            @{source=$recoveryFixture.Source;owner=$recoveryFixture.Owner} | ConvertTo-Json -Compress | Set-Content -LiteralPath $scopeTemp -Encoding UTF8
            Move-Item -LiteralPath $scopeTemp -Destination (Join-Path $runRoot 'consume-scope.json')
        }
        Arm-InventoryRecoveryProxy
    }
    if ($InventoryConcurrency) { $concurrencyFixture = @(Initialize-InventoryConcurrencyFixture) }
    if ($VerifyResultAbandonmentResponseLoss) { Start-ResultAbandonmentProxy }
    if ($LobbyUI) { New-Item -ItemType File -Path $serverReadyFile | Out-Null }
    else {
        $clientLogs = @(foreach ($member in $members) {
            # Start locally, authenticate in UE, resume the real session, then mint a fresh ticket after warm-up.
            Start-Unreal ("client" + ($members.IndexOf($member) + 1)) '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' @('-game',
                "-RPGFlowE2ESteam=$($member.SteamId)", "-RPGFlowE2ECharacter=$($member.CharacterId)", "-RPGFlowE2ESession=$sessionId")
        })
    }
    foreach ($log in $clientLogs) { Wait-Marker $log 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1' }
    if ($InventoryRecovery) {
        Complete-InventoryRecoveryFixture
        $succeeded = $true
        if ($InventoryRecoveryRestart) {
            Write-Host "PASS: same-allocation UE server restart / restored authored WBP recovery / identical frozen replay / PostgreSQL one commit. Logs: $runRoot"
        } elseif ($InventoryRecoveryRevocation) {
            Write-Host "PASS: pending committed inventory command / real abandonment receipt / Iris ejection / remaining party and stored inventory preserved. Logs: $runRoot"
        } elseif ($InventoryRecoveryCredentialExpiry) {
            Write-Host "PASS: assigned server credential expired / real backend 401 / original command still pending / PostgreSQL unchanged. Logs: $runRoot"
        } elseif ($InventoryRecoveryConsumeDeath) {
            Write-Host "PASS: original recovery response held / approved lethal damage / no potion effect / same-request recovery unlocked / PostgreSQL one commit. Logs: $runRoot"
        } else {
            Write-Host "PASS: authored WBP recovery button / lost committed HTTP response / frozen same-request replay / PostgreSQL one commit. Logs: $runRoot"
        }
        return
    }
    if ($InventoryConcurrency) {
        Complete-InventoryConcurrencyFixture
        $succeeded = $true
        Write-Host "PASS: two authenticated clients / owner isolation / PostgreSQL location conflict / cached replay. Logs: $runRoot"
        return # no combat, settlement, UI input or platform authentication claim
    }
    if ($InventoryMoveSwap) {
        Complete-InventoryMoveSwapFixture
        $succeeded = $true
        Write-Host "PASS: authenticated WBP / plain drag move and atomic swap / PostgreSQL receipts / retained quickslots. Logs: $runRoot"
        return # do not open the combat gate
    }
    if ($splitFixtureMode) {
        Complete-StackSplitFixture
        $succeeded = $true
        Write-Host "PASS: authenticated inventory WBP / routed $(if ($InventorySplitQuantity) { 'Ctrl quantity cancel and confirm' } else { 'Shift drag' }) / PostgreSQL atomic split / retained source quickslot. Logs: $runRoot"
        return # do not open the combat gate
    }
    if ($InventoryStackMerge) {
        Complete-StackMergeFixture
        $succeeded = $true
        Write-Host "PASS: authenticated inventory WBP / routed drag / PostgreSQL partial and full merge / quickslot cleanup. Logs: $runRoot"
        return # finally still closes only this run's backend/UE processes; do not open the combat gate
    }
    if ($ReauthenticateAfterAdmission) {
        Expire-FixtureAuthentication $members $dungeonAuthExpiredFile
        foreach ($log in $clientLogs) {
            Wait-Marker $log 'DUNGEON_FLOW_E2E_DUNGEON_AUTH_EXPIRED InProgress=1 BeforeReward=1 TokenInvalidated=1 IdentityRetained=1'
            Wait-Marker $log 'Dungeon session load request failed (HTTP 401).'
        }
        Renew-HarnessAuthentication $members
    }
    if ($VerifyResultAbandonment) {
        $beforeAbandonment = Read-ResultAbandonmentDatabase
        if ($beforeAbandonment.state -cne 'InProgress' -or @($beforeAbandonment.members).Count -ne 2 -or
            @($beforeAbandonment.receipts).Count -ne 0 -or @($beforeAbandonment.settlements).Count -ne 0) {
            throw 'Result abandonment initial persistent state mismatch.'
        }
        $beforeItems = Invoke-Api "items?ownerType=Character&ownerId=$($members[0].CharacterId)&includeTerminal=false&limit=500" 'Get' $members[0].Headers
        $retainedItemsBefore = @($beforeItems.items | Sort-Object { $_.state.instanceId }) | ConvertTo-Json -Depth 30 -Compress
    }
    New-Item -ItemType File -Path $gate | Out-Null
    if ($VerifyResultAbandonment) {
        Wait-Marker $serverLog 'DUNGEON_RESULT_ABANDON SERVER_WIPE Party=2 ServerGASFixture=1 NoDirectBattleStateWrite=1'
        Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON CANCEL LiveConfirmation=1 NoPost=1 NoTravel=1 MembershipRetained=1'
        Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON QUEUED LiveResultConfirmation=1 IdentityPinned=1 NoConnectedPost=1 RetryBlocked=1 DuplicateGuard=1'
        if ($VerifyResultAbandonmentResponseLoss) {
            Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON LOST_REPLY SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1'
            Wait-Marker $clientLogs[0] 'DUNGEON_ABANDON_UI UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1'
            $lost = Read-ResultAbandonmentProxy
            # Preserve the fault snapshot even when a transport assertion fails.
            $lost | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'abandonment-proxy-lost.json') -Encoding UTF8
            # libcurl may report the lost reply without an automatic replay.
            # Never require a transport retry to occur; no replay may reach the
            # backend before explicit release. The fixture unit tests force and
            # verify held replays independently of this client transport choice.
            $automaticTransportReplayObserved = $lost.blockedReplays -gt 0
            if ($lost.phase -cne 'dropped' -or $lost.forwarded -ne 1 -or $lost.droppedAcknowledgements -ne 1 -or
                $lost.blockedReplays -lt 0 -or $lost.replayForwarded -ne 0 -or $lost.receiptSHA256 -cnotmatch '^[0-9a-f]{64}$') {
                throw 'Result abandonment initial response loss or premature replay mismatch.'
            }
        }
        else {
            Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON RECEIPT SameProcess=1 ActualLobby=1 ValidatedPlayerPost=1 RewardExcluded=1'
            Wait-Marker $clientLogs[0] 'DUNGEON_RESULT_ABANDON LOBBY_COMPLETE Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1'
        }
        Wait-Marker $clientLogs[1] 'DUNGEON_RESULT_ABANDON REMAINING_DEFEATED LiveResult=1 MembershipRetained=1'
        $afterAbandonment = Read-ResultAbandonmentDatabase
        $receiptRows = @($afterAbandonment.receipts)
        if ($afterAbandonment.state -cne 'InProgress' -or @($afterAbandonment.members).Count -ne 1 -or
            $afterAbandonment.members[0] -cne $members[1].CharacterId -or @($afterAbandonment.settlements).Count -ne 0 -or
            $receiptRows.Count -ne 1 -or $receiptRows[0].character_id -cne $members[0].CharacterId -or
            $receiptRows[0].steam_id -cne $members[0].SteamId -or $receiptRows[0].session_state_at_abandonment -cne 'InProgress') {
            throw 'Result abandonment receipt/remaining-party database mismatch before retry.'
        }
        $receiptBeforeSettlement = $receiptRows[0] | ConvertTo-Json -Compress
        New-Item -ItemType File -Path $resultRetryGate | Out-Null
        Wait-Marker $clientLogs[1] 'DUNGEON_RESULT_ABANDON RETRY_CLICK LiveResult=1 ServerRPC=1 DuplicateGuard=1'
        Wait-Marker $serverLog 'DUNGEON_RESULT_ABANDON SERVER_RETRY Remaining=1 DepartedAbsent=1 SamePawn=1 NewBoss=1'
        Wait-Marker $clientLogs[1] 'DUNGEON_RESULT_ABANDON RETRY_RECOVERED SamePawn=1 FullHealth=1 DeathCleared=1 CapsuleInputReady=1 NewBoss=1 PendingCleared=1'
        New-Item -ItemType File -Path $resultSettleGate | Out-Null
    }
    $rewardMembers = @($members | Select-Object -Skip $(if ($VerifyResultAbandonment) { 1 } else { 0 }))
    $rewardClientLogs = @($clientLogs | Select-Object -Skip $(if ($VerifyResultAbandonment) { 1 } else { 0 }))
    if ($NaturalPlayerAttack) {
        Wait-Marker $serverLog "DUNGEON_FLOW_E2E_NATURAL_READY PersistedStarterWeapons=$ClientCount NativeAbilities=1 ManualDamage=0"
        New-Item -ItemType File -Path $naturalAttackGate | Out-Null
        if ($LiveBossAI) { Wait-Marker $serverLog 'DUNGEON_FLOW_E2E_LIVE_AI_STARTED BrainRestarted=1 ManualDamage=0' }
        Wait-Marker $serverLog "DUNGEON_FLOW_E2E_NATURAL_VICTORY AuthoredPlayerInput=1 BossDead=1 Party=$ClientCount NoManualDamage=1"
        if ($LiveBossAI) { Wait-Marker $serverLog 'DUNGEON_FLOW_E2E_LIVE_AI_VICTORY Telegraph=1 AttackFinished=1 PlayerDamaged=' }
    }
    else { Wait-Marker $serverLog "DUNGEON_FLOW_E2E_SERVER_VICTORY ServerGASHit=1 DeadToVictory=1 Party=$($rewardMembers.Count)" }
    foreach ($log in $rewardClientLogs) {
        if ($ReauthenticateAfterAdmission) {
            Wait-Marker $log 'DUNGEON_FLOW_E2E_RESULT_REAUTH_CLICK LiveButton=1 DuplicateLocked=1 ExpiredReturnRefreshBlocked=1'
            Wait-Marker $log 'DUNGEON_FLOW_E2E_RESULT_REAUTH_RESTORED SameAccount=1 FreshTerminalRead=1 SelectionSessionRetained=1'
        }
        if ($LobbyUI) {
            Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_LOGIN ActualMap=1 LiveButton=1'
            Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_ENTRY LiveButton=1 FreshTicket=1 TrustedBackendAddress=1'
            Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_UI_RESTORED SelectedRosterCharacter=1'
        }
        Wait-Marker $log "DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=$($expectedRewards.Gold) InventoryQuantity=$expectedInventoryQuantity MailQuantity=0 RewardVersion=$($expectedRewards.Version)"
        Wait-Marker $log 'DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1'
    }
    $plannedDisconnectDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Assert-Healthy
        $plannedDisconnectCount = @(Select-String -LiteralPath $serverLog -SimpleMatch 'ErrorContext="dungeon_planned_lobby_disconnect"').Count
        if ($plannedDisconnectCount -ge $ClientCount) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $plannedDisconnectDeadline)
    if ($plannedDisconnectCount -ne $ClientCount) { throw "Planned lobby disconnect count mismatch: $plannedDisconnectCount of $ClientCount." }
    $persistedSession = Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if ($persistedSession.state -ne 'Cleared' -or $persistedSession.difficulty -cne $LobbyDifficulty -or
        $persistedSession.serverId -cne $serverId -or @($persistedSession.members).Count -ne $rewardMembers.Count) { throw 'Completed party state mismatch.' }
    if ($VerifyResultAbandonment) {
        $afterSettlement = Read-ResultAbandonmentDatabase
        $settlements = @($afterSettlement.settlements)
        if ($afterSettlement.state -cne 'Cleared' -or @($afterSettlement.receipts).Count -ne 1 -or
            ($afterSettlement.receipts[0] | ConvertTo-Json -Compress) -cne $receiptBeforeSettlement -or
            @($afterSettlement.members).Count -ne 1 -or $afterSettlement.members[0] -cne $members[1].CharacterId -or
            $settlements.Count -ne 1 -or $settlements[0].state -cne 'Completed' -or
            @($settlements[0].character_ids).Count -ne 1 -or $settlements[0].character_ids[0] -cne $members[1].CharacterId) {
            throw 'Result abandonment immutable receipt/settlement recipient mismatch.'
        }
        if ($VerifyResultAbandonmentResponseLoss) {
            $databaseBeforeRecovery = $afterSettlement | ConvertTo-Json -Depth 12 -Compress
            Complete-ResultAbandonmentRecovery
            $databaseAfterRecovery = Read-ResultAbandonmentDatabase | ConvertTo-Json -Depth 12 -Compress
            if ($databaseAfterRecovery -cne $databaseBeforeRecovery) {
                throw 'Receipt recovery changed the settled party, immutable receipt or settlement recipients.'
            }
        }
        $abandonedWallet = Invoke-Api "economy/wallets/$($members[0].CharacterId)" 'Get' $members[0].Headers
        $abandonedItems = Invoke-Api "items?ownerType=Character&ownerId=$($members[0].CharacterId)&includeTerminal=false&limit=500" 'Get' $members[0].Headers
        $retainedItems = @($abandonedItems.items)
        Assert-DungeonFirstSliceStarterEquipment $retainedItems $members[0].CharacterId
        if (@($abandonedWallet.balances | Where-Object { $_.balance -ne 0 }).Count -ne 0 -or
            $retainedItems.Count -ne 3 -or
            (@($abandonedItems.items | Sort-Object { $_.state.instanceId }) | ConvertTo-Json -Depth 30 -Compress) -cne $retainedItemsBefore) {
            throw 'Abandoned player received rewards or lost existing starter equipment.'
        }
    }
    $rewards = @(foreach ($member in $rewardMembers) {
        $wallet = Invoke-Api "economy/wallets/$($member.CharacterId)" 'Get' $member.Headers
        $gold = @($wallet.balances | Where-Object { $_.currencyCode -eq 'RosterGold' })
        $itemPage = Invoke-Api "items?ownerType=Character&ownerId=$($member.CharacterId)&includeTerminal=false&limit=500" 'Get' $member.Headers
        $items = @($itemPage.items)
        $potion = @($items | Where-Object { $_.definitionType -eq 'RPGItemDefinition' -and $_.definitionName -eq 'GameItem.Consume.Potion.Red.Large' })
        $helm = @($items | Where-Object { $_.definitionType -eq 'RPGItemDefinition' -and $_.definitionName -eq 'GameItem.Equipment.Helm.Default' })
        Assert-DungeonFirstSliceStarterEquipment $items $member.CharacterId
        if ($gold.Count -ne 1 -or $gold[0].balance -ne $expectedRewards.Gold -or $items.Count -ne $expectedItemRecords -or
            $potion.Count -ne 1 -or $potion[0].state.quantity -ne $expectedRewards.Potions -or
            $helm.Count -ne [int]($expectedRewards.Helms -gt 0) -or
            ($expectedRewards.Helms -gt 0 -and $helm[0].state.quantity -ne $expectedRewards.Helms) -or
            @($items | Where-Object { $_.location.containerType -cnotin @('Equipment','Inventory') }).Count -ne 0) {
            throw 'Persisted reward or starter equipment mismatch after lobby travel.'
        }
        [pscustomobject]@{ CharacterId = $member.CharacterId; Gold = $expectedRewards.Gold;
            Potions = $expectedRewards.Potions; Helms = $expectedRewards.Helms; RewardVersion = $expectedRewards.Version;
            ItemRecords = $expectedItemRecords; InventoryQuantity = $expectedInventoryQuantity; MailQuantity = 0 }
    })
    for ($check = 0; $check -lt 8; $check++) { Start-Sleep -Milliseconds 250; Assert-Healthy }
    if ($SlatePointerInput) {
        $pointerExpectations = @(
            @{ Log = $clientLogs[0]; Widget = 'AbandonDungeonFromResultButton'; Count = (2 + [int][bool]$ConfirmationNavigation) },
            @{ Log = $clientLogs[0]; Widget = 'CancelResultAbandonmentButton'; Count = 1 },
            @{ Log = $clientLogs[0]; Widget = 'ConfirmResultAbandonmentButton'; Count = 1 },
            @{ Log = $clientLogs[1]; Widget = 'RetryBattleButton'; Count = 1 },
            @{ Log = $clientLogs[1]; Widget = 'ReturnToLobbyButton'; Count = 1 })
        if ($VerifyResultAbandonmentResponseLoss) {
            $count = 1 + [int][bool]$VerifyResultAbandonmentReauthentication
            $pointerExpectations += @(
                @{ Log = $clientLogs[0]; Widget = 'AbandonDungeonSessionButton'; Count = ($count * (1 + [int][bool]$ConfirmationNavigation)) },
                @{ Log = $clientLogs[0]; Widget = 'ConfirmDungeonAbandonmentButton'; Count = $count })
        }
        if ($VerifyResultAbandonmentReauthentication) {
            $pointerExpectations += @{ Log = $clientLogs[0]; Widget = 'LoginDevelopmentButton'; Count = 1 }
        }
        foreach ($expected in $pointerExpectations) {
            $marker = "DUNGEON_FLOW_POINTER CLICK Widget=$($expected.Widget) HitTest=1 CommonViewport=1 Physical=0"
            if (@(Select-String -LiteralPath $expected.Log -SimpleMatch $marker).Count -ne $expected.Count) {
                throw 'Missing or duplicate routed Slate pointer input proof.'
            }
        }
        foreach ($log in $logs) {
            if (Select-String -LiteralPath $log -Pattern 'LogUIActionRouter:\s*Error:|RPG_NETTEST IRIS_REQUIRED_BUT_INACTIVE' -Quiet) {
                throw 'CommonUI viewport or Iris diagnostic regression.'
            }
        }
        if ($ConfirmationNavigation) {
            $expectedNavigations = 1
            if ($VerifyResultAbandonmentResponseLoss) { $expectedNavigations += 1 + [int][bool]$VerifyResultAbandonmentReauthentication }
            if (@(Select-String -LiteralPath $clientLogs[0] -SimpleMatch 'DUNGEON_FLOW_CONFIRMATION_NAV Verified=1').Count -ne $expectedNavigations) {
                throw 'Missing or duplicate confirmation navigation proof.'
            }
        }
    }
    $unrealModuleHashAfter = (Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash
    $sameUnrealModule = $unrealModuleHash -eq $unrealModuleHashAfter
    $sameResultSources = $true
    if ($VerifyResultAbandonment -or $VerifyServerDungeonContent) {
        $sourceAfter = @{}
        foreach ($path in $resultSourcePaths) {
            $sourceAfter[$path] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $path) -Algorithm SHA256).Hash
            if ($sourceAfter[$path] -cne $resultSourceHashes[$path]) { $sameResultSources = $false }
        }
        $sourceAfter | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'source-after.json') -Encoding UTF8
    }
    [pscustomobject]@{ Passed = ($sameUnrealModule -and $sameResultSources); SessionId = $sessionId; Clients = $ClientCount; ActualBossMap = $true;
        ServerContentMappingVerified = [bool]$VerifyServerDungeonContent;
        BackendDifficultyVerified = $true; Difficulty = $LobbyDifficulty; InitialConfigurationWaitVerified = $true;
        ActualStartupNPCAbilityLevelVerified = $true; StartupNPCAbilityLevel = $expectedRewards.AbilityLevel;
        RewardVersion = $expectedRewards.Version;
        LocalDifficultyOverrideUsed = $false; FunctionalFlowObserved = $true;
        EditorModuleSHA256Before = $unrealModuleHash; EditorModuleSHA256After = $unrealModuleHashAfter;
        ModuleUnchangedDuringRun = $sameUnrealModule;
        SelectedResultSourcesUnchangedDuringRun = $sameResultSources;
        RuntimeUserRoot = $runtimeRoot;
        AuthenticatedAdmission = $true; LiveResultClick = $true; ActualLocalLobbyTravel = $true;
        LobbyUIVerified = [bool]$LobbyUI; WaitingSessionCreatedInUE = [bool]$LobbyUI;
        LobbyDifficultySelectionVerified = [bool]$LobbyUI; BackendDifficultySnapshotLocked = [bool]$LobbyUI;
        BackendPartyJoinFromUI = [bool]($LobbyUI -and $ClientCount -gt 1);
        CharacterCreationInUIVerified = [bool]$CreateCharactersInUI;
        CharacterCreationGuardsVerified = [bool]$CreateCharactersInUI;
        BackendAuthenticationExpiryVerified = [bool]($ReauthenticateInLobby -or $ReauthenticateAfterAdmission);
        LobbyReauthenticationVerified = [bool]$ReauthenticateInLobby;
        DungeonAuthenticationExpiryVerified = [bool]$ReauthenticateAfterAdmission;
        ResultReauthenticationVerified = [bool]$ReauthenticateAfterAdmission;
        DefeatResultAbandonmentVerified = [bool]$VerifyResultAbandonment;
        ResultAbandonmentRemainingPartyRetryVerified = [bool]$VerifyResultAbandonment;
        ResultAbandonmentDatabaseExclusionVerified = [bool]$VerifyResultAbandonment;
        ResultAbandonmentResponseLossVerified = [bool]$VerifyResultAbandonmentResponseLoss;
        ResultAbandonmentAutomaticTransportReplayObserved = $automaticTransportReplayObserved;
        ResultAbandonmentReauthenticationVerified = [bool]$VerifyResultAbandonmentReauthentication;
        ResultAbandonmentReceiptRecoveredAfterPartySettlement = [bool]$VerifyResultAbandonmentResponseLoss;
        SlateHitTestPointerInputVerified = [bool]$SlatePointerInput;
        GlobalCommonUIViewportVerified = [bool]$SlatePointerInput;
        MenuDesiredFocusVerified = [bool]$SlatePointerInput;
        OffscreenRHIPointerInputVerified = [bool]$SlatePointerInput;
        PhysicalMouseInputVerified = $false;
        SlateConfirmationNavigationVerified = [bool]$ConfirmationNavigation;
        PhysicalKeyboardOrGamepadVerified = $false;
        SteamTransportVerified = $false; NaturalPlayerAttackVerified = [bool]$NaturalPlayerAttack;
        NaturalCombatVerified = [bool]$LiveBossAI;
        BossAttackDamageObserved = [bool]($LiveBossAI -and (Select-String -LiteralPath $serverLog -SimpleMatch 'DUNGEON_FLOW_E2E_LIVE_AI_DAMAGE' -Quiet));
        Rewards = $rewards } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if (-not $sameUnrealModule) { throw 'Shared Editor module changed during E2E; functional observations retained but current-binary proof is not established.' }
    if (-not $sameResultSources) { throw 'Selected result-abandonment inputs changed during E2E; current-source proof is not established.' }
    $succeeded = $true
    Write-Host "PASS: authenticated bossmap admission -> $(if ($VerifyResultAbandonment) { 'result confirmation / lobby POST / remaining-party retry and server GAS kill' } elseif ($LiveBossAI) { 'live boss AI and authored player attack kill' } elseif ($NaturalPlayerAttack) { 'authored player attack kill' } else { 'server GAS kill' }) -> PostgreSQL rewards -> live result button -> local lobby. Logs: $runRoot"
}
catch {
    if ($inventoryFixtureMode -or $InventoryConcurrency) {
        # Record structural diagnostics, never HTTP bodies, credentials or bound arguments.
        [ordered]@{ Passed = $false; Mode = $(if ($InventoryRecoveryRestart) { 'InventoryRecoveryRestart' } elseif ($InventoryRecoveryRevocation) { 'InventoryRecoveryRevocation' } elseif ($InventoryRecoveryCredentialExpiry) { 'InventoryRecoveryCredentialExpiry' } elseif ($InventoryRecovery) { 'InventoryRecovery' } elseif ($InventoryConcurrency) { 'InventoryConcurrency' } elseif ($InventoryMoveSwap) { 'InventoryMoveSwap' } elseif ($InventorySplitQuantity) { 'InventorySplitQuantity' } elseif ($InventoryStackSplit) { 'InventoryStackSplit' } else { 'InventoryStackMerge' }); ErrorType = $_.Exception.GetType().Name
            ErrorId = $_.FullyQualifiedErrorId; ScriptStack = $_.ScriptStackTrace
            FailingScript = $_.InvocationInfo.ScriptName; FailingLine = $_.InvocationInfo.ScriptLineNumber
        } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'failure.json') -Encoding UTF8
    }
    throw
}
finally {
    # Generated records and logs are retained. Never touch another run's process or database rows.
    $ownedShutdownFailed = $false
    foreach ($process in $processes) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue; $process.WaitForExit(10000) | Out-Null }
        } catch {
            if (-not $InventoryRecoveryConsumeDeath) { throw }
            $ownedShutdownFailed = $true
        } finally {
            # Death-mode cleanup still needs the live process handles to prove
            # every owned writer exited. Dispose them only after that check.
            if (-not $InventoryRecoveryConsumeDeath) { $process.Dispose() }
        }
    }
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
    if ($InventoryRecoveryConsumeDeath) {
        # All owned UE/proxy/backend writers are closed before touching our two
        # explicitly allocated temporary roots. Keep the small result and logs.
        try {
            try {
                if ($consumeBackendSnapshot) {
                    $snapshotUnchanged = $false
                    try {
                        Assert-InventoryConsumeBackendSnapshot $projectRoot $consumeBackendSnapshot
                        $snapshotUnchanged = $true
                    } finally {
                        @{inputsUnchanged=$snapshotUnchanged;files=$consumeBackendSnapshot.Files.Count} | ConvertTo-Json |
                            Set-Content -LiteralPath (Join-Path $runRoot 'backend-source-closed.json') -Encoding UTF8
                        $resultPath = Join-Path $runRoot 'result.json'
                        if (-not $snapshotUnchanged -and (Test-Path -LiteralPath $resultPath)) {
                            $result = Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
                            $result.Passed = $false
                            $result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resultPath -Encoding UTF8
                        }
                    }
                }
            } finally { Clear-InventoryConsumeDeathArtifacts -OwnedShutdownSucceeded (-not $ownedShutdownFailed) }
        }
        finally { foreach ($process in $processes) { $process.Dispose() } }
    }
    if ($VerifyRegisteredDungeonDefinition -or $VerifyServerDungeonContent) {
        # Only now are all owned log writers closed. Keep failure attempts as well.
        $finalSourcesMatch=$true
        foreach($path in $resultSourcePaths) {
            $absolute=Join-Path $projectRoot $path
            if(-not(Test-Path -LiteralPath $absolute) -or (Get-FileHash -LiteralPath $absolute).Hash -cne $resultSourceHashes[$path]) { $finalSourcesMatch=$false }
        }
        $finalModuleHash=if(Test-Path -LiteralPath $unrealModulePath) { (Get-FileHash -LiteralPath $unrealModulePath).Hash } else { $null }
        $closedPassed=$succeeded -and $finalSourcesMatch -and $finalModuleHash -ceq $unrealModuleHash
        [ordered]@{passed=$closedPassed;registeredDefaultDefinition=[bool]$VerifyRegisteredDungeonDefinition;
            serverContentMapping=[bool]$VerifyServerDungeonContent;sourcesUnchanged=$finalSourcesMatch;
            moduleSHA256=$unrealModuleHash;closedModuleSHA256=$finalModuleHash;sourceHashes=$resultSourceHashes;
            closedLogHashes=@(@($logs.ToArray())+@($backendOut,$backendErr) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
                ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} });
            scope=$(if($VerifyServerDungeonContent){'Trusted server ID/map/reward snapshot and authenticated UE two-client lobby/admission, scripted GAS victory, PostgreSQL settlement and return; not automatic allocation, natural combat, Steam or Shipping'}else{'Saved default definition and authenticated UE lobby Waiting-session creation/join only; no server assignment, combat, reward settlement, Steam or Shipping'})} |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot $(if($VerifyServerDungeonContent){'server-content.proof.json'}else{'registered-definition.proof.json'})) -Encoding UTF8
        if($succeeded -and -not $closedPassed) { throw 'Registered definition provenance changed before closed-log proof.' }
    }
    if (-not $succeeded) { Write-Host "FAILED (artifacts retained): $runRoot" }
}
