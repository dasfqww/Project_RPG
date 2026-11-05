[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024,65535)][int]$BackendPort = 3058,
    [ValidateRange(1024,65535)][int]$GamePort = 18858,
    [ValidateRange(60,600)][int]$TimeoutSeconds = 240,
    [switch]$BackendFixtureOnly,
    [switch]$VerifyCombat,
    [switch]$VerifyContributionHUD,
    [ValidateSet('FullCharge','QuickCharge')][string]$ChargeChoice = 'FullCharge',
    [string]$RuntimeProjectRoot = '',
    [string]$BackendBuildDirectory = ''
)

# Isolated development authentication/Memory, real ticket admission, authored
# boss-map Pawn and Iris. Combat mode uses stationary targets and authored hits.
$ErrorActionPreference = 'Stop'
if ($VerifyContributionHUD -and (-not $VerifyCombat -or $BackendFixtureOnly)) {
    throw 'Contribution HUD observation requires VerifyCombat and actual UE peers.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($RuntimeProjectRoot)) {$RuntimeProjectRoot=$projectRoot}
$RuntimeProjectRoot=[IO.Path]::GetFullPath($RuntimeProjectRoot)
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $RuntimeProjectRoot 'Project_RPG.uproject'
$module = Join-Path $RuntimeProjectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$backendProject = Join-Path $projectRoot 'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj'
$dotnet = (Get-Command dotnet).Source
foreach ($path in @($editor,$project,$module,$backendProject)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing prerequisite: $path" }
}
if (-not [string]::IsNullOrWhiteSpace($BackendBuildDirectory)) {
    $BackendBuildDirectory=[IO.Path]::GetFullPath($BackendBuildDirectory)
    foreach ($name in @('ProjectRpg.Backend.dll','ProjectRpg.Backend.deps.json','ProjectRpg.Backend.runtimeconfig.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $BackendBuildDirectory $name))) {throw "Missing prebuilt backend prerequisite: $name"}
    }
}
if ($RuntimeProjectRoot -ine $projectRoot) {
    foreach ($relativePath in @('Project_RPG.uproject','Config/DefaultGame.ini','Config/DefaultEngine.ini')) {
        if ((Get-FileHash -LiteralPath (Join-Path $RuntimeProjectRoot $relativePath)).Hash -cne
            (Get-FileHash -LiteralPath (Join-Path $projectRoot $relativePath)).Hash) { throw "Runtime copy differs: $relativePath" }
    }
}
if (@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() |
    Where-Object Port -eq $BackendPort).Count -or
    @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners() |
    Where-Object Port -eq $GamePort).Count) { throw 'Requested fixture port is already in use.' }
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $projectRoot "Saved\Tests\CharacterProgressionRestore\$runId"
$runtimeUserRoot = Join-Path $projectRoot "Saved\Tests\PR\$($runId.Substring(0,12))"
# Leave room for scoped admission journals and their UUID temporary filenames.
$maximumJournalPath = Join-Path $runtimeUserRoot "s\Saved\Online\DungeonDisconnections\$('a'*32)\$('b'*32)\$([Guid]::Empty)\ConsumeRecoveries"
if ($maximumJournalPath.Length + 46 -ge 260) { throw 'Workspace path is too long for the isolated journal fixture.' }
if (Test-Path -LiteralPath $runtimeUserRoot) { throw 'Refusing to reuse an existing runtime directory.' }
New-Item -ItemType Directory -Path $runtimeUserRoot | Out-Null
$buildRoot = Join-Path $runRoot 'backend-build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$baseUrl = "http://127.0.0.1:$BackendPort"
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$adminToken = [Guid]::NewGuid().ToString('N')
$environmentOverrides = @{
    ASPNETCORE_ENVIRONMENT = 'Development'; PROJECT_RPG_BACKEND_ADMIN_TOKEN = $adminToken
    POSTGRES_CONNECTION_STRING = $null; ConnectionStrings__Postgres = $null; Storage__Provider = 'Memory'
    Kestrel__Endpoints__Http__Url = $baseUrl; Steam__AllowDevelopmentAuthentication = 'true'
    PROJECT_RPG_GAME_SERVER_ID = $null; PROJECT_RPG_DUNGEON_SESSION_ID = $null; PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN = $null
    DungeonContent__Entries__1__DungeonId = 'Progression.Training'
    DungeonContent__Entries__1__MapPackage = '/Game/Maps/testmap'
    DungeonContent__Entries__1__Difficulties__0 = 'Normal'
}
$sourcePaths = @(
    'Source/Project_RPG/Public/Combat/RPGCombatContributionTypes.h',
    'Source/Project_RPG/Public/Component/Combat/RPGCombatContributionComponent.h',
    'Source/Project_RPG/Private/Component/Combat/RPGCombatContributionComponent.cpp',
    'Source/Project_RPG/Private/Component/Combat/RPGCombatControlComponent.cpp',
    'Source/Project_RPG/Public/Skill/RPGSkillDefinition.h',
    'Source/Project_RPG/Public/Character/RPGNonPlayerCharacter.h',
    'Source/Project_RPG/Private/Character/RPGNonPlayerCharacter.cpp',
    'Source/Project_RPG/Private/Tests/RPGSkillDamageNetworkProbe.h',
    'Source/Project_RPG/Private/Tests/RPGSkillDamageSecurityTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGSkillContributionContentTests.cpp',
    'Scripts/Editor/ConfigureGreatSwordContributionTripods.py',
    'Config/DefaultGameplayTags.ini',
    'Content/Blueprints/Character/NPC/DummyNPC/BP_Dummy_Gdn.uasset',
    'Source/Project_RPG/Public/Type/RPGStructTypes.h',
    'Source/Project_RPG/Public/Skill/RPGSkillRuntimeTypes.h',
    'Source/Project_RPG/Private/Skill/RPGSkillRuntimeTypes.cpp',
    'Source/Project_RPG/Private/Skill/RPGSkillDefinition.cpp',
    'Source/Project_RPG/Private/Tests/RPGSkillCooldownBaselineTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGSkillRuntimeSpecTests.cpp',
    'Scripts/Editor/ConfigureGreatSwordCombatBaseline.py',
    'Source/Project_RPG/Private/Tests/RPGCharacterProgressionRestoreE2E.h',
    'Source/Project_RPG/Private/Tests/RPGCharacterProgressionRestoreE2E.cpp',
    'Source/Project_RPG/Private/Tests/RPGCharacterProgressionCombatProbe.h',
    'Source/Project_RPG/Private/Tests/RPGCharacterProgressionCombatProbe.cpp',
    'Source/Project_RPG/Private/Tests/RPGSkillDamageNetworkProbe.cpp',
    'Source/Project_RPG/Private/Ability/Skill/RPGGreatSwordResolveAbility.cpp',
    'Source/Project_RPG/Private/Ability/RPGPlayerGameplayAbility.cpp',
    'Source/Project_RPG/Public/Component/Combat/PawnCombatComponent.h',
    'Source/Project_RPG/Private/Component/Combat/PawnCombatComponent.cpp',
    'Source/Project_RPG/Private/Tests/RPGEquipmentCombatTests.cpp',
    'Source/Project_RPG/Private/Progression/RPGCharacterProgressionComponent.cpp',
    'Source/Project_RPG/Public/Progression/RPGCharacterProgressionComponent.h',
    'Source/Project_RPG/Private/Progression/RPGCharacterProgressionSnapshot.cpp',
    'Source/Project_RPG/Private/Manager/HttpWebManagerProgression.cpp',
    'Source/Project_RPG/Private/Character/RPGPlayer.cpp',
    'Source/Project_RPG/Private/PlayerClass/RPGPlayerClassComponent.cpp',
    'Source/Project_RPG/Private/Component/Skill/RPGPlayerSkillComponent.cpp',
    'Source/Project_RPG/Private/Component/UI/QuickSlotComponent.cpp',
    'Source/Project_RPG/Private/Skill/QuickSlot/RPGSkillQuickSlotServices.cpp',
    'Source/Project_RPG/Private/Skill/RPGClassSkillEligibility.cpp',
    'Source/Project_RPG/Private/Component/Equipment/RPGAuthoritativeEquipmentComponent.cpp',
    'Source/Project_RPG/Private/Component/Equipment/RPGPlayerWeaponLoadoutComponent.cpp',
    'Source/Project_RPG/Private/Skill/RPGGameplayAbility_SkillContainer.cpp',
    'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
    'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
    'Source/Project_RPG/Private/Player/RPGPlayerState.cpp',
    'Config/DefaultGame.ini','Config/DefaultEngine.ini',
    'Content/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior.uasset',
    'Content/Blueprints/GameData/Skill/DA_Skill_AssultBlade.uasset',
    'Content/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.uasset',
    'Content/Blueprints/GameData/Skill/DA_Skill_WhirlWind.uasset',
    'Content/Blueprints/GameData/Skill/DA_Skill_JumpSmash.uasset',
    'Content/Blueprints/GameData/Skill/DT_Skill.uasset',
    'Content/Blueprints/Character/Player/BP_Player.uasset',
    'Content/Blueprints/Character/Player/Abilities/Skill/GA_PlayerSkill_AssultBlade.uasset',
    'Content/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash.uasset',
    'Content/Blueprints/Character/Player/Abilities/Skill/GA_Player_JumpSmash.uasset',
    'Content/Blueprints/Character/Player/Abilities/Skill/GA_Player_WhirlWind.uasset',
    'Content/Blueprints/Controller/BP_PlayerController.uasset',
    'Scripts/Tests/Run-CharacterProgressionRestoreE2ETest.ps1'
)
if ($VerifyContributionHUD) {
    $sourcePaths += @(
        'Source/Project_RPG/Public/UI/Content/RPGCombatContributionWidget.h',
        'Source/Project_RPG/Private/UI/Content/RPGCombatContributionWidget.cpp',
        'Source/Project_RPG/Public/UI/HUD/RPGHUD.h',
        'Source/Project_RPG/Private/UI/HUD/RPGHUD.cpp',
        'Source/Project_RPG/Public/Controller/RPGPlayerController.h',
        'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp'
    )
}
$moduleTime = (Get-Item -LiteralPath $module).LastWriteTimeUtc
foreach ($source in $sourcePaths | Where-Object { [IO.Path]::GetExtension($_) -in @('.h','.cpp') }) {
    if (-not $BackendFixtureOnly -and (Get-Item -LiteralPath (Join-Path $projectRoot $source)).LastWriteTimeUtc -gt $moduleTime) { throw "Build changed source first: $source" }
}
$sourceHashes = @($sourcePaths | ForEach-Object {
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash}
})
$moduleHash = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
[ordered]@{moduleSha256=$moduleHash;selectedSources=$sourceHashes} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $runRoot 'InputsBeforeRun.json') -Encoding UTF8

function Invoke-Api {
    param([string]$Path,[string]$Method='Get',[hashtable]$Headers=@{},[object]$Body=$null)
    $parameters=@{Uri="$baseUrl/api/$Path";Method=$Method;Headers=$Headers;TimeoutSec=10;UseBasicParsing=$true}
    if ($null -ne $Body) { $parameters.ContentType='application/json';$parameters.Body=$Body | ConvertTo-Json -Depth 8 -Compress }
    try { $response=Invoke-WebRequest @parameters }
    catch {
        $status=if ($_.Exception.Response) {[int]$_.Exception.Response.StatusCode} else {0}
        throw "Fixture HTTP $Method $Path failed (HTTP $status); credentials and response body omitted."
    }
    if ($response.Content) { return $response.Content | ConvertFrom-Json }
}
function Start-OwnedProcess {
    param([string]$File,[string[]]$Arguments,[string]$Directory,[hashtable]$Environment=@{},[string]$OutputLog='')
    $prior=@{}
    try {
        foreach ($name in $Environment.Keys) {
            $prior[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
            [Environment]::SetEnvironmentVariable($name,$Environment[$name],'Process')
        }
        $parameters=@{FilePath=$File;ArgumentList=$Arguments;WorkingDirectory=$Directory;PassThru=$true;WindowStyle='Hidden'}
        if ($OutputLog) {$parameters.RedirectStandardOutput=$OutputLog;$parameters.RedirectStandardError=$OutputLog+'.stderr'}
        $process=Start-Process @parameters
        $processes.Add($process)
        return $process
    } finally {
        foreach ($name in $prior.Keys) {[Environment]::SetEnvironmentVariable($name,$prior[$name],'Process')}
    }
}
function Start-Unreal {
    param([string]$Name,[string]$Destination,[string[]]$Extra,[hashtable]$Environment=@{})
    $arguments=@(('"'+$project+'"'),$Destination)+$Extra+@(
        '-UseIrisReplication=1','-RPGRequireIris','-unattended','-nop4','-nosplash','-nosound','-nosteam',
        '-NoLiveCoding','-NoLoadingScreen','-DisablePython','-DDC=(EnginePak,Local)','-nullrhi',
        '-RPGProgressionRestoreE2E',"-RPGRestoreCharacter=$characterId","-RPGRestoreSteam=$steamId","-RPGRestoreSession=$sessionId",
        "-RPGRestoreChargeChoice=$ChargeChoice",
        ('-RPGRestoreGates="'+$runRoot+'"'),('-UserDir="'+(Join-Path $runtimeUserRoot $(if ($Name -eq 'server') {'s'} else {'c'}))+'"'),
        ('-abslog="'+(Join-Path $runRoot "$Name.log")+'"'),'-ExecCmds="t.MaxFPS 60"',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$baseUrl/api",
        "-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:ApiUrl=$baseUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$baseUrl/api")
    if ($VerifyCombat) {$arguments+='-RPGProgressionCombatE2E'}
    if ($VerifyContributionHUD) {$arguments+='-RPGProgressionContributionHUD'}
    # Native UMG ticks need a drawn viewport; null RHI leaves its geometry at zero.
    if ($VerifyContributionHUD -and $Name -eq 'client') {
        $arguments=@($arguments | Where-Object {$_ -ne '-nullrhi'}) + @('-RenderOffscreen','-Windowed','-ResX=1280','-ResY=720','-ForceRes')
    }
    return Start-OwnedProcess $editor $arguments $projectRoot $Environment
}
function Maintain-OwnedStartupLease {
    if ([DateTime]::UtcNow -lt $script:nextStartupHeartbeat) {return}
    $heartbeat=Invoke-Api "$sessionPath/heartbeat" 'Post' $serverHeaders @{serverId=$serverId}
    if ($heartbeat.dungeonSessionId -cne $sessionId -or $heartbeat.serverId -cne $serverId -or
        $heartbeat.state -notin @('Loading','InProgress')) {throw 'Owned startup heartbeat scope mismatch.'}
    $script:startupHeartbeats++
    $script:nextStartupHeartbeat=[DateTime]::UtcNow.AddSeconds(10)
}
try {
    $processEnvironment=[Environment]::GetEnvironmentVariables('Process')
    $pathKeys=@($processEnvironment.Keys | Where-Object { $_.ToString() -ieq 'Path' })
    if ($pathKeys.Count -gt 1) {
        $pathValue=[string]$processEnvironment[$pathKeys[0]]
        foreach ($key in $pathKeys) {[Environment]::SetEnvironmentVariable($key.ToString(),$null,'Process')}
        [Environment]::SetEnvironmentVariable('Path',$pathValue,'Process')
    }
    if ([string]::IsNullOrWhiteSpace($BackendBuildDirectory)) {
        & $dotnet build $backendProject -c Release --artifacts-path (Join-Path $runRoot 'backend-artifacts') --output $buildRoot *> (Join-Path $runRoot 'backend-build.log')
        if ($LASTEXITCODE -ne 0) {throw 'Isolated backend build failed.'}
    } else {
        Get-ChildItem -LiteralPath $BackendBuildDirectory | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $buildRoot -Recurse
        }
    }
    $backend=Start-OwnedProcess $dotnet @(('"'+(Join-Path $buildRoot 'ProjectRpg.Backend.dll')+'"')) $buildRoot $environmentOverrides (Join-Path $runRoot 'backend.stdout.log')
    $ready=$false
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($backend.HasExited) {throw 'Owned backend exited before readiness.'}
        try {$health=Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 2;if ($health.status -eq 'ready' -and $health.storageProvider -eq 'Memory') {$ready=$true;break}} catch {}
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) {throw 'Memory backend not ready.'}
    $listeners=@(Get-NetTCPConnection -State Listen -LocalPort $BackendPort | Where-Object {$_.OwningProcess -eq $backend.Id -and $_.LocalAddress -eq '127.0.0.1'})
    if ($listeners.Count -ne 1) {throw 'Readiness listener is not the owned backend.'}
    $steamId=([uint64]76561300000000000+[uint64][Convert]::ToUInt32($runId.Substring(0,8),16)).ToString()
    $auth=Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steamId"}
    $playerHeaders=@{Authorization='Bearer '+$auth.accessToken}
    $character=Invoke-Api 'characters' 'Post' $playerHeaders @{name=('Restore'+$runId.Substring(0,10));classId='RPGPlayerClassDefinition:GreatSwordWarrior'}
    $characterId=[string]$character.characterId
    $sibling=Invoke-Api 'characters' 'Post' $playerHeaders @{name=('Sibling'+$runId.Substring(0,10))}
    $siblingBefore=Invoke-Api "characters/$($sibling.characterId)/progression" 'Get' $playerHeaders
    $adminHeaders=@{Authorization='Bearer '+$adminToken}
    # Earn the recipe through the existing real settlement/read path, never by writing storage.
    $training=Invoke-Api 'dungeon-sessions' 'Post' $playerHeaders @{characterId=$characterId;dungeonId='Progression.Training';difficulty='Normal'}
    $trainingPath='dungeon-sessions/'+$training.dungeonSessionId
    $null=Invoke-Api "$trainingPath/departure" 'Post' $playerHeaders @{characterId=$characterId;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$training.lobbyRevision}
    $assignment=Invoke-Api "$trainingPath/activate" 'Post' $adminHeaders @{serverId=('training-'+$runId);serverAddress='127.0.0.1:7799'}
    $trainingHeaders=@{Authorization='Bearer '+$assignment.gameServerAccessToken}
    $null=Invoke-Api "$trainingPath/start" 'Post' $trainingHeaders @{serverId=('training-'+$runId)}
    $null=Invoke-Api "$trainingPath/settle-rewards" 'Post' $trainingHeaders @{serverId=('training-'+$runId);rewardVersion='FirstSlice.v1';changes=@();itemRewards=@()}
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do {
        $training=Invoke-Api $trainingPath 'Get' $playerHeaders
        if ($training.state -eq 'Cleared') {break}
        if ($training.state -eq 'Failed') {throw 'Training settlement failed.'}
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($training.state -ne 'Cleared') {throw 'Training settlement timed out.'}
    $record=Invoke-Api "characters/$characterId/progression" 'Get' $playerHeaders
    if ($record.experience -ne 100 -or $record.totalSP -ne 3 -or $record.revision -ne '2') {throw 'Training recipe mismatch.'}
    $skills=@($record.skills | ForEach-Object {@{skillTag=$_.skillTag;level=$(if ($_.skillTag -eq 'Player.Ability.Skill.Charge') {4} else {1});tripods=$(if ($_.skillTag -eq 'Player.Ability.Skill.Charge') {@($ChargeChoice,'')} else {@('','')})}})
    $slots=@('Player.Ability.Skill.Charge','Player.Ability.Skill.AssultBlade','','','','','','Player.Ability.Skill.WhirlWind')
    $saved=Invoke-Api "characters/$characterId/skill-configuration" 'Put' $playerHeaders @{requestId=[Guid]::NewGuid().ToString('D');expectedRevision=[string]$record.revision;skills=$skills;skillSlots=$slots}
    if ($saved.revision -ne '3' -or $saved.usedSP -ne 3) {throw 'Saved recipe mismatch.'}
    if ($BackendFixtureOnly) {
        $saved | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'BackendFixture.proof.json') -Encoding UTF8
        Write-Host "PROGRESSION_RESTORE_FIXTURE PASS BackendOnly=1 NoUnrealRuntime=1 Proof=$runRoot"
        return
    }
    $session=Invoke-Api 'dungeon-sessions' 'Post' $playerHeaders @{characterId=$characterId;dungeonId='Dungeon.RaidSwordMan';difficulty='Normal'}
    $sessionId=[string]$session.dungeonSessionId
    $sessionPath="dungeon-sessions/$sessionId"
    $null=Invoke-Api "$sessionPath/departure" 'Post' $playerHeaders @{characterId=$characterId;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$session.lobbyRevision}
    $serverId='restore-'+$runId
    $assignment=Invoke-Api "$sessionPath/activate" 'Post' $adminHeaders @{serverId=$serverId;serverAddress="127.0.0.1:$GamePort"}
    $serverHeaders=@{Authorization='Bearer '+$assignment.gameServerAccessToken}
    $script:nextStartupHeartbeat=[DateTime]::MinValue
    $script:startupHeartbeats=0
    $serverEnvironment=@{PROJECT_RPG_GAME_SERVER_ID=$serverId;PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken}
    $server=Start-Unreal 'server' '/Game/Maps/bossmap' @('-server',"-port=$GamePort",'-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity') $serverEnvironment
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($server.HasExited) {throw 'Owned server exited before start.'}
        Maintain-OwnedStartupLease
        $session=Invoke-Api $sessionPath 'Get' $playerHeaders
        if ($session.state -eq 'InProgress') {break}
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($session.state -ne 'InProgress') {throw 'Actual server did not start assigned session.'}
    $client=Start-Unreal 'client' '/Game/Maps/LobbyMap' @('-game','-RPGDevelopmentLogin') @{
        PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null}
    $castReleased=$false;$passed=$false
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in @($backend,$server,$client)) {if ($process.HasExited) {throw 'An owned process exited before proof completion.'}}
        if (-not $castReleased) {Maintain-OwnedStartupLease}
        $serverText=Get-Content -LiteralPath (Join-Path $runRoot 'server.log') -Raw -ErrorAction SilentlyContinue
        $clientText=Get-Content -LiteralPath (Join-Path $runRoot 'client.log') -Raw -ErrorAction SilentlyContinue
        if (($serverText+$clientText) -match 'PROGRESSION_RESTORE_E2E FAIL|: Error:|Fatal error:|Assertion failed:|SecurityViolation|Rejected replicated target') {throw 'UE restore probe or runtime failed; inspect scoped logs.'}
        if (-not $castReleased -and $serverText -match 'PROGRESSION_RESTORE_E2E RESTORED Role=Server' -and $clientText -match 'PROGRESSION_RESTORE_E2E RESTORED Role=Client') {
            Set-Content -LiteralPath (Join-Path $runRoot 'cast.ready') -Value 'ready' -Encoding ASCII
            $castReleased=$true
        }
        if ($VerifyCombat) {
            if ($serverText -match 'PROGRESSION_COMBAT_E2E PASS Role=Server' -and $clientText -match 'PROGRESSION_COMBAT_E2E PASS Role=Client' -and
                (-not $VerifyContributionHUD -or $clientText -match 'PROGRESSION_CONTRIBUTION_HUD PASS AutoViewport=1 Rounds=7 Switch=1 Unlock=1 Completion=1')) {$passed=$true;break}
        } elseif ($serverText -match 'PROGRESSION_RESTORE_E2E PASS Server=1' -and $clientText -match 'PROGRESSION_RESTORE_E2E RELEASE Slot=0') {$passed=$true;break}
        Start-Sleep -Milliseconds 250
    }
    if (-not $passed) {throw 'Restore/input proof timed out.'}
    $final=Invoke-Api "characters/$characterId/progression" 'Get' $playerHeaders
    $siblingAfter=Invoke-Api "characters/$($sibling.characterId)/progression" 'Get' $playerHeaders
    if (($final | ConvertTo-Json -Depth 8 -Compress) -cne ($saved | ConvertTo-Json -Depth 8 -Compress) -or
        ($siblingBefore | ConvertTo-Json -Depth 8 -Compress) -cne ($siblingAfter | ConvertTo-Json -Depth 8 -Compress)) {throw 'Combat changed persistent configuration or sibling.'}
    Set-Content -LiteralPath (Join-Path $runRoot 'stop.ready') -Value 'ready' -Encoding ASCII
    foreach ($ownedUnreal in @($client,$server)) {
        if (-not $ownedUnreal.WaitForExit(30000) -or $ownedUnreal.ExitCode -ne 0) {throw 'Owned UE did not exit cleanly after proof.'}
    }
    $sourceChanges=@($sourceHashes | Where-Object {(Get-FileHash -LiteralPath (Join-Path $projectRoot $_.path) -Algorithm SHA256).Hash -cne $_.sha256})
    $moduleAfter=(Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
    $errorLines=@(Select-String -Path (Join-Path $runRoot 'server.log'),(Join-Path $runRoot 'client.log') -Pattern ': Error:|Fatal error:|Assertion failed:|Ensure condition failed:')
    if ($sourceChanges.Count -or $moduleAfter -cne $moduleHash -or $errorLines.Count) {throw 'Provenance or strict runtime log audit failed.'}
    $combatProof=$null
    if ($VerifyCombat) {
        $combatProof=Get-Content -LiteralPath (Join-Path $runRoot 'Combat.proof.json') -Raw | ConvertFrom-Json
        if ($combatProof.choice -cne $ChargeChoice -or $combatProof.successfulHits -ne 6 -or $combatProof.identitySpent -ne 50 -or $combatProof.finalGauge -ne 50 -or
            $combatProof.empoweredDamage -le $combatProof.baseDamage -or $combatProof.cooldown -ne 12 -or $combatProof.manaCost -ne 0 -or
            $combatProof.stagger -ne 100 -or $combatProof.partBreak -ne 100 -or $combatProof.excludedContributionTargets -ne 3) {throw 'Combat proof mismatch.'}
    }
    $hudProof=$null
    if ($VerifyContributionHUD) {
        $hudProof=Get-Content -LiteralPath (Join-Path $runRoot 'ContributionHUD.proof.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $hudProof.autoViewport -or -not $hudProof.nativeTickOnly -or $hudProof.observedRounds -ne 7 -or
            -not $hudProof.targetSwitch -or -not $hudProof.unlockCleared -or -not $hudProof.returnedToTarget -or -not $hudProof.completion -or
            ($hudProof.units -join ',') -cne '0,20,40,60,80,100,100') {throw 'Contribution HUD proof mismatch.'}
    }
    [ordered]@{passed=$true;scope=$(if($VerifyContributionHUD){'Development/Memory/real HTTP ticket admission/authored boss-map Pawn/Iris; nullRHI server and offscreen rendered client'}else{'Development/Memory/real HTTP ticket admission/authored boss-map Pawn/Iris/nullRHI'});moduleSha256=$moduleHash;runtimeProjectRoot=$RuntimeProjectRoot;runtimeUserRoot=$runtimeUserRoot;
        sourceCount=$sourceHashes.Count;sourceChanges=$sourceChanges.Count;errors=$errorLines.Count;revision=$final.revision;
        serverAndOwnerRestored=$true;chargeChoice=$ChargeChoice;chargeRuntimeFromSavedLevel=$true;existingQuickSlotInput=$true;noDuplicateGrantOnRefresh=$true;
        combat=$combatProof;
        contributionHUD=$hudProof;backendBuildDirectory=$BackendBuildDirectory;
        backendDllSha256=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash;
        immutableCombatConfiguration=$true;siblingUnchanged=$true;
        serverExitCode=$server.ExitCode;clientExitCode=$client.ExitCode;
        startupHeartbeats=$startupHeartbeats;
        notVerified=@('Steam','PostgreSQL','visual GPU layout/physical input','natural boss victory','process restart/reconnect',$(if(-not $VerifyCombat){'damage/hit'}else{'positive mana costs; authored mana cost is zero'}))
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
    Write-Host "PROGRESSION_RESTORE_E2E PASS Proof=$runRoot"
} finally {
    foreach ($process in $processes) {
        if (-not $process.HasExited) {$process.Kill();$process.WaitForExit(10000) | Out-Null}
        $process.Dispose()
    }
}
