[CmdletBinding()]
param(
    [string] $EngineRoot = 'D:\UE_5.8',
    [ValidateRange(20,900)] [int] $TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$proofParser = Join-Path $PSScriptRoot 'NPCDamageLifecycleProof.ps1'
$parserHash = (Get-FileHash -LiteralPath $proofParser -Algorithm SHA256).Hash
. $proofParser
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
foreach ($path in @($editor,$project,$module)) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing prerequisite: $path" } }
$runRoot = Join-Path $projectRoot ('Saved\Tests\NPCDamageLifecycle\Automation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$moduleHash = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
$assetFiles=@('Content\Blueprints\Character\NPC\DummyNPC\BP_Dummy_Gdn.uasset',
    'Content\Blueprints\Character\NPC\Gruntling\Guardian\DA_Guardian.uasset',
    'Content\Blueprints\Character\NPC\Gruntling\GameplayAbility\GA_Guardian_Death.uasset',
    'Content\Blueprints\Character\Shared\GameplayAbility\GA_NPC_Death_Base.uasset',
    'Content\Blueprints\Character\Shared\GameplayEffect\GE_Shared_ApplyDamage.uasset',
    'Content\Blueprints\Character\NPC\Gruntling\GameplayEffect\GE_Guardian_StartUp.uasset',
    'Content\Blueprints\Character\NPC\GE_Enemy_Static.uasset',
    'Content\Blueprints\Character\NPC\Gruntling\Guardian\BP_Guardian_DropItemAbility.uasset',
    'Content\Blueprints\GameData\Item\DT_ItemDrop.uasset')
$assetHashes=@($assetFiles | ForEach-Object { $asset=Join-Path $projectRoot $_; [ordered]@{path=$asset;SHA256=(Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash} })
$process = $null
try {
    $runtimeArguments = @(('"'+$project+'"'), '/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.NPCDamage.AuthoredDeathAndRespawn"',
        # Observe the saved dissolve/Niagara path without opening a window. NullRHI
        # deliberately prevents Niagara component creation in a standalone world.
        '-TestExit="Automation Test Queue Empty"', '-unattended','-nop4','-nosplash','-nosound','-RenderOffscreen','-nosteam','-DisablePython',
        '-DDC=(EnginePak,Local)', ('-UserDir="'+(Join-Path $runRoot 'User')+'"'), ('-abslog="'+$logPath+'"'))
    $process = Start-Process -FilePath $editor -ArgumentList $runtimeArguments -PassThru -WindowStyle Hidden
    Write-Host "Started authored NPC lifecycle test (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "NPC lifecycle automation timed out: $logPath" }
        Start-Sleep -Milliseconds 250
    }
    if ($process.ExitCode -ne 0) { throw "Editor exit $($process.ExitCode): $logPath" }
    $log = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $combatProof = Get-NPCDamageLifecycleProof -Log $log
    if ($moduleHash -ne (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash) { throw 'Editor module changed during NPC test.' }
    if ($parserHash -ne (Get-FileHash -LiteralPath $proofParser -Algorithm SHA256).Hash) { throw 'NPC proof parser changed during test; rerun on one version.' }
    foreach ($asset in $assetHashes) {
        if ($asset.SHA256 -ne (Get-FileHash -LiteralPath $asset.path -Algorithm SHA256).Hash) { throw 'Authored NPC asset changed during test.' }
    }
    $proof=[ordered]@{schemaVersion=1;editorModuleSHA256=$moduleHash;logPath=$logPath
        logSHA256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash;validationParserSHA256=$parserHash;authoredAssetHashes=$assetHashes}
    foreach ($key in $combatProof.Keys) { $proof[$key] = $combatProof[$key] }
    $proofPath=Join-Path $runRoot 'NPCDamageLifecycle.proof.json'
    $proof | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: actual authored NPC death, corpse rejection and server-forced replacement isolation. $proofPath"
}
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
