[CmdletBinding()]
param(
    [string] $EngineRoot = 'D:\UE_5.8',
    [ValidateRange(30, 900)][int] $TimeoutSeconds = 300,
    [switch] $SkipSDKSetup,
    [switch] $PickupJournalOnly,
    [switch] $PickupRuntimeOnly,
    [switch] $QuickSlotOnly,
    [switch] $LocalAbilityStartupOnly,
    [switch] $PresentationOnly,
    [switch] $StackMergeOnly,
    [switch] $StackSplitOnly,
    [switch] $SplitQuantityOnly,
    [switch] $InventoryRecoveryOnly,
    [switch] $ConsumeRecoveryOnly
)

$ErrorActionPreference = 'Stop'
if (@(@($PickupJournalOnly, $PickupRuntimeOnly, $QuickSlotOnly, $LocalAbilityStartupOnly, $PresentationOnly, $StackMergeOnly, $StackSplitOnly, $SplitQuantityOnly, $InventoryRecoveryOnly, $ConsumeRecoveryOnly) | Where-Object { $_ }).Count -gt 1) {
    throw 'Select only one focused test scope.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$inventoryModulePath = Join-Path $projectRoot 'Plugins/RPGInventory/Binaries/Win64/UnrealEditor-RPGInventoryRuntime.dll'
if (-not (Test-Path -LiteralPath $editorPath) -or -not (Test-Path -LiteralPath $modulePath) -or -not (Test-Path -LiteralPath $inventoryModulePath)) {
    throw 'Build Project_RPGEditor Win64 Development before running item automation.'
}
$testFiles = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source\Project_RPG\Private\Tests') -Filter '*.cpp')
$testFiles += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Tests') -Filter '*.cpp')
if ($PickupJournalOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @('RPGPickupGrantJournalTests.cpp','RPGItemBackendGatewayTests.cpp','RPGPickupGrantRuntimeTests.cpp') })
}
if ($PickupRuntimeOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @('RPGPickupGrantRuntimeTests.cpp','RPGItemPickupGrantPlannerTests.cpp') })
}
if ($QuickSlotOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @('RPGQuickSlotProjectionTests.cpp', 'RPGQuickSlotClickTests.cpp') })
}
if ($LocalAbilityStartupOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -eq 'RPGLocalAbilityStartupTests.cpp' })
}
if ($PresentationOnly) {
    # Definition text and the existing cell actions share this fixture.
    $testFiles = @($testFiles | Where-Object { $_.Name -eq 'RPGItemProjectionInteractionTests.cpp' })
}
if ($StackMergeOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @(
        'RPGItemStackMergeTests.cpp', 'RPGItemAsyncCommandOrchestratorTests.cpp',
        'RPGItemProjectionInteractionTests.cpp') })
}
if ($StackSplitOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @('RPGItemStackSplitTests.cpp',
        'RPGItemStackMergeTests.cpp', 'RPGItemAsyncCommandOrchestratorTests.cpp', 'RPGQuickSlotClickTests.cpp') })
}
if ($SplitQuantityOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @('RPGInventorySplitQuantityTests.cpp',
        'RPGItemStackSplitTests.cpp', 'RPGItemProjectionInteractionTests.cpp', 'RPGQuickSlotClickTests.cpp') })
}
if ($InventoryRecoveryOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -in @('RPGInventoryCommandRecoveryTests.cpp',
        'RPGItemAsyncCommandOrchestratorTests.cpp', 'RPGItemStackMergeTests.cpp', 'RPGItemStackSplitTests.cpp',
        'RPGItemBackendGatewayTests.cpp', 'RPGItemMailTests.cpp', 'RPGConsumeRecoveryTests.cpp') })
}
if ($ConsumeRecoveryOnly) {
    $testFiles = @($testFiles | Where-Object { $_.Name -eq 'RPGConsumeRecoveryTests.cpp' })
}
$testCommands = if ($PickupJournalOnly) {
    'Automation RunTests ^ProjectRPG.Item.PickupJournal.+^ProjectRPG.Item.Backend.+^ProjectRPG.Item.PickupGrant.Runtime'
}
else { 'Automation RunTests ProjectRPG.Item.' }
$requiredTests = @($testFiles | ForEach-Object {
    [regex]::Matches([IO.File]::ReadAllText($_.FullName), '"(ProjectRPG\.Item\.[^"]+)"') |
        ForEach-Object { $_.Groups[1].Value }
} | Sort-Object -Unique)
if ($requiredTests.Count -eq 0) { throw 'No native item tests discovered in the source.' }
if ($PickupRuntimeOnly -or $QuickSlotOnly -or $LocalAbilityStartupOnly -or $PresentationOnly -or $StackMergeOnly -or $StackSplitOnly -or $SplitQuantityOnly -or $InventoryRecoveryOnly -or $ConsumeRecoveryOnly) {
    # The startup regressions share the QuickSlot prefix, but each focused run
    # must select exactly its source-derived tests, not every descendant.
    $testCommands = 'Automation RunTests ' + (($requiredTests | ForEach-Object { '^' + $_ + '$' }) -join '+')
}
$sourcePaths = @(
    'Source/Project_RPG/Public/Player/RPGPlayerState.h',
    'Source/Project_RPG/Private/Player/RPGPlayerState.cpp',
    'Source/Project_RPG/Public/Component/RPGItemCommandComponent.h',
    'Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp',
    'Source/Project_RPG/Private/Component/RPGPickupGrantComponent.cpp',
    'Source/Project_RPG/Public/Component/RPGPickupGrantComponent.h',
    'Source/Project_RPG/Public/Character/RPGNonPlayerCharacter.h',
    'Source/Project_RPG/Private/Character/RPGNonPlayerCharacter.cpp',
    'Source/Project_RPG/Public/Ability/NPC/RPGNPCDropItemAbility.h',
    'Source/Project_RPG/Private/Ability/NPC/RPGNPCDropItemAbility.cpp',
    'Source/Project_RPG/Public/Item/PickUp/RPGPickUpBase.h',
    'Source/Project_RPG/Private/Item/PickUp/RPGPickUpBase.cpp',
    'Source/Project_RPG/Public/Component/RPGInventoryProjectionComponent.h',
    'Source/Project_RPG/Private/Component/RPGInventoryProjectionComponent.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGPickupGrantJournal.h',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGPickupGrantJournal.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendSubsystem.h',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendSubsystem.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendGateway.h',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendGateway.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendGatewayPickup.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendJsonCodec.h',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendJsonCodec.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGPickupGrantReceiptPolicy.cpp',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGPickupGrantReceiptPolicy.h',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendTransport.h',
    'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendTransport.cpp',
    'Scripts/Tests/Run-ItemAutomationTest.ps1'
)
$sourcePaths += @($testFiles | ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) })
if ($QuickSlotOnly) {
    $sourcePaths = @(
        'Source/Project_RPG/Public/Controller/RPGPlayerController.h',
        'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp',
        'Source/Project_RPG/Public/UI/RPGQuickSlotWidget.h',
        'Source/Project_RPG/Private/UI/RPGQuickSlotWidget.cpp',
        'Source/Project_RPG/Public/UI/RPGWidgetBase.h',
        'Source/Project_RPG/Private/UI/RPGWidgetBase.cpp',
        'Source/Project_RPG/Public/Component/UI/QuickSlotComponent.h',
        'Source/Project_RPG/Private/Component/UI/QuickSlotComponent.cpp',
        'Source/Project_RPG/Public/Component/RPGItemCommandComponent.h',
        'Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp',
        'Source/Project_RPG/Public/Component/RPGInventoryProjectionComponent.h',
        'Source/Project_RPG/Private/Component/RPGInventoryProjectionComponent.cpp',
        'Source/Project_RPG/Public/UI/MVVM/RPGInventoryProjectionViewModel.h',
        'Source/Project_RPG/Private/UI/MVVM/RPGInventoryProjectionViewModel.cpp',
        'Scripts/Tests/Run-ItemAutomationTest.ps1'
    ) + @($testFiles | ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) })
}
if ($LocalAbilityStartupOnly) {
    $sourcePaths = @(
        'Source/Project_RPG/Public/Component/RPGAbilitySystemComponent.h',
        'Source/Project_RPG/Private/Component/RPGAbilitySystemComponent.cpp',
        'Source/Project_RPG/Public/Character/RPGPlayer.h',
        'Source/Project_RPG/Private/Character/RPGPlayer.cpp',
        'Source/Project_RPG/Public/Ability/RPGGameplayAbility.h',
        'Source/Project_RPG/Private/Ability/RPGGameplayAbility.cpp',
        'Scripts/Tests/Run-ItemAutomationTest.ps1'
    ) + @($testFiles | ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) })
}
if ($PresentationOnly -or $StackMergeOnly -or $StackSplitOnly -or $SplitQuantityOnly -or $InventoryRecoveryOnly -or $ConsumeRecoveryOnly) {
    $sourcePaths = @(
        'Source/Project_RPG/Public/UI/MVVM/RPGInventoryProjectionViewModel.h',
        'Source/Project_RPG/Private/UI/MVVM/RPGInventoryProjectionViewModel.cpp',
        'Source/Project_RPG/Private/UI/Inventory/Projection/RPGInventoryProjectionWidget.cpp',
        'Source/Project_RPG/Private/UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Definition/RPGItemDefinitionCatalog.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Definition/RPGItemDefinitionCatalog.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Projection/RPGInventoryProjectionMapper.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Projection/RPGInventoryProjectionMapper.cpp',
        'Source/Project_RPG/Public/Component/RPGInventoryProjectionComponent.h',
        'Source/Project_RPG/Private/Component/RPGInventoryProjectionComponent.cpp',
        'Source/Project_RPG/Public/Component/RPGItemCommandComponent.h',
        'Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp',
        'Scripts/Tests/Run-ItemAutomationTest.ps1'
    ) + @($testFiles | ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) })
}
if ($StackMergeOnly -or $StackSplitOnly -or $SplitQuantityOnly -or $InventoryRecoveryOnly -or $ConsumeRecoveryOnly) {
    $sourcePaths += @(
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Policy/RPGItemStackPolicy.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Policy/RPGItemStackPolicy.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemActionCommandPlanner.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemActionCommandPlanner.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemAsyncCommandOrchestrator.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemAsyncCommandOrchestrator.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemTransactionTypes.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Persistence/RPGItemRecord.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Persistence/RPGItemRecord.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Persistence/RPGInMemoryItemRepository.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Projection/RPGInventoryProjectionStore.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Projection/RPGInventoryProjectionStore.cpp'
    )
}
if ($StackSplitOnly -or $SplitQuantityOnly -or $InventoryRecoveryOnly -or $ConsumeRecoveryOnly) {
    $sourcePaths += @(
        'Source/Project_RPG/Public/UI/Inventory/Projection/RPGInventoryProjectionWidget.h',
        'Source/Project_RPG/Private/UI/RPGConfirmationInput.h',
        'Source/Project_RPG/Public/UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h',
        'Source/Project_RPG/Public/UI/Inventory/Projection/RPGItemIdDragDropOperation.h',
        'Source/Project_RPG/Private/UI/RPGQuickSlotWidget.cpp',
        'Source/Project_RPG/Public/UI/RPGQuickSlotWidget.h',
        'Source/Project_RPG/Private/Component/UI/QuickSlotComponent.cpp',
        'Source/Project_RPG/Public/Component/UI/QuickSlotComponent.h'
    )
}
if ($SplitQuantityOnly) {
    $sourcePaths += 'Content/Blueprints/UI/Inventory/Projection/WBP_InventoryProjection.uasset'
}
if ($InventoryRecoveryOnly -or $ConsumeRecoveryOnly) {
    $sourcePaths += @('Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendJsonCodec.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendJsonCodec.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendGateway.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendGateway.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendTypes.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendTransport.h',
        'Source/Project_RPG/Public/Player/RPGPlayerState.h',
        'Source/Project_RPG/Private/Player/RPGPlayerState.cpp')
	$sourcePaths += @('Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGInventoryCommandJournal.h',
		'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGInventoryCommandJournal.cpp',
		'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Backend/RPGItemBackendSubsystem.h',
		'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Backend/RPGItemBackendSubsystem.cpp')
    $sourcePaths += @('Source/Project_RPG/Private/Tests/RPGConsumeRecoveryTestEffect.h',
        'Source/Project_RPG/Private/Tests/RPGConsumeRecoveryTestEffect.cpp',
        'Source/Project_RPG/Private/Tests/RPGSkillDamageSecurityTestFixture.h',
        'Source/Project_RPG/Private/Tests/RPGSkillDamageSecurityTestFixture.cpp',
        'Source/Project_RPG/Public/Item/Definition/RPGItemDefinitionFragments.h',
        'Source/Project_RPG/Public/Component/RPGAbilitySystemComponent.h',
        'Source/Project_RPG/Private/Component/RPGAbilitySystemComponent.cpp',
        'Source/Project_RPG/Public/Attribute/RPGAttributeSet.h',
        'Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp')
    $sourcePaths += @('Source/Project_RPG/Public/Character/RPGBaseCharacter.h',
        'Source/Project_RPG/Private/Character/RPGBaseCharacter.cpp',
        'Source/Project_RPG/Public/Character/RPGPlayer.h',
        'Source/Project_RPG/Private/Character/RPGPlayer.cpp',
        'Source/Project_RPG/Public/DataAsset/StartUpData/DataAsset_StartUpDataBase.h',
        'Source/Project_RPG/Private/DataAsset/StartUpData/DataAsset_StartUpDataBase.cpp')
}
if ($PickupRuntimeOnly -or $PickupJournalOnly) {
    $sourcePaths += @('Source/Project_RPG/Public/UI/MVVM/RPGInventoryProjectionViewModel.h',
        'Source/Project_RPG/Private/UI/MVVM/RPGInventoryProjectionViewModel.cpp')
}
if ($PickupRuntimeOnly) {
    $sourcePaths += @('Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemPickupGrantPlanner.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemPickupGrantPlanner.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Definition/RPGItemDefinition.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Definition/RPGItemDefinition.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Definition/RPGItemDefinitionCatalog.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Definition/RPGItemDefinitionCatalog.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Persistence/RPGInMemoryItemRepository.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Persistence/RPGInMemoryItemRepository.cpp',
        'Source/Project_RPG/Public/Item/Policy/RPGItemActionPolicy.h',
        'Source/Project_RPG/Private/Item/Policy/RPGItemActionPolicy.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemAsyncCommandOrchestrator.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemAsyncCommandOrchestrator.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGItemActionCommandPlanner.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGItemActionCommandPlanner.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Transaction/RPGInventoryCommandJournal.h',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Private/Item/Transaction/RPGInventoryCommandJournal.cpp',
        'Plugins/RPGInventory/Source/RPGInventoryRuntime/Public/Item/Policy/RPGItemInventoryPolicy.h')
}
$sourcePaths += 'Plugins/RPGInventory/RPGInventory.uplugin'
$sourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Plugins/RPGInventory/Source') -File -Recurse |
    ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1).Replace('\', '/') })
$sourcePaths = @($sourcePaths | Sort-Object -Unique)
$sourceHashes = @($sourcePaths | ForEach-Object {
    [ordered]@{path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash}
})
$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$inventoryModuleHash = (Get-FileHash -LiteralPath $inventoryModulePath -Algorithm SHA256).Hash
$runRoot = Join-Path $projectRoot ('Saved\Tests\Item\Automation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$previousSDK = $env:UE_SKIP_UBT_SDK_SETUP
$process = $null
try {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    $arguments = @(
        ('"' + $projectPath + '"'), '/Engine/Maps/Entry',
        ('-ExecCmds="' + $testCommands + '"'), '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-DisablePython',
        '-NoLiveCoding', '-DDC-ForceMemoryCache', ('-UserDir="' + (Join-Path $runRoot 'User') + '"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $processHandle = $process.Handle
    Write-Host "Started item automation (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Item automation timed out; inspect $logPath" }
        Start-Sleep -Milliseconds 250
    }
    $process.WaitForExit()
    if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0) { throw "Editor exited $($process.ExitCode); inspect $logPath" }
    # Exit is required before finalization; explicitly permit retained/shared
    # Windows handles while reading the completed log, as in the HTTP fixture.
    $logStream = [IO.FileStream]::new($logPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try {
        $logReader = [IO.StreamReader]::new($logStream)
        try { $log = $logReader.ReadToEnd() } finally { $logReader.Dispose() }
    } finally { $logStream.Dispose() }
    foreach ($path in $requiredTests) {
        $name = $path.Split('.')[-1]
        if (-not $log.Contains("Test Completed. Result={Success} Name={$name} Path={$path}")) {
            throw "Missing successful result for $path; inspect $logPath"
        }
    }
    if ($log -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:') {
        throw "Item automation/runtime failure; inspect $logPath"
    }
    $results = [regex]::Matches($log, 'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>ProjectRPG\.Item\.[^}]+)\}')
    if ($results.Count -ne $requiredTests.Count -or @($results | Where-Object { $_.Groups['result'].Value -ne 'Success' }).Count -gt 0) {
        throw 'Unexpected/missing item test results.'
    }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) { throw 'Editor module changed during item automation.' }
    if ($inventoryModuleHash -ne (Get-FileHash -LiteralPath $inventoryModulePath -Algorithm SHA256).Hash) { throw 'Inventory module changed during item automation.' }
    foreach ($entry in $sourceHashes) {
        if ($entry.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.path) -Algorithm SHA256).Hash) {
            throw "Source changed during item automation: $($entry.path)"
        }
    }
    $proofPath = Join-Path $runRoot 'Item.proof.json'
    [ordered]@{schemaVersion=1; editorModuleSHA256=$moduleHash; inventoryModuleSHA256=$inventoryModuleHash; requiredTests=$requiredTests;
        passedTests=@($results | ForEach-Object { $_.Groups['path'].Value }); sourceHashes=$sourceHashes;
        logPath=$logPath; logSHA256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash;
        scope=[ordered]@{nativeItemFixtures=$true; localAbilityStartupMock=[bool]$LocalAbilityStartupOnly;
            pickupGrantRuntimeAndPendingPresentation=[bool]$PickupRuntimeOnly;
            itemPresentationAndInteraction=[bool]$PresentationOnly;
            stackMergeAndDirectRegressions=[bool]$StackMergeOnly;
            stackSplitAndDirectRegressions=[bool]$StackSplitOnly;
            splitQuantitySelectionAndDirectRegressions=[bool]$SplitQuantityOnly;
            inventoryCommandRecoveryAndDirectRegressions=[bool]$InventoryRecoveryOnly;
            consumeRecoveryAndLivingAvatar=[bool]$ConsumeRecoveryOnly;
            authoredInventoryTemplateOnly=[bool]$SplitQuantityOnly;
            localPickupJournalFiles=(@($requiredTests | Where-Object { $_.StartsWith('ProjectRPG.Item.PickupJournal.') }).Count -gt 0);
            localInventoryJournalFiles=[bool]$InventoryRecoveryOnly;
            actualBackendHttp=$false; actualPersistence=$false;
            networkReplication=$false; steam=$false; shipping=$false}
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: $($results.Count) native item tests. Log: $logPath; proof: $proofPath"
}
catch {
    [ordered]@{schemaVersion=1; passed=$false; reason=$_.Exception.Message;
        editorModuleSHA256=$moduleHash; inventoryModuleSHA256=$inventoryModuleHash; requiredTests=$requiredTests; sourceHashes=$sourceHashes;
        logPath=$logPath; ownedLogFrozen=($null -ne $process -and $process.HasExited)
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'Failure.proof.json') -Encoding UTF8
    throw
}
finally {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = $previousSDK }
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
