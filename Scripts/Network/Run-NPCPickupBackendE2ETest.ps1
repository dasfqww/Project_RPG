[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024,65535)][int]$Port = 18836,
    [ValidateRange(60,600)][int]$TimeoutSeconds = 300,
    [ValidateRange(1,4)][int]$ClientCount = 1,
    [switch]$LegacyProtocol,
    [switch]$LosePickupAck,
    [switch]$LosePrepareAck,
    [switch]$SlotConflict,
    [switch]$RevisionConflict,
    [switch]$RestartServerAfterLostAck,
    [string]$BackendUrl = 'http://127.0.0.1:3036',
    [string]$ConnectionString = 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only',
    [switch]$SkipPostgresStart,
    [switch]$NoBackendRestore
)

# Authenticated remote clients. Saved NPC death/GE/ability/pickup; transient
# guaranteed drop row, paused NPC, positioned player/pickup, scripted interaction.
# No saved asset writes, admission bypass, production Steam or natural combat claim.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$baseUrl = $BackendUrl.TrimEnd('/')
$uri = [Uri]$baseUrl
if ($uri.Scheme -ne 'http' -or $uri.Host -ne '127.0.0.1' -or $uri.AbsolutePath -ne '/' -or $uri.Port -eq $Port) { throw 'Use distinct loopback HTTP/UE ports.' }
if ($ConnectionString -cne 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only') { throw 'This fixture is restricted to the existing local development PostgreSQL database.' }
if ($LosePickupAck -and $ClientCount -ne 2) { throw 'Acknowledgement-loss verification requires exactly two authenticated clients.' }
if ($RestartServerAfterLostAck -and (-not $LosePickupAck -or $ClientCount -ne 2)) { throw 'Server restart verification requires -LosePickupAck -ClientCount 2.' }
if ($LosePrepareAck -and ($LegacyProtocol -or $LosePickupAck -or $RestartServerAfterLostAck)) { throw 'Prepare acknowledgement loss is a separate prepared-protocol scenario.' }
if ($SlotConflict -and ($LegacyProtocol -or -not $LosePickupAck -or $RestartServerAfterLostAck -or $ClientCount -ne 2)) { throw 'Slot conflict requires prepared protocol -LosePickupAck -ClientCount 2, without UE restart.' }
if ($RevisionConflict -and ($LegacyProtocol -or -not $LosePickupAck -or $RestartServerAfterLostAck -or $SlotConflict -or $LosePrepareAck -or $ClientCount -ne 2)) { throw 'Revision conflict requires prepared protocol -LosePickupAck -ClientCount 2 and no other conflict/restart mode.' }
$preparedFaultMode=if ($LegacyProtocol) {'none'} elseif ($LosePrepareAck) {'prepare-once'} elseif ($LosePickupAck) {'consume-held'} else {'none'}
$node = if (-not $LegacyProtocol -or $LosePickupAck) { (Get-Command node -ErrorAction Stop).Source } else { $null }
foreach ($file in @($project,$editor,$module)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing prerequisite: $file" } }
if ($RevisionConflict -and -not [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($module)).Contains('RPG_NPC_PICKUP_FIXTURE_INVENTORY_ONLY')) {
    throw 'Build the container-aware NPC pickup probe before revision-conflict integration; no fixture records were created.'
}
$listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
if (@($listeners.GetActiveTcpListeners() | Where-Object { $_.Port -eq $uri.Port }).Count -or
    @($listeners.GetActiveUdpListeners() | Where-Object { $_.Port -eq $Port }).Count) { throw 'A requested port is already in use.' }
try { $dockerStatus = & docker info --format '{{.ServerVersion}}' 2>&1 }
catch { throw 'Docker engine is unavailable; start Docker Desktop before PostgreSQL E2E.' }
if ($LASTEXITCODE -ne 0) { throw 'Docker engine is unavailable; start Docker Desktop before PostgreSQL E2E.' }
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
$environment = [Environment]::GetEnvironmentVariables('Process')
$pathKeys = @($environment.Keys | Where-Object { $_.ToString() -ieq 'Path' })
if ($pathKeys.Count -gt 1) {
    $pathValue = [string]$environment[$pathKeys[0]]
    foreach ($key in $pathKeys) { [Environment]::SetEnvironmentVariable($key.ToString(),$null,'Process') }
    [Environment]::SetEnvironmentVariable('Path',$pathValue,'Process')
}
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $projectRoot ("Saved\Tests\NPCPickupBackend\$runId")
$revisionUserRoot = if ($RevisionConflict) { Join-Path $projectRoot ("Saved\U\"+$runId.Substring(0,8)) } else { $null }
if ($RevisionConflict -and (Test-Path -LiteralPath $revisionUserRoot)) { throw 'Revision runtime UserDir must be fresh.' }
$serverUserDirectory = if ($RevisionConflict) { Join-Path $revisionUserRoot 's' } else { Join-Path $runRoot 'server-User' }
$buildRoot = Join-Path $runRoot 'backend-build'
$pickupReadyFile = Join-Path $runRoot 'pickup-ready.gate'
$pickupCommittedFile = Join-Path $runRoot 'pickup-committed.gate'
$itemApiUrl = $baseUrl
$proxyControlUrl = $null
$proxyHeaders = $null
$proxySnapshot = $null
$proxyScript = Join-Path $PSScriptRoot 'Fixtures\ItemPickupHttpFaultProxy.mjs'
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$moduleHash = (Get-FileHash -LiteralPath $module).Hash
$runnerHash = (Get-FileHash -LiteralPath $PSCommandPath).Hash
$runnerSourcePath=$PSCommandPath
$partyProofPath = Join-Path $PSScriptRoot 'NPCPickupNetworkProof.ps1'
$partyProofHash = (Get-FileHash -LiteralPath $partyProofPath).Hash
. $partyProofPath
$preparedProofPath=Join-Path $PSScriptRoot 'PreparedPickupE2EProof.ps1'
if (-not $LegacyProtocol) { . $preparedProofPath }
$slotProofPath=Join-Path $PSScriptRoot 'NPCPickupSlotConflict.ps1'
if ($SlotConflict) { . $slotProofPath }
$revisionProofPath=Join-Path $PSScriptRoot 'NPCPickupRevisionConflict.ps1'
if ($RevisionConflict) { . $revisionProofPath }
$restartProofPath=Join-Path $PSScriptRoot 'NPCPickupServerRestart.ps1'
if ($RestartServerAfterLostAck) { . $restartProofPath }
$sourceFiles = @('Private\Tests\RPGNPCPickupNetworkProbe.cpp','Private\Tests\RPGNPCPickupNetworkProbe.h',
    'Private\Tests\RPGItemCommandE2EProbeComponent.cpp','Private\Tests\RPGItemCommandE2EProbeComponent.h',
    'Private\Tests\RPGDungeonFlowE2EProbe.cpp','Private\Ability\NPC\RPGNPCDropItemAbility.cpp',
    'Public\Ability\NPC\RPGNPCDropItemAbility.h','Private\Character\RPGNonPlayerCharacter.cpp',
    'Public\Character\RPGNonPlayerCharacter.h','Private\Item\PickUp\RPGPickUpBase.cpp',
    'Public\Item\PickUp\RPGPickUpBase.h','Private\Component\RPGItemCommandComponent.cpp',
    'Private\Component\RPGPickupGrantComponent.cpp',
    'Private\Component\RPGInventoryProjectionComponent.cpp','Public\Component\RPGInventoryProjectionComponent.h',
    'Private\Item\Transaction\RPGPickupGrantJournal.cpp','Public\Item\Transaction\RPGPickupGrantJournal.h')
$sourceFiles += @('Private\Item\Backend\RPGItemBackendGatewayPickup.cpp','Private\Item\Backend\RPGItemBackendGateway.cpp','Private\Item\Backend\RPGItemBackendSubsystem.cpp',
    'Private\Item\Backend\RPGItemBackendTransport.cpp','Private\Item\Backend\RPGItemBackendJsonCodec.cpp',
    'Public\Item\Backend\RPGItemBackendSubsystem.h','Public\Item\Backend\RPGItemBackendGateway.h',
    'Public\Component\RPGPickupGrantComponent.h','Private\Item\Transaction\RPGPickupGrantReceiptPolicy.cpp',
    'Public\Item\Transaction\RPGPickupGrantReceiptPolicy.h','Public\Item\Backend\RPGItemBackendJsonCodec.h')
$assetFiles = @('Blueprints\Character\NPC\BP_RPGNPCBase.uasset','Blueprints\Character\NPC\DummyNPC\BP_Dummy_Gdn.uasset',
    'Blueprints\Character\NPC\Gruntling\Guardian\DA_Guardian.uasset',
    'Blueprints\Character\NPC\Gruntling\Guardian\BP_Guardian_DropItemAbility.uasset',
    'Blueprints\Character\NPC\Gruntling\GameplayAbility\GA_Guardian_Death.uasset',
    'Blueprints\Character\Shared\GameplayAbility\GA_NPC_Death_Base.uasset',
    'Blueprints\Character\Shared\GameplayEffect\GE_Shared_ApplyDamage.uasset',
    'Blueprints\Character\NPC\Gruntling\Anim\Montage\AM_Guardian_Death.uasset',
    'Blueprints\GameData\Item\DT_ItemDrop.uasset','Blueprints\Item\Equipment\BP_Pickup_GreatSword.uasset',
    'Blueprints\GameData\Item\Weapons\DA_Item_GreatSword.uasset')
$files = @($sourceFiles | ForEach-Object { Join-Path $projectRoot ("Source\Project_RPG\$_") }) +
    @($assetFiles | ForEach-Object { Join-Path $projectRoot ("Content\$_") })
if (-not $LegacyProtocol -or $LosePickupAck) {
    $files += @($proxyScript,(Join-Path $PSScriptRoot 'Fixtures\PreparedPickupHttpObservation.mjs'),(Join-Path $projectRoot 'Config\DefaultGame.ini'))
}
if (-not $LegacyProtocol) {
    $files += $preparedProofPath
    $backendSnapshotHelper=Join-Path $PSScriptRoot 'NPCPickupBackendSnapshot.ps1'
    . $backendSnapshotHelper
    $backendSnapshot=New-NPCPickupBackendSnapshot $projectRoot (Join-Path $runRoot 'backend-source') -ReuseRestoreCache:$NoBackendRestore
    $backendSnapshotManifest=Join-Path $runRoot 'BackendSnapshot.proof.json'
    $backendSnapshot | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $backendSnapshotManifest -Encoding UTF8
    $files += @($backendSnapshotHelper,$backendSnapshotManifest)+@($backendSnapshot.inputs | ForEach-Object path)+@($backendSnapshot.restoreMetadata | ForEach-Object path)
}
if ($RestartServerAfterLostAck) { $files += $restartProofPath }
if ($SlotConflict) { $files += $slotProofPath }
if ($RevisionConflict) { $files += $revisionProofPath }
$hashes = @($files | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} })
foreach ($relative in $sourceFiles) {
    if ((Get-Item -LiteralPath (Join-Path $projectRoot "Source\Project_RPG\$relative")).LastWriteTimeUtc -gt (Get-Item -LiteralPath $module).LastWriteTimeUtc) { throw 'Build the current Project_RPG Editor module before running this fixture.' }
}
# Persist provenance before any runtime/DB writes so an interrupted attempt is
# identifiable without borrowing a later shared-worktree DLL. This is not a pass.
[ordered]@{schemaVersion=1;scope='Pre-runtime attempt manifest only; not a completed E2E';passed=$false;
    pickupProtocol=$(if ($LegacyProtocol) {'legacy'} else {'prepared'});runId=$runId;startedAtUtc=[DateTime]::UtcNow.ToString('o');moduleSHA256=$moduleHash;
    backendSourceSnapshotVerified=(-not $LegacyProtocol);
    preparedFaultMode=$preparedFaultMode;slotConflictFixture=[bool]$SlotConflict;revisionConflictFixture=[bool]$RevisionConflict;
    runnerSHA256=$runnerHash;partyProofSHA256=$partyProofHash;clients=$ClientCount;losePickupAck=[bool]$LosePickupAck;sourceAndAssetHashes=$hashes;startupLeaseHeartbeatFixture=$true} | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath (Join-Path $runRoot 'Attempt.proof.json') -Encoding UTF8
$envNames = @('ASPNETCORE_ENVIRONMENT','PROJECT_RPG_BACKEND_ADMIN_TOKEN','POSTGRES_CONNECTION_STRING',
    'Kestrel__Endpoints__Http__Url','PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
    'PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN','PROJECT_RPG_ITEM_PROXY_UPSTREAM_PORT','PROJECT_RPG_ITEM_PROXY_LISTEN_PORT','PROJECT_RPG_ITEM_PROXY_JOURNAL_ROOT','UE_SKIP_UBT_SDK_SETUP')
$previousEnv = @{}
foreach ($name in $envNames) { $previousEnv[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$adminToken = "npc-pickup-$runId"
$adminHeaders = @{Authorization="Bearer $adminToken"}
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$logs = [Collections.Generic.List[string]]::new()
$failure = 'ITEM_PROXY_FAIL|RPG_NPC_PICKUP FAIL|DUNGEON_FLOW_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|LogScript: Error:|LogScript: Warning:[^\r\n]*(?:Accessed None|Attempted to access|Infinite loop|Array index out of bounds)|Log(?:Iris\w*|Net\w*):\s*Error:|RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED'
$backend = $null
$sessionId = $null
$serverHeaders = $null
$maintainStartupLease = $false
$nextStartupHeartbeatUtc = [DateTime]::MinValue
$startupLeaseHeartbeatCount = 0
$blockedDatabaseSnapshot=$null

function Invoke-Api {
    param([string]$Path,[string]$Method='Get',[hashtable]$Headers=@{},[object]$Body=$null,[int]$ExpectedStatus=200)
    $apiParams = @{Uri="$baseUrl/api/$Path";Method=$Method;Headers=$Headers;TimeoutSec=10;UseBasicParsing=$true}
    if ($null -ne $Body) { $apiParams.ContentType='application/json'; $apiParams.Body=$Body | ConvertTo-Json -Depth 30 -Compress }
    try { $response=Invoke-WebRequest @apiParams }
    catch {
        if ($ExpectedStatus -in @(403,404,409) -and $null -ne $_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq $ExpectedStatus) { return }
        throw "HTTP $Method $Path failed; credentials/body omitted."
    }
    if ([int]$response.StatusCode -ne $ExpectedStatus) { throw "Unexpected HTTP status for $Method $Path" }
    if ($response.Content) { return $response.Content | ConvertFrom-Json }
}
function Assert-Healthy {
    foreach ($proc in $processes) { $proc.Refresh(); if ($proc.HasExited) { throw "Owned process $($proc.Id) exited; inspect $runRoot" } }
    foreach ($log in $logs) { if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Pattern $failure -Quiet)) { throw "Runtime failure; inspect $log" } }
    if ($maintainStartupLease -and [DateTime]::UtcNow -ge $nextStartupHeartbeatUtc) {
        $keepalive=Invoke-Api "dungeon-sessions/$sessionId/heartbeat" 'Post' $serverHeaders @{serverId=$serverId}
        if ($keepalive.dungeonSessionId -ne $sessionId -or $keepalive.serverId -ne $serverId -or
            $keepalive.state -notin @('Loading','InProgress')) { throw 'Assigned startup heartbeat scope mismatch.' }
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
function Start-Backend {
    param([string]$Name)
    $out=Join-Path $runRoot "$Name.stdout.log"; $err=Join-Path $runRoot "$Name.stderr.log"
    $backendArguments = @(('"'+(Join-Path $buildRoot 'ProjectRpg.Backend.dll')+'"'),
        '--DungeonContent:Entries:1:DungeonId=Dungeon.NPCPickupE2E',
        '--DungeonContent:Entries:1:MapPackage=/Game/Maps/testmap',
        '--DungeonContent:Entries:1:Difficulties:0=Normal')
    $proc=Start-Process -FilePath $dotnet -ArgumentList $backendArguments -WorkingDirectory $buildRoot -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
    $processes.Add($proc)
    $deadline=[DateTime]::UtcNow.AddSeconds(45); $ready=$false
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        try { $health=Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 2; if ($health.status -eq 'ready' -and $health.storageProvider -eq 'Postgres') { $ready=$true; break } } catch { }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw "PostgreSQL backend readiness failed; inspect $out and $err" }
    return $proc
}
function Start-Unreal {
    param([string]$Name,[string]$Destination,[string[]]$Extra,[string]$SavedDirectoryName='')
    if (-not $SavedDirectoryName) { $SavedDirectoryName=$Name }
    $userDirectory = if ($RevisionConflict) {
        # Report journals append two hashes, a session UUID and a child journal.
        # Keep the process prefix short even inside a private RuntimeProject.
        $role=if ($Name -ceq 'server') {'s'} elseif ($Name -cmatch '^client-([1-2])$') {'c'+$Matches[1]} else {throw 'Unknown revision fixture role.'}
        Join-Path $revisionUserRoot $role
    } else { Join-Path $runRoot "$SavedDirectoryName-User" }
    if ([Environment]::GetEnvironmentVariable('PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN','Process')) {
        throw 'UE must not inherit fault-proxy control credentials.'
    }
    if ('-server' -notin $Extra -and [Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN','Process')) {
        throw 'A UE client must not inherit the assigned game-server token.'
    }
    # Existing prebuilt editor runtime, as in Run-ItemAutomationTest -SkipSDKSetup.
    # Do not launch a ValidatePlatforms Build.bat behind a concurrent module build.
    $env:UE_SKIP_UBT_SDK_SETUP='1'
    $log=Join-Path $runRoot "$Name.log"
    $runtimeArgs=@(('"'+$project+'"'),$Destination)+$Extra+@('-RPGNPCDropPickupE2E','-UseIrisReplication=1','-RPGRequireIris',
        '-unattended','-nop4','-nosplash','-nosound','-nullrhi','-nosteam','-NoLiveCoding','-NoLoadingScreen','-DisablePython',
        '-DDC=(EnginePak,Local)',('-UserDir="'+$userDirectory+'"'),('-abslog="'+$log+'"'),'-ExecCmds="t.MaxFPS 30"',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$baseUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$itemApiUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$baseUrl/api")
    $runtimeArgs += "-RPGNPCPickupTimeoutSeconds=$TimeoutSeconds"
    if (-not $LegacyProtocol) { $runtimeArgs += '-RPGNPCPickupPreparedE2E' }
    if ($RevisionConflict) { $runtimeArgs += '-RPGNPCPickupRevisionConflictE2E' }
    if ($ClientCount -gt 1) { $runtimeArgs += "-RPGNPCPickupCharacters=$($members.CharacterId -join ',')" }
    if ($LosePickupAck -and '-RPGNPCPickupRestartRecoveryE2E' -notin $Extra) { $runtimeArgs += @('-RPGNPCPickupLostAckE2E',('-RPGNPCPickupCommittedFile="'+$pickupCommittedFile+'"')) }
    $proc=Start-Process -FilePath $editor -ArgumentList $runtimeArgs -WorkingDirectory $projectRoot -PassThru -WindowStyle Hidden
    $processes.Add($proc); $logs.Add($log)
    Write-Host "Started $Name (PID $($proc.Id)): $log"
    return $log
}
function Invoke-ProxyControl {
    param([string]$Path,[object]$Body=$null)
    $proxyRequest=@{Uri="$proxyControlUrl/$Path";Headers=$proxyHeaders;TimeoutSec=5;UseBasicParsing=$true;Method='Get'}
    if ($null -ne $Body) { $proxyRequest.Method='Post'; $proxyRequest.ContentType='application/json'; $proxyRequest.Body=$Body | ConvertTo-Json -Compress }
    try { return (Invoke-WebRequest @proxyRequest).Content | ConvertFrom-Json }
    catch { throw 'Item proxy control failed; credentials/body omitted.' }
}
function Wait-ProxyState {
    param([scriptblock]$Condition)
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        $state=Invoke-ProxyControl 'state'
        if ($state.failure) { throw 'Scoped HTTP fault fixture failed.' }
        if (& $Condition $state) { return $state }
        Start-Sleep -Milliseconds 250
    }
    throw 'Expected HTTP fault boundary was not observed.'
}
function Start-ItemProxy {
    $out=Join-Path $runRoot 'item-proxy.stdout.log'; $err=Join-Path $runRoot 'item-proxy.stderr.log'
    $controlToken=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N')
    $env:PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN=$controlToken
    $env:PROJECT_RPG_ITEM_PROXY_UPSTREAM_PORT=[string]$uri.Port
    $env:PROJECT_RPG_ITEM_PROXY_LISTEN_PORT='0'
    $env:PROJECT_RPG_ITEM_PROXY_JOURNAL_ROOT=Join-Path $serverUserDirectory "Saved\ItemPickupJournal\$sessionId"
    try {
        $proc=Start-Process -FilePath $node -ArgumentList ('"'+$proxyScript+'"') -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
    } finally {
        # Only the owned Node fixture needs these control credentials/settings.
        # In particular, neither UE client nor server may inherit the control token.
        foreach ($name in @('PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN','PROJECT_RPG_ITEM_PROXY_UPSTREAM_PORT','PROJECT_RPG_ITEM_PROXY_LISTEN_PORT','PROJECT_RPG_ITEM_PROXY_JOURNAL_ROOT')) {
            [Environment]::SetEnvironmentVariable($name,$null,'Process')
        }
    }
    $processes.Add($proc); $logs.Add($err)
    $ports=$null
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        if (Test-Path -LiteralPath $out) {
            $readyStream=[IO.FileStream]::new($out,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            $readyReader=[IO.StreamReader]::new($readyStream)
            try { $readyText=$readyReader.ReadToEnd() } finally { $readyReader.Dispose() }
            if (-not [string]::IsNullOrWhiteSpace($readyText)) {
                $ports=[regex]::Match($readyText,'^ITEM_PROXY_READY ControlPort=([1-9]\d*) ProxyPort=([1-9]\d*)\r?\n?$')
                if ($ports.Success) { break }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ports.Success) { throw 'Item proxy did not become ready.' }
    $script:proxyControlUrl="http://127.0.0.1:$($ports.Groups[1].Value)"
    $script:itemApiUrl="http://127.0.0.1:$($ports.Groups[2].Value)"
    $script:proxyHeaders=@{Authorization="Bearer $controlToken"}
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $authorizationHash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]$serverHeaders.Authorization)))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $armBody=@{characterIds=@($members.CharacterId);authorizationSHA256=$authorizationHash}
    if (-not $LegacyProtocol) { $armBody.protocol='prepared'; $armBody.serverId=$serverId; $armBody.sessionId=$sessionId }
    if ($preparedFaultMode -ne 'none') { $armBody.fault=$preparedFaultMode }
    if ($SlotConflict) { $armBody.slotConflict=$true }
    if ($RevisionConflict) { $armBody.revisionConflict=$true }
    $armed=Invoke-ProxyControl 'arm' $armBody
    $expectedPhase=if ($LegacyProtocol) {'armed'} else {'observing'}
    if ($armed.phase -ne $expectedPhase) { throw 'Item proxy scope was not armed.' }
}
function Get-NativeRows {
    param([string]$Log,[int]$ExpectedCount)
    $text=Get-Content -LiteralPath $Log -Raw -Encoding UTF8
    if ($text -match $failure) { throw "Runtime failure: $Log" }
    $rows=@([regex]::Matches($text,'(?m)^(?:\[[^\]\r\n]+\]\[\s*\d+\])?LogTemp: Display: (RPG_NPC_PICKUP [^\r\n]+)\r?$'))
    if ($rows.Count -ne $ExpectedCount -or [regex]::Matches($text,'RPG_NPC_PICKUP ').Count -ne $ExpectedCount) { throw "Wrong native observation count: $Log" }
    return @($rows | ForEach-Object {$_.Groups[1].Value})
}
function Assert-Persisted {
    param([string]$ItemId,[string]$RequestId,[string]$Definition)
    $item=Invoke-Api "items/$ItemId" 'Get' $playerHeaders
    $all=Invoke-Api "items?ownerType=Character&ownerId=$characterId&includeTerminal=false&limit=200" 'Get' $playerHeaders
    $receipt=Invoke-Api "item-transactions/$RequestId" 'Get' $serverHeaders
    if (@($all.items).Count -ne 1 -or $item.state.instanceId -ne $ItemId -or $item.definitionType -ne 'RPGItemDefinition' -or
        $item.definitionName -ne $Definition -or $item.owner.type -ne 'Character' -or $item.owner.ownerId -ne $characterId -or
        $item.location.containerType -ne 'Inventory' -or $item.location.containerId -ne $characterId -or $item.location.slotIndex -ne 0 -or
        $item.state.quantity -ne 1 -or $item.revision -ne 1 -or $item.metadata.creationSource -ne 'WorldPickup' -or
        $receipt.status -notin @('Committed','AlreadyCommitted') -or $receipt.requestId -ne $RequestId -or $receipt.operation -ne 'Item.Pickup' -or
        $receipt.actor.ownerId -ne $characterId -or $receipt.affectedQuantity -ne 1 -or @($receipt.records).Count -ne 1 -or
        $receipt.records[0].state.instanceId -ne $ItemId -or $receipt.records[0].revision -ne 1 -or $receipt.records[0].state.quantity -ne 1 -or
        ($item | ConvertTo-Json -Depth 25 -Compress) -cne ($receipt.records[0] | ConvertTo-Json -Depth 25 -Compress)) { throw 'Persistent item/receipt mismatch or duplicate grant.' }
    return $receipt
}
function Assert-PartyIsolation {
    param([string]$ItemId,[string]$RequestId)
    foreach ($member in $members) {
        # A player token may never read a server transaction receipt, even the winner.
        $null=Invoke-Api "item-transactions/$RequestId" 'Get' $member.Headers $null 403
        if ($member.CharacterId -eq $characterId) { continue }
        $inventory=Invoke-Api "items?ownerType=Character&ownerId=$($member.CharacterId)&includeTerminal=false&limit=200" 'Get' $member.Headers
        if (@($inventory.items).Count -ne 0) { throw 'A losing claimant received a persistent item.' }
        $null=Invoke-Api "items/$ItemId" 'Get' $member.Headers $null 403
        $null=Invoke-Api "items?ownerType=Character&ownerId=$characterId&includeTerminal=false&limit=200" 'Get' $member.Headers $null 403
    }
    foreach ($rejected in $rejectedIds) { $null=Invoke-Api "item-transactions/$rejected" 'Get' $serverHeaders $null 404 }
}

try {
    if (-not $SkipPostgresStart) { & docker compose -f (Join-Path $projectRoot 'Backend\compose.postgres.yml') up -d --wait; if ($LASTEXITCODE -ne 0) { throw 'Development PostgreSQL startup failed' } }
    $backendProject=if ($LegacyProtocol) { Join-Path $projectRoot 'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj' } else { $backendSnapshot.projectFile }
    $buildArgs=@('build',$backendProject,'--output',$buildRoot)
    if ($NoBackendRestore) { $buildArgs+='--no-restore' }
    & $dotnet @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 'Backend build failed' }
    $backendModuleHash=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash
    $env:ASPNETCORE_ENVIRONMENT='Development'; $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN=$adminToken
    $env:POSTGRES_CONNECTION_STRING=$ConnectionString
    [Environment]::SetEnvironmentVariable('Kestrel__Endpoints__Http__Url',$baseUrl,'Process')
    foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    $backend=Start-Backend 'backend'
    $members=@(for ($index=1;$index -le $ClientCount;$index++) {
        $steamId=([uint64]76561410000000000+[uint64][Convert]::ToUInt32($runId.Substring(0,8),16)*4+[uint64]$index).ToString()
        $auth=Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steamId"}
        $headers=@{Authorization="Bearer $($auth.accessToken)"}
        $character=Invoke-Api 'characters' 'Post' $headers @{name="NPC$($runId.Substring(0,10))$index"} 201
        [pscustomobject]@{SteamId=$steamId;CharacterId=[string]$character.characterId;Headers=$headers;Log=$null}
    })
    $playerHeaders=$members[0].Headers; $characterId=$members[0].CharacterId
    $session=Invoke-Api 'dungeon-sessions' 'Post' $playerHeaders @{characterId=$characterId;dungeonId='Dungeon.NPCPickupE2E';difficulty='Normal'} 201
    $sessionId=[string]$session.dungeonSessionId
    foreach ($member in @($members | Select-Object -Skip 1)) {
        $session=Invoke-Api "dungeon-sessions/$sessionId/members" 'Post' $member.Headers @{characterId=$member.CharacterId}
    }
    if ($session.state -ne 'Waiting' -or @($session.members).Count -ne $ClientCount) { throw 'Waiting fixture party mismatch.' }
    $serverId="npc-pickup-$($runId.Substring(0,12))"
    . (Join-Path $projectRoot 'Backend/Tools/Request-DungeonDeparture.ps1')
    $null = Request-RPGDungeonDeparture $baseUrl $sessionId $characterId $playerHeaders
    $assignment=Invoke-Api "dungeon-sessions/$sessionId/activate" 'Post' $adminHeaders @{serverId=$serverId;serverAddress="127.0.0.1:$Port"}
    $serverHeaders=@{Authorization="Bearer $($assignment.gameServerAccessToken)"}
    if (-not $LegacyProtocol -or $LosePickupAck) { Start-ItemProxy }
    $maintainStartupLease=$true
    try {
        $env:PROJECT_RPG_GAME_SERVER_ID=$serverId; $env:PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId
        $env:PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken
        $serverLog=Start-Unreal 'server' '/Game/Maps/testmap?listen?game=/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C' @('-server',"-port=$Port",'-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity')
    } finally { foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) { [Environment]::SetEnvironmentVariable($name,$null,'Process') } }
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds); $started=$false
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        $state=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
        if ($state.state -eq 'InProgress') { $started=$true; break }
        if ($state.state -ne 'Loading') { throw 'Assigned session left Loading before UE startup completed.' }
        Start-Sleep -Milliseconds 250
    }
    if (-not $started) { throw 'Assigned UE server did not start its actual backend session.' }
    $pickupSendFile=Join-Path $runRoot 'pickup-send.gate'
    for ($index=0;$index -lt $members.Count;$index++) {
        $member=$members[$index]
        $extra=@('-game','-RPGDungeonFlowE2E',"-RPGFlowE2ESteam=$($member.SteamId)",
            "-RPGFlowE2ECharacter=$($member.CharacterId)","-RPGFlowE2ESession=$sessionId",('-RPGNPCPickupReadyFile="'+$pickupReadyFile+'"'))
        if ($ClientCount -gt 1) { $extra+=@('-PktLag=250','-PktLoss=0',('-RPGNPCPickupSendFile="'+$pickupSendFile+'"')) }
        $member.Log=Start-Unreal "client-$($index+1)" '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' $extra
    }
    foreach ($member in $members) { Wait-Marker $member.Log 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1' }
    # BeginPlay/InProgress can precede blocking editor startup work. Keep this
    # owned server's assigned lease alive only until actual remote admission,
    # then require normal UE heartbeats for drop/pickup and backend recovery.
    $maintainStartupLease=$false
    Wait-Marker $serverLog 'RPG_NPC_PICKUP SERVER_DROP '
    New-Item -ItemType File -Path $pickupReadyFile | Out-Null
    if ($ClientCount -gt 1) {
        foreach ($member in $members) { Wait-Marker $member.Log 'RPG_NPC_PICKUP CLIENT_READY ' }
        # Every authenticated contender has the same live replica and is in range
        # before any sends. Client network lag prevents retirement overtaking sends.
        New-Item -ItemType File -Path $pickupSendFile | Out-Null
        if ($RevisionConflict) { Invoke-NPCPickupRevisionConflict; return }
        if ($SlotConflict) { Invoke-NPCPickupSlotConflict }
        if ($LosePickupAck) {
            $blocked=Wait-ProxyState {param($s) $s.phase -eq 'blocked' -and $s.droppedAcknowledgements -eq 1}
            $winner=@($members | Where-Object CharacterId -eq $blocked.committed.characterId)
            if ($winner.Count -ne 1) { throw 'Unknown durable commit owner.' }
            $playerHeaders=$winner[0].Headers; $characterId=$winner[0].CharacterId
            $blockedReceipt=Assert-Persisted $blocked.committed.itemId $blocked.committed.requestId $blocked.committed.definition
            if (-not $LegacyProtocol) {
                $o=$blocked.prepared.origin
                $blockedIntent=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$($o.requestId)" 'Get' $serverHeaders
                $blockedDatabaseSnapshot=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
                Assert-NPCPickupPreparedDatabaseSnapshot $blockedDatabaseSnapshot $blockedIntent $blockedReceipt $sessionId $serverId $o.dropId $characterId
                $blockedDatabaseSnapshot | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot 'UnknownAck.blocked.database.proof.json') -Encoding UTF8
                $pendingJournal=Join-Path $runRoot "server-User\Saved\ItemPickupJournal\$sessionId\$($o.requestId).json"
                $pendingCopy=Join-Path $runRoot 'UnknownAck.journal-v2.json'
                if ((Get-FileHash -LiteralPath $pendingJournal).Hash.ToLowerInvariant() -cne $o.journalSHA256) { throw 'Pending journal did not match the pre-HTTP origin.' }
                Copy-Item -LiteralPath $pendingJournal -Destination $pendingCopy
                if ((Get-FileHash -LiteralPath $pendingCopy).Hash.ToLowerInvariant() -cne $o.journalSHA256) { throw 'Pending journal copy mismatch.' }
            }
            # This file is created only after direct, authenticated PostgreSQL GETs.
            if (-not (Test-Path -LiteralPath $pickupCommittedFile)) { New-Item -ItemType File -Path $pickupCommittedFile | Out-Null }
            Wait-Marker $serverLog 'RPG_NPC_PICKUP SERVER_UNCERTAIN '
            $uncertainRows=Get-NativeRows $serverLog 3
            $uncertain=[regex]::Match($uncertainRows[2],'^RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=([1-9]\d*) Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=([1-8])$')
            if (-not $uncertain.Success) { throw 'Missing live gateway attempt limit.' }
            $gatewayAttempts=[int]$uncertain.Groups[2].Value
            $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                Assert-Healthy
                $pending=@($members | ForEach-Object {
                    Select-String -LiteralPath $_.Log -Pattern 'LogTemp: Display: RPG_NPC_PICKUP CLIENT_REJECTED_PENDING ' | ForEach-Object {$_.Line}
                })
                if ($pending.Count -gt 3) { throw 'Unexpected pending command result count.' }
                if ($pending.Count -eq 3) { break }
                Start-Sleep -Milliseconds 250
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($pending.Count -ne 3) { throw 'Missing live-pickup rejections before recovery.' }
            $blocked=Wait-ProxyState {param($s) $s.phase -eq 'blocked' -and $s.blockedRetries.Count -ge [Math]::Max(4,$gatewayAttempts) -and
                ($LegacyProtocol -or (@($s.prepared.preparations | Where-Object consumed -eq $true).Count -ge 1 -and $s.prepared.preparations[-1].acknowledgement -eq 'delivered'))}
            $blocked | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runRoot 'UnknownAck.blocked.observed.json') -Encoding UTF8
            # Read the effective limit from the live native subsystem, not a
            # config-file assumption. The next burst crosses its >=2s timer.
            $gap=if ($LegacyProtocol) { Get-NPCPickupNativeRetryGap $blocked -GatewayAttempts $gatewayAttempts } else { Get-NPCPickupPreparedRetryGap $blocked }
            $null=Assert-Persisted $blocked.committed.itemId $blocked.committed.requestId $blocked.committed.definition
            [ordered]@{passed=$false;scope='Durable commit/uncertainty boundary only; final recovery not yet observed';state=$blocked;gatewayAttempts=$gatewayAttempts;nativeGrantTimerGapSeconds=$gap} |
                ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'UnknownAck.blocked.proof.json') -Encoding UTF8
            if ($RestartServerAfterLostAck) {
                Invoke-NPCPickupServerRestart $blocked.committed $gatewayAttempts $gap
                return
            }
            $released=Invoke-ProxyControl 'release' @{}
            if ($released.phase -ne 'released') { throw 'Proxy recovery was not released.' }
        }
        foreach ($member in $members) { Wait-Marker $member.Log 'RPG_NPC_PICKUP CLIENT_PARTY_COMPLETE ' }
    } else { Wait-Marker $members[0].Log 'RPG_NPC_PICKUP CLIENT_COMPLETE ' }
    Wait-Marker $serverLog 'RPG_NPC_PICKUP SERVER_COMMIT '
    $serverRowCount=if ($LosePickupAck) {4} else {3}
    $clientRowCount=if ($LosePickupAck) {5} elseif ($ClientCount -gt 1) {3} else {2}
    $serverRows=Get-NativeRows $serverLog $serverRowCount
    $dropId=$null
    if (-not $LegacyProtocol) {
        $dropId=Get-NPCPickupPreparedDropId $serverRows[1]
        $serverRows[1]=$serverRows[1] -creplace (' DropId='+[regex]::Escape($dropId)+'$'),''
    }
    if ($ClientCount -gt 1) {
        $observation=Get-NPCPickupPartyObservations $serverRows @($members | ForEach-Object {
            [pscustomobject]@{characterId=$_.CharacterId;rows=@(Get-NativeRows $_.Log $clientRowCount)}
        }) -UnknownAck:$LosePickupAck
        $itemId=$observation.itemId; $requestId=$observation.requestId; $definition=$observation.definition
        if ($LosePickupAck -and $observation.gatewayAttempts -ne $gatewayAttempts) { throw 'Borrowed effective gateway attempt limit.' }
        $rejectedIds=@($observation.rejectedRequestIds); $rejectedId=$rejectedIds[0]
        $npcNetId=$observation.npcNetId; $pickupNetId=$observation.pickupNetId
        $winner=@($members | Where-Object CharacterId -eq $observation.winnerCharacterId)
        if ($winner.Count -ne 1) { throw 'Unknown winning fixture member.' }
        $playerHeaders=$winner[0].Headers; $characterId=$winner[0].CharacterId
    } else {
    $clientRows=Get-NativeRows $members[0].Log 2
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    $death=[regex]::Match($serverRows[0],'^RPG_NPC_PICKUP SERVER_DEATH NPC=([1-9]\d*) Deaths=1 Authenticated=1 NativeSource=1$')
    $drop=[regex]::Match($serverRows[1],'^RPG_NPC_PICKUP SERVER_DROP NPC=([1-9]\d*) Pickup=([1-9]\d*) Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1$')
    $commit=[regex]::Match($serverRows[2],"^RPG_NPC_PICKUP SERVER_COMMIT Pickup=([1-9]\d*) Item=($guid) Definition=([A-Za-z0-9_.]+) Quantity=1 Revision=1 Retired=1$")
    $send=[regex]::Match($clientRows[0],'^RPG_NPC_PICKUP CLIENT_SEND Pickup=([1-9]\d*) Requests=2 ExistingRPC=1$')
    $done=[regex]::Match($clientRows[1],"^RPG_NPC_PICKUP CLIENT_COMPLETE Pickup=([1-9]\d*) Request=($guid) Rejected=($guid) Item=($guid) Definition=([A-Za-z0-9_.]+) Quantity=1 Revision=1 Retired=1$")
    if (-not $death.Success -or -not $drop.Success -or -not $commit.Success -or -not $send.Success -or -not $done.Success -or
        $death.Groups[1].Value -ne $drop.Groups[1].Value -or $drop.Groups[2].Value -ne $commit.Groups[1].Value -or
        $commit.Groups[1].Value -ne $send.Groups[1].Value -or $send.Groups[1].Value -ne $done.Groups[1].Value -or
        $commit.Groups[2].Value -ne $done.Groups[4].Value -or $commit.Groups[3].Value -ne $done.Groups[5].Value -or
        $done.Groups[2].Value -eq $done.Groups[3].Value) { throw 'Malformed/borrowed NPC, pickup, request or persistent item observations.' }
    $itemId=$done.Groups[4].Value; $requestId=$done.Groups[2].Value; $rejectedId=$done.Groups[3].Value; $definition=$done.Groups[5].Value
    $rejectedIds=@($rejectedId); $npcNetId=$death.Groups[1].Value; $pickupNetId=$drop.Groups[2].Value
    }
    $receipt=Assert-Persisted $itemId $requestId $definition
    Assert-PartyIsolation $itemId $requestId
    if ($LosePickupAck) {
        $proxySnapshot=Wait-ProxyState {param($s) $s.phase -eq 'recovered' -and $s.delivered.upstreamStatus -eq 'AlreadyCommitted'}
        if ($proxySnapshot.committed.itemId -ne $itemId -or $proxySnapshot.committed.requestId -ne $requestId -or
            $proxySnapshot.delivered.itemId -ne $itemId -or $proxySnapshot.delivered.requestId -ne $requestId -or
            $proxySnapshot.delivered.bodySHA256 -cne $proxySnapshot.committed.bodySHA256) { throw 'UE recovery/HTTP/DB identity mismatch.' }
    }
    $databaseSnapshot=$null
    if (-not $LegacyProtocol) {
        $finalPreparedPhase=if ($LosePickupAck) {'recovered'} else {'observed'}
        $proxySnapshot=Wait-ProxyState {param($s) $s.phase -eq $finalPreparedPhase}
        $proxySnapshot | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runRoot 'PreparedPickup.proxy.observed.json') -Encoding UTF8
        Assert-NPCPickupPreparedObservation $proxySnapshot $sessionId $serverId $dropId $requestId $itemId $characterId -FaultMode $preparedFaultMode
        $intentPath="dungeon-sessions/$sessionId/pickup-intents/$requestId"
        $intent=Invoke-Api $intentPath 'Get' $serverHeaders
        foreach ($member in $members) {
            Invoke-Api $intentPath 'Get' $member.Headers $null 403
            Invoke-Api "$intentPath/commit" 'Post' $member.Headers $null 403
        }
        foreach ($rejected in $rejectedIds) { Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$rejected" 'Get' $serverHeaders $null 404 }
        $journalRoot=Join-Path $runRoot "server-User\Saved\ItemPickupJournal\$sessionId"
        if (-not (Test-Path -LiteralPath $journalRoot -PathType Container) -or
            @(Get-ChildItem -LiteralPath $journalRoot -File).Count -ne 0) { throw 'Confirmed pickup journal was not resolved.' }
        $databaseSnapshot=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $requestId $itemId
        $databaseSnapshot | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot 'PreparedPickup.database.observed.json') -Encoding UTF8
        Assert-NPCPickupPreparedDatabaseSnapshot $databaseSnapshot $intent $receipt $sessionId $serverId $dropId $characterId
        if ($LosePickupAck -and (ConvertTo-NPCPickupCanonicalJson $databaseSnapshot) -cne (ConvertTo-NPCPickupCanonicalJson $blockedDatabaseSnapshot)) {
            throw 'Already committed PostgreSQL rows changed during native acknowledgement recovery.'
        }
        # Real HTTP replay, no body and no reconstructed caller-controlled item.
        $replay=Invoke-Api "$intentPath/commit" 'Post' $serverHeaders
    } else {
        # Historical generic protocol only. Not prepared-protocol evidence.
        $newRecord=$receipt.records[0] | ConvertTo-Json -Depth 25 | ConvertFrom-Json
        $newRecord.revision=0
        $replay=Invoke-Api 'item-transactions/commit' 'Post' $serverHeaders @{requestId=$requestId;operation=$receipt.operation;
            commandFingerprint=$receipt.commandFingerprint;actor=$receipt.actor;affectedQuantity=1;mutations=@(@{expectedRevision=0;newRecord=$newRecord})}
    }
    if ($replay.status -ne 'AlreadyCommitted' -or $replay.requestId -ne $requestId) { throw 'Exact HTTP command replay was not idempotent.' }
    $null=Assert-Persisted $itemId $requestId $definition
    Assert-PartyIsolation $itemId $requestId
    if (-not $LegacyProtocol) {
        $afterReplay=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $requestId $itemId
        if ((ConvertTo-NPCPickupCanonicalJson $afterReplay) -cne (ConvertTo-NPCPickupCanonicalJson $databaseSnapshot)) { throw 'Prepared PostgreSQL rows changed after exact consume replay.' }
    }
    # Stop/restart only this run's backend; keep the shared PostgreSQL and UE alive.
    [void]$processes.Remove($backend)
    Stop-Process -Id $backend.Id; $backend.WaitForExit(10000) | Out-Null; $backend.Dispose()
    $backend=Start-Backend 'backend-restarted'
    $null=Assert-Persisted $itemId $requestId $definition
    Assert-PartyIsolation $itemId $requestId
    if (-not $LegacyProtocol) {
        $afterRestart=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $requestId $itemId
        if ((ConvertTo-NPCPickupCanonicalJson $afterRestart) -cne (ConvertTo-NPCPickupCanonicalJson $databaseSnapshot)) { throw 'Prepared PostgreSQL rows changed after backend restart.' }
        $databaseSnapshot | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot 'PreparedPickup.database.proof.json') -Encoding UTF8
    }
    for ($check=0;$check -lt 4;$check++) { Start-Sleep -Milliseconds 250; Assert-Healthy }
    # Freeze only this run's UE logs before hashing. Get-FileHash cannot safely
    # open the engine's live log handle; missing hashes must never become PASS.
    foreach ($proc in $processes.ToArray()) {
        if ($proc.ProcessName -ne 'UnrealEditor-Cmd') { continue }
        $proc.Refresh()
        if ($proc.HasExited) { throw 'Owned UE process exited before final observations were frozen.' }
        Stop-Process -InputObject $proc -ErrorAction Stop
        if (-not $proc.WaitForExit(10000)) { throw 'Owned UE log freeze timed out.' }
        [void]$processes.Remove($proc)
        $proc.Dispose()
    }
    $null=Get-NativeRows $serverLog $serverRowCount
    foreach ($member in $members) { $null=Get-NativeRows $member.Log $clientRowCount }
    if (-not $LegacyProtocol) {
        $finalProxy=Invoke-ProxyControl 'state'
        Assert-NPCPickupPreparedObservation $finalProxy $sessionId $serverId $dropId $requestId $itemId $characterId -FaultMode $preparedFaultMode
        if ((ConvertTo-NPCPickupCanonicalJson $finalProxy) -cne (ConvertTo-NPCPickupCanonicalJson $proxySnapshot)) { throw 'Unexpected late prepared pickup traffic.' }
        $proxySnapshot | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runRoot 'PreparedPickup.proxy.proof.json') -Encoding UTF8
    }
    if ($LosePickupAck) {
        $finalProxy=Invoke-ProxyControl 'state'
        if ($finalProxy.failure -or ($finalProxy | ConvertTo-Json -Depth 8 -Compress) -cne ($proxySnapshot | ConvertTo-Json -Depth 8 -Compress)) { throw 'Unexpected late fault traffic.' }
        $proxySnapshot | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'UnknownAck.proxy.proof.json') -Encoding UTF8
    }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $module).Hash -or $runnerHash -ne (Get-FileHash -LiteralPath $PSCommandPath).Hash) { throw 'Module/runner changed during E2E.' }
    if ($partyProofHash -ne (Get-FileHash -LiteralPath $partyProofPath).Hash) { throw 'Party proof parser changed during E2E.' }
    if ($backendModuleHash -ne (Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash) { throw 'Isolated backend binary changed during E2E.' }
    foreach ($file in $hashes) { if ($file.SHA256 -ne (Get-FileHash -LiteralPath $file.path).Hash) { throw 'Source/asset changed during E2E.' } }
    $serverLogHash=(Get-FileHash -LiteralPath $serverLog -ErrorAction Stop).Hash
    $clientLogHashes=@($members | ForEach-Object { [ordered]@{characterId=$_.CharacterId;path=$_.Log;SHA256=(Get-FileHash -LiteralPath $_.Log -ErrorAction Stop).Hash} })
    if ($serverLogHash -notmatch '^[A-F0-9]{64}$' -or @($clientLogHashes | Where-Object {$_.SHA256 -notmatch '^[A-F0-9]{64}$'}).Count) { throw 'Missing final log hash; refusing PASS.' }
    [ordered]@{schemaVersion=5;passed=$true;pickupProtocol=$(if ($LegacyProtocol) {'legacy'} else {'prepared'});
        slotConflictReservationPreservedVerified=[bool]$SlotConflict;slotConflictTimerGapSeconds=$(if ($SlotConflict) {$slotConflictGap} else {$null});
        preparedFaultMode=$preparedFaultMode;prepareAckRetryVerified=[bool]$LosePrepareAck;consumeAckRetryVerified=($LosePickupAck -and -not $LegacyProtocol);
        backendSourceSnapshotVerified=(-not $LegacyProtocol);nativeAndAssetLiveStabilityVerified=$true;
        serverDropId=$dropId;journalV2BeforePrepareVerified=(-not $LegacyProtocol);emptyBodyConsumeVerified=(-not $LegacyProtocol);
        postgresConsumedOriginVerified=(-not $LegacyProtocol);preparedPlayerAccessDeniedVerified=(-not $LegacyProtocol);
        preparedFaultAndUERestartVerified=$false;clients=$ClientCount;authenticatedAdmission=$true;actualSavedNPCDeath=$true;
        transientGuaranteedDropRow=$true;nativeDamageSourceFixture=$true;pausedNPCFixture=$true;positionedActorsFixture=$true;
        startupLeaseHeartbeatFixture=$true;startupLeaseHeartbeatCount=$startupLeaseHeartbeatCount;naturalColdStartupWithinDefaultLeaseVerified=$false;
        editorAutoSDKSetupSkippedFixture=$true;
        scriptedInteraction=$true;serverObservationFileBarrierFixture=$true;existingPickupRPC=$true;queuedDuplicateRejected=$true;postgresItemAndReceiptVerified=$true;
        ownedUnrealLogsFrozen=$true;proofIntegrityVerified=$true;
        exactHTTPReplayVerified=$true;ownedBackendRestartRecoveryVerified=$true;unknownAckRetryVerified=[bool]$LosePickupAck;
        nativeGrantTimerRetryVerified=[bool]$LosePickupAck;nativeGrantTimerGapSeconds=$(if ($LosePickupAck) {$gap} else {$null});
        nativeGatewayAttempts=$(if ($LosePickupAck) {$gatewayAttempts} else {$null});
        committedBeforeMissingAckVerified=[bool]$LosePickupAck;pendingPickupAndEmptyProjectionVerified=[bool]$LosePickupAck;
        frozenHTTPBodyRetryVerified=([bool]$LosePickupAck -or [bool]$LosePrepareAck);faultProxyState=$proxySnapshot;
        multipleClaimantsVerified=($ClientCount -gt 1);sameReplicatedPickupPreparedBeforeSend=($ClientCount -gt 1);
        ownerIsolationVerified=$true;clientPacketLagMilliseconds=$(if ($ClientCount -gt 1) {250} else {0});packetLossPercent=0;
        livePickupRejectionsBeforeRecoveryVerified=[bool]$LosePickupAck;inFlightRejectionTimingVerified=$false;worldDropServerRestartRecoveryVerified=$false;steamVerified=$false;shippingRuntimeVerified=$false;
        sessionId=$sessionId;characterId=$characterId;itemId=$itemId;requestId=$requestId;rejectedRequestId=$rejectedId;
        rejectedRequestIds=$rejectedIds;claimants=$(if ($ClientCount -gt 1) {$observation.claimants} else {@([ordered]@{characterId=$characterId;won=$true})});
        npcNetId=$npcNetId;pickupNetId=$pickupNetId;definitionName=$definition;moduleSHA256=$moduleHash;
        runnerSHA256=$runnerHash;partyProofSHA256=$partyProofHash;backendModuleSHA256=$backendModuleHash;sourceAndAssetHashes=$hashes;serverLogSHA256=$serverLogHash;
        clientLogs=$clientLogHashes;clientLogSHA256=$(if ($ClientCount -eq 1) {$clientLogHashes[0].SHA256} else {$null})} | ConvertTo-Json -Depth 7 |
        Set-Content -LiteralPath (Join-Path $runRoot 'NPCPickupBackend.proof.json') -Encoding UTF8
    Write-Host "PASS: $ClientCount authenticated client(s), saved NPC/drop -> normal pickup RPC -> single PostgreSQL grant, claimant/duplicate rejection, owner isolation, exact HTTP replay and owned backend restart. $runRoot"
} catch {
    [ordered]@{schemaVersion=1;passed=$false;reason=$_.Exception.Message;moduleSHA256=$moduleHash;runnerSHA256=$runnerHash;
        sessionId=$sessionId;serverRestartAttempt=[bool]$RestartServerAfterLostAck;sourceAndAssetHashes=$hashes} | ConvertTo-Json -Depth 7 |
        Set-Content -LiteralPath (Join-Path $runRoot 'Failure.proof.json') -Encoding UTF8
    throw
} finally {
    if ($sessionId -and $serverHeaders) {
        try { $null=Invoke-Api "dungeon-sessions/$sessionId/finish" 'Post' $serverHeaders @{serverId=$serverId;outcome='Failed'} } catch { Write-Warning 'Could not finish this fixture session; generated records are retained.' }
    }
    foreach ($proc in $processes) {
        $proc.Refresh(); if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -ErrorAction SilentlyContinue; $proc.WaitForExit(10000) | Out-Null }
        $proc.Dispose()
    }
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name,$previousEnv[$name],'Process') }
    # Never delete rows, volumes, assets or stop another run's Docker container.
}
