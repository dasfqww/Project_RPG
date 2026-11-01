[CmdletBinding()]
param(
    [string] $EngineRoot = 'D:\UE_5.8',
    [ValidateRange(30, 600)][int] $TimeoutSeconds = 180,
    [switch] $GameplayRevocationOnly,
    [switch] $EjectionOnly,
    [switch] $MembershipEjectionOnly,
    [switch] $ConnectionRecoveryOnly,
    [switch] $SkipSDKSetup
)

$ErrorActionPreference = 'Stop'
$selectedModes = @(@($GameplayRevocationOnly, $EjectionOnly, $MembershipEjectionOnly, $ConnectionRecoveryOnly) | Where-Object { $_ })
if ($selectedModes.Count -gt 1) { throw 'Choose only one focused test mode.' }
$fullDungeonFlow = -not ($GameplayRevocationOnly -or $EjectionOnly -or $MembershipEjectionOnly -or $ConnectionRecoveryOnly)
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
if (-not (Test-Path -LiteralPath $editorPath)) { throw "Missing editor: $editorPath" }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'))) {
    throw 'Build Project_RPGEditor Win64 Development before running this test.'
}
$runRoot = Join-Path $projectRoot ('Saved\Tests\DungeonFlow\Automation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$process = $null
$previousSDK = $env:UE_SKIP_UBT_SDK_SETUP
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$sourcePaths = @(
    'Source/Project_RPG/Public/Security/RPGDungeonGameplayAuthority.h',
    'Source/Project_RPG/Private/Security/RPGDungeonGameplayAuthority.cpp',
    'Source/Project_RPG/Public/Component/RPGPlayerMovementComponent.h',
    'Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp',
    'Source/Project_RPG/Public/Character/RPGBaseCharacter.h',
    'Source/Project_RPG/Private/Character/RPGBaseCharacter.cpp',
    'Source/Project_RPG/Public/Character/RPGPlayer.h',
    'Source/Project_RPG/Private/Character/RPGPlayer.cpp',
    'Source/Project_RPG/Public/Component/RPGAbilitySystemComponent.h',
    'Source/Project_RPG/Private/Component/RPGAbilitySystemComponent.cpp',
    'Source/Project_RPG/Private/Component/RPGAbilitySystemTargetDataSecurity.cpp',
    'Source/Project_RPG/Private/Component/RPGSecurityValidationComponent.cpp',
    'Source/Project_RPG/Private/Component/Combat/PawnCombatComponent.cpp',
    'Source/Project_RPG/Private/Combat/HitQuery/RPGHitQuerySubsystem.cpp',
    'Source/Project_RPG/Private/Security/RPGDamageExecutionGuard.cpp',
    'Source/Project_RPG/Private/GameplayEffect/GE_ExecCalc_TakeDamage.cpp',
    'Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp',
    'Source/Project_RPG/Private/AI/RPGBossThreatComponent.cpp',
    'Source/Project_RPG/Public/GameMode/RPGBossBattleGameMode.h',
    'Source/Project_RPG/Private/GameMode/RPGBossBattleGameMode.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonGameplayRevocationTestFixture.h',
    'Source/Project_RPG/Private/Tests/RPGDungeonGameplayRevocationTestFixture.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonGameplayRevocationTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossTestFixture.h',
    'Source/Project_RPG/Private/Tests/RPGBossTestFixture.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossBattleRetryTestFixture.h',
    'Source/Project_RPG/Private/Tests/RPGBossBattleRetryTestFixture.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossDamageTestFixture.h',
    'Source/Project_RPG/Private/Tests/RPGBossDamageTestFixture.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossThreatTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossDamageTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossBattleRetryTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGBossNPCStartupTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGEquipmentCombatTests.cpp',
    'Source/Project_RPG/Public/Online/RPGDungeonAbandonment.h',
    'Source/Project_RPG/Private/Online/RPGDungeonAbandonment.cpp',
    'Source/Project_RPG/Public/Online/RPGDungeonMembershipSnapshot.h',
    'Source/Project_RPG/Private/Online/RPGDungeonMembershipSnapshot.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonMembershipSnapshotTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonMembershipMonitorTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonAuthorityRevocationTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonEjectionTestFixture.h',
    'Source/Project_RPG/Private/Tests/RPGDungeonEjectionTestFixture.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonEjectionTests.cpp',
    'Source/Project_RPG/Public/Player/RPGPlayerState.h',
    'Source/Project_RPG/Private/Player/RPGPlayerState.cpp',
    'Source/Project_RPG/Public/Component/RPGItemCommandComponent.h',
    'Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp',
    'Source/Project_RPG/Private/Component/RPGPickupGrantComponent.cpp',
    'Source/Project_RPG/Private/Tests/RPGPickupGrantRuntimeTests.cpp',
    'Source/Project_RPG/Public/Component/RPGInventoryProjectionComponent.h',
    'Source/Project_RPG/Private/Component/RPGInventoryProjectionComponent.cpp',
    'Source/Project_RPG/Public/Security/RPGSecurityTelemetrySubsystem.h',
    'Source/Project_RPG/Private/Security/RPGSecurityTelemetrySubsystem.cpp',
    'Source/Project_RPG/Public/Security/RPGSecurityTelemetryTypes.h',
    'Source/Project_RPG/Private/Security/RPGSecurityTelemetryTypes.cpp',
    'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
    'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
    'Source/Project_RPG/Private/GameMode/RPGGameModePendingAdmission.cpp',
    'Source/Project_RPG/Public/Controller/RPGPlayerController.h',
    'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonClientFlowTests.cpp',
    'Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h',
    'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
    'Source/Project_RPG/Public/Manager/HttpWebManager.h',
    'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
    'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyWidget.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyNavigation.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyNavigation.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyNavigationTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyFocusRecoveryTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyFocusHandoffTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGOnlineLobbyBackInputTests.cpp',
    'Source/Project_RPG/Public/UI/Content/RPGDungeonResultActionsWidget.h',
    'Source/Project_RPG/Private/UI/Content/RPGDungeonResultActionsWidget.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonResultNavigationTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonAbandonmentTests.cpp',
    'Scripts/Tests/Run-DungeonClientFlowAutomationTest.ps1'
)
if ($ConnectionRecoveryOnly) {
    $sourcePaths = @(
        'Source/Project_RPG/Project_RPG.Build.cs',
        'Source/Project_RPG/Private/UI/Option/RPGOptionMenu.cpp',
        'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
        'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
        'Source/Project_RPG/Private/GameMode/RPGGameModePendingAdmission.cpp',
        'Source/Project_RPG/Public/Controller/RPGPlayerController.h',
        'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp',
        'Source/Project_RPG/Private/Tests/RPGDungeonEjectionTestFixture.h',
        'Source/Project_RPG/Private/Tests/RPGDungeonEjectionTestFixture.cpp',
        'Source/Project_RPG/Private/Tests/RPGDungeonEjectionTests.cpp',
        'Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h',
        'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
        'Source/Project_RPG/Public/Manager/HttpWebManager.h',
        'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
        'Source/Project_RPG/Public/Online/RPGDungeonDisconnectionJournal.h',
        'Source/Project_RPG/Private/Online/RPGDungeonDisconnectionJournal.cpp',
        'Source/Project_RPG/Public/Online/RPGDungeonConsumeJournal.h',
        'Source/Project_RPG/Private/Online/RPGDungeonConsumeJournal.cpp',
        'Source/Project_RPG/Private/Tests/RPGDungeonConsumeJournalTests.cpp',
        'Source/Project_RPG/Public/Online/RPGJoinTicketAdmission.h',
        'Source/Project_RPG/Private/Online/RPGJoinTicketAdmission.cpp',
        'Source/Project_RPG/Private/Tests/RPGJoinTicketAdmissionTests.cpp',
        'Source/Project_RPG/Private/Tests/RPGDungeonDisconnectionOrderingTests.cpp',
        'Source/Project_RPG/Private/Tests/RPGDungeonClientFlowTests.cpp',
        'Scripts/Tests/Run-DungeonClientFlowAutomationTest.ps1'
    )
}
$sourceHashes = @($sourcePaths | ForEach-Object {
    [ordered]@{ path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash }
})
try {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    $focusedTests = @(
        'ProjectRPG.Online.DungeonAuthorityRevocation.Gameplay',
        'ProjectRPG.Online.DungeonAuthorityRevocation.LateDamage',
        'ProjectRPG.Online.DungeonAuthorityRevocation.ThreatAndParticipation',
        'ProjectRPG.Online.DungeonAuthorityRevocation.Identity',
        'ProjectRPG.Boss.Threat', 'ProjectRPG.Boss.Damage', 'ProjectRPG.Boss.BattleRetry',
        'ProjectRPG.Boss.NPCStartup', 'ProjectRPG.Boss.WeaponSweep',
        'ProjectRPG.Item.EquipmentCombat.SwitchStopsOldCollisionAndStaleEvents',
        'ProjectRPG.Item.EquipmentCombat.ClientCannotEnableOrDispatchWeaponHits'
    )
    if ($EjectionOnly) {
        $focusedTests = @(
            'ProjectRPG.Online.DungeonEjection.Lifecycle',
            'ProjectRPG.Online.DungeonEjection.Guards',
            'ProjectRPG.Online.DungeonEjection.Reentry',
            'ProjectRPG.Online.DungeonMembershipMonitor',
            'ProjectRPG.Online.DungeonAuthorityRevocation.Gameplay'
        )
    }
    if ($MembershipEjectionOnly) {
        $focusedTests = @(
            'ProjectRPG.Online.DungeonEjection.PendingAdmissionRevocation',
            'ProjectRPG.Online.DungeonEjection.PendingAdmissionRevocationGuards',
            'ProjectRPG.Online.DungeonMembershipSnapshotCodec',
            'ProjectRPG.Online.DungeonEjection.DisconnectionExpiry',
            'ProjectRPG.Online.DungeonEjection.DisconnectionExpiryGuards',
            'ProjectRPG.Online.DungeonEjection.MembershipCompletion',
            'ProjectRPG.Online.DungeonEjection.MembershipCachedSweep',
            'ProjectRPG.Online.DungeonEjection.MembershipScopeReentry',
            'ProjectRPG.Online.DungeonMembershipMonitor',
            'ProjectRPG.Online.DungeonMembershipTransport'
        )
    }
    if ($ConnectionRecoveryOnly) {
        $focusedTests = @(
            'ProjectRPG.Online.DungeonReturn',
            'ProjectRPG.Online.DungeonRecovery',
            'ProjectRPG.Online.DungeonPendingConnectionRecovery',
            'ProjectRPG.Online.DungeonConnectionCleanup',
            'ProjectRPG.Online.DungeonPreLoginConnectionRecovery',
            'ProjectRPG.Online.DungeonPlannedDisconnect',
            'ProjectRPG.Online.JoinTicketAdmissionCodec',
            'ProjectRPG.Online.DungeonDisconnectionJournal',
            'ProjectRPG.Online.DungeonDisconnectionOrdering',
            'ProjectRPG.Online.DungeonInFlightMutationCompletion',
            'ProjectRPG.Online.DungeonConsumeResponseRecovery',
            'ProjectRPG.Online.DungeonConsumeEvidenceRecovery',
            'ProjectRPG.Online.DungeonConsumeLossJournal',
            'ProjectRPG.Online.DungeonConsumeObservedLossRestart',
            'ProjectRPG.Online.DungeonConsumeCancellationRecovery',
            'ProjectRPG.Online.DungeonConsumeObservationWriteRecovery',
            'ProjectRPG.Online.DungeonDisconnectionWriteRecovery',
            'ProjectRPG.Online.DungeonJournalRestoreRecovery',
            'ProjectRPG.Online.DungeonRecoveryDiagnostics'
        )
    }
    $testFilter = if (-not $fullDungeonFlow) { $focusedTests -join '+' } else { 'ProjectRPG.Online.Dungeon' }
    $arguments = @(
        ('"' + $projectPath + '"'), '/Engine/Maps/Entry',
        ('-ExecCmds="Automation RunTests ' + $testFilter + '"'),
        '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-DisablePython',
        '-NoLiveCoding', '-DDC-ForceMemoryCache', ('-UserDir="' + (Join-Path $runRoot 'User') + '"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $processHandle = $process.Handle
    Write-Host "Started dungeon-flow automation (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Automation timed out; inspect $logPath" }
        Start-Sleep -Milliseconds 250
    }
    $process.WaitForExit()
    if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode); inspect $logPath" }
    if (-not (Test-Path -LiteralPath $logPath)) { throw "Automation log missing: $logPath" }
    $testPaths = @(
        'ProjectRPG.Online.DungeonReturn', 'ProjectRPG.Online.DungeonRecovery',
        'ProjectRPG.Online.DungeonConnectionCleanup', 'ProjectRPG.Online.DungeonPlannedDisconnect',
        'ProjectRPG.Online.DungeonPreLoginConnectionRecovery',
        'ProjectRPG.Online.DungeonConsumeResponseRecovery',
        'ProjectRPG.Online.DungeonConsumeEvidenceRecovery',
        'ProjectRPG.Online.DungeonConsumeLossJournal',
        'ProjectRPG.Online.DungeonConsumeObservedLossRestart',
        'ProjectRPG.Online.DungeonConsumeCancellationRecovery',
        'ProjectRPG.Online.DungeonConsumeObservationWriteRecovery',
        'ProjectRPG.Online.DungeonDisconnectionWriteRecovery',
        'ProjectRPG.Online.DungeonJournalRestoreRecovery',
        'ProjectRPG.Online.DungeonRecoveryDiagnostics',
        'ProjectRPG.Online.DungeonPendingConnectionRecovery', 'ProjectRPG.Online.DungeonResult',
        'ProjectRPG.Online.DungeonResultWidget', 'ProjectRPG.Online.DungeonReward.ConfiguredAssetsAreValid',
        'ProjectRPG.Online.DungeonResultNavigation.NativeControls',
        'ProjectRPG.Online.DungeonLobbyPolicy', 'ProjectRPG.Online.DungeonLobbyWidget',
        'ProjectRPG.Online.DungeonLobbyNavigation.OrderAndAvailability',
        'ProjectRPG.Online.DungeonLobbyNavigation.ModalIsolation',
        'ProjectRPG.Online.DungeonLobbyNavigation.NativeConnection',
        'ProjectRPG.Online.DungeonLobbyFocusRecovery.StaleChildAndFallback',
        'ProjectRPG.Online.DungeonLobbyFocusRecovery.InputAndForeignScope',
        'ProjectRPG.Online.DungeonLobbyFocusRecovery.ModalPriority',
        'ProjectRPG.Online.DungeonLobbyFocusHandoff.LocalViewport',
        'ProjectRPG.Online.DungeonLobbyBackInput.Preparation',
        'ProjectRPG.Online.DungeonLobbyCharacterName',
        'ProjectRPG.Online.DungeonAuthentication', 'ProjectRPG.Online.DungeonAuthenticationTransport',
        'ProjectRPG.Online.DungeonAuthenticationClock',
        'ProjectRPG.Online.DungeonAbandonmentCodec', 'ProjectRPG.Online.DungeonAbandonmentFlow',
        'ProjectRPG.Online.DungeonAbandonmentRetryPolicy', 'ProjectRPG.Online.DungeonAbandonmentTransport',
        'ProjectRPG.Online.DungeonAbandonmentConfirmation',
        'ProjectRPG.Online.DungeonMembershipSnapshotCodec',
        'ProjectRPG.Online.DungeonMembershipMonitor', 'ProjectRPG.Online.DungeonMembershipTransport',
        'ProjectRPG.Online.DungeonEjection.Lifecycle', 'ProjectRPG.Online.DungeonEjection.Guards',
        'ProjectRPG.Online.DungeonEjection.DisconnectionExpiry', 'ProjectRPG.Online.DungeonEjection.DisconnectionExpiryGuards',
        'ProjectRPG.Online.DungeonEjection.Reentry',
        'ProjectRPG.Online.DungeonEjection.MembershipCompletion',
        'ProjectRPG.Online.DungeonEjection.MembershipCachedSweep',
        'ProjectRPG.Online.DungeonEjection.MembershipScopeReentry',
        'ProjectRPG.Online.DungeonEjection.PendingAdmissionRevocation',
        'ProjectRPG.Online.DungeonEjection.PendingAdmissionRevocationGuards',
        'ProjectRPG.Online.DungeonAuthorityRevocation.Identity', 'ProjectRPG.Online.DungeonAuthorityRevocation.Items',
        'ProjectRPG.Online.DungeonAuthorityRevocation.Wallet', 'ProjectRPG.Online.DungeonAuthorityRevocation.Telemetry',
        'ProjectRPG.Online.DungeonAuthorityRevocation.Gameplay', 'ProjectRPG.Online.DungeonAuthorityRevocation.LateDamage',
        'ProjectRPG.Online.DungeonAuthorityRevocation.ThreatAndParticipation',
        'ProjectRPG.Online.DungeonResultAuthentication',
        'ProjectRPG.Online.DungeonReward.DefinitionBuildsPersistentPayload',
        'ProjectRPG.Online.DungeonReward.DefinitionRejectsInvalidContent'
    )
    if (-not $fullDungeonFlow) { $testPaths = $focusedTests }
    foreach ($testPath in $testPaths) {
        $testName = $testPath.Split('.')[-1]
        $marker = "Test Completed. Result={Success} Name={$testName} Path={$testPath}"
        if (-not (Select-String -LiteralPath $logPath -SimpleMatch $marker -Quiet)) {
            throw "Missing successful result for $testPath; inspect $logPath"
        }
    }
    if (Select-String -LiteralPath $logPath -Pattern 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet) {
        throw "Automation/runtime failure; inspect $logPath"
    }
    $log = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $results = [regex]::Matches($log, 'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>ProjectRPG\.[^}]+)\}')
    if ($results.Count -eq 0 -or @($results | Where-Object { $_.Groups['result'].Value -ne 'Success' }).Count -gt 0) {
        throw "Missing or unsuccessful dungeon results; inspect $logPath"
    }
    if (-not $fullDungeonFlow -and ($results.Count -ne $focusedTests.Count -or
        @($results | Where-Object { $_.Groups['path'].Value -notin $focusedTests }).Count -ne 0)) {
        throw "Unexpected focused test coverage; inspect $logPath"
    }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) { throw 'Editor module changed during dungeon automation.' }
    foreach ($entry in $sourceHashes) {
        if ($entry.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.path) -Algorithm SHA256).Hash) {
            throw "Source changed during dungeon automation: $($entry.path)"
        }
    }
    $proofPath = Join-Path $runRoot 'DungeonFlow.proof.json'
    [ordered]@{ schemaVersion=1; editorModuleSHA256=$moduleHash; gameplayRevocationOnly=[bool]$GameplayRevocationOnly; ejectionOnly=[bool]$EjectionOnly;
        membershipEjectionOnly=[bool]$MembershipEjectionOnly;
        connectionRecoveryOnly=[bool]$ConnectionRecoveryOnly;
        passedTests=@($results | ForEach-Object { $_.Groups['path'].Value }); logPath=$logPath;
        logSHA256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash; sourceHashes=$sourceHashes;
        scope=[ordered]@{ isolatedDungeonFlow=$fullDungeonFlow; nativeAbandonmentCallbacksAndConfirmation=$fullDungeonFlow;
            membershipSnapshotCodec=($fullDungeonFlow -or [bool]$MembershipEjectionOnly); nativeMembershipRequestCallbacks=($fullDungeonFlow -or [bool]$MembershipEjectionOnly);
            membershipMonitorAndAdmissionScopeGuard=($fullDungeonFlow -or [bool]$EjectionOnly -or [bool]$MembershipEjectionOnly); actualMembershipSnapshotHttp=$false;
            nativeScopedIdentityRevocation=(-not $ConnectionRecoveryOnly); nativeScopedIdentityAndEconomyRevocation=$fullDungeonFlow;
            nativeTelemetryQuarantine=$fullDungeonFlow;
            nativeGameplayQuarantine=(-not ($MembershipEjectionOnly -or $ConnectionRecoveryOnly)); pendingDamageReceiptRevocation=(-not ($EjectionOnly -or $MembershipEjectionOnly -or $ConnectionRecoveryOnly)); remainingPartyThreatAndSurvival=(-not ($EjectionOnly -or $MembershipEjectionOnly -or $ConnectionRecoveryOnly));
            nativeConnectionCloseAndLogout=($fullDungeonFlow -or [bool]$EjectionOnly -or [bool]$MembershipEjectionOnly -or [bool]$ConnectionRecoveryOnly);
            nativeUnexpectedDisconnectClassification=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativePreLoginConnectionRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeDisconnectionMutationOrdering=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeInFlightDungeonMutationCompletion=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeConsumeResponseRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeConsumeEvidenceRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeConsumeObservedLossJournalRestart=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeConsumeCancellationProofAndCleanup=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeConsumeObservationWriteRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeDisconnectionOutboxWriteRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeDungeonJournalStartupRestoreRetry=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativeDungeonRecoveryReadOnlyDiagnostics=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            nativePlannedCloseAndBoundedLocalRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            actualDisconnectReportingHttp=$false;
            nativeRawQueuedMovementBeforeDecode=($fullDungeonFlow -or [bool]$EjectionOnly);
            actualSocketConnection=$false; actualClientDisconnectAndReturn=$false;
            queuedMovementHandlerNativeOnly=(-not $ConnectionRecoveryOnly); actualQueuedMovementNetworkDrain=$false;
            nativePendingConnectionRecovery=($fullDungeonFlow -or [bool]$ConnectionRecoveryOnly);
            actualTelemetryQuarantinePersistence=$false; automaticConnectedMemberRevocation=$false;
            nativeAutomaticMembershipCompletionAndCachedSweep=[bool]$MembershipEjectionOnly;
            nativePendingAdmissionReceiptRejection=($fullDungeonFlow -or [bool]$MembershipEjectionOnly);
            actualAutomaticMembershipPollingNetworkEjection=$false;
            actualAbandonmentHttp=$false; serverEjection=$false; networkReplication=$false; backendAbandonment=$false; shipping=$false }
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: $($results.Count) tests (gameplay-revocation-only=$([bool]$GameplayRevocationOnly), ejection-only=$([bool]$EjectionOnly), membership-ejection-only=$([bool]$MembershipEjectionOnly), connection-recovery-only=$([bool]$ConnectionRecoveryOnly)). Log: $logPath; proof: $proofPath"
}
finally {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = $previousSDK }
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
