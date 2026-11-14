[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N')
$proofFolder = 'TradeTargetUi-20261109'
$reservation = 'trading-target-ui-20261109'
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/'+$proofFolder+'/Integration-'+$run)
$tempRoot=Join-Path $workspace 'Saved/TU'
$temp=Join-Path $tempRoot $run.Substring(0,8)
$project=Join-Path $temp 'Project'
$proof=[ordered]@{Passed=$false;Scope='Full published project UHT and focused integration CPP compile, no module link/runtime';Head='';Inputs=@();Cleanup=$null;Failure=$null}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof { [IO.File]::WriteAllText((Join-Path $proofRoot 'Result.json'),($proof|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false)) }
function Assert-Reservation {
    if (@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match ('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')}).Count -ne 1) { throw 'Trading target UI reservation missing' }
}
try {
    Assert-Reservation
    $null=[IO.Directory]::CreateDirectory($project)
    $null=[IO.Directory]::CreateDirectory($proofRoot)
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $paths=@('Source','Config','Project_RPG.uproject')
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only HEAD -- Plugins)
    foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})) {
        $paths+=$descriptor
        $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($name in @('Source','Config','Resources','Shaders')) {
            $child=$dir+'/'+$name
            if (@($pluginFiles|Where-Object {$_.StartsWith($child+'/')}).Count) {$paths+=$child}
        }
    }
    $archive=Join-Path $temp 'Inputs.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths
    if($LASTEXITCODE -ne 0){throw 'Source archive failed'}
    & tar -xf $archive -C $project
    if($LASTEXITCODE -ne 0){throw 'Source extraction failed'}
    $files=@('Source/Project_RPG/Public/Trading/RPGTradePreview.h','Source/Project_RPG/Private/Trading/RPGTradePreview.cpp','Source/Project_RPG/Private/Trading/RPGTradeJson.h','Source/Project_RPG/Public/Trading/RPGTradeClient.h','Source/Project_RPG/Private/Trading/RPGTradeClient.cpp','Source/Project_RPG/Public/Trading/RPGTradeTransport.h','Source/Project_RPG/Private/Trading/RPGTradeTransport.cpp','Source/Project_RPG/Public/Trading/RPGTradeCoordinator.h','Source/Project_RPG/Private/Trading/RPGTradeCoordinator.cpp','Source/Project_RPG/Public/Trading/RPGTradeLobbyOwner.h','Source/Project_RPG/Private/Trading/RPGTradeLobbyOwner.cpp','Source/Project_RPG/Public/Trading/RPGTradeSubsystem.h','Source/Project_RPG/Private/Trading/RPGTradeSubsystem.cpp','Source/Project_RPG/Public/UI/Lobby/RPGTradeWidget.h','Source/Project_RPG/Public/UI/Lobby/RPGTradingWindowLayout.h','Source/Project_RPG/Private/UI/Lobby/RPGTradingWindowLayout.cpp','Source/Project_RPG/Private/UI/Lobby/RPGTradeWidget.cpp','Source/Project_RPG/Public/Trading/RPGTradeOfferDraft.h','Source/Project_RPG/Private/Trading/RPGTradeOfferDraft.cpp')
    $files+=@('Source/Project_RPG/Public/Trading/RPGTradeTargets.h','Source/Project_RPG/Private/Trading/RPGTradeTargets.cpp')
    foreach($relative in $files) {
        $source=Join-Path $workspace $relative
        $destination=Join-Path $project $relative
        $sha=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
        if((Get-FileHash -LiteralPath $destination).Hash -ne $sha -or (Get-FileHash -LiteralPath $source).Hash -ne $sha){throw 'Overlay changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$sha}
    }
    $fileList=Join-Path $temp 'CompileFiles.txt'
    $compile=@('Source/Project_RPG/Private/UI/Lobby/RPGTradingWindowLayout.cpp','Source/Project_RPG/Private/Trading/RPGTradeTransport.cpp','Source/Project_RPG/Private/Trading/RPGTradeCoordinator.cpp','Source/Project_RPG/Private/Trading/RPGTradeLobbyOwner.cpp','Source/Project_RPG/Private/UI/Lobby/RPGTradeWidget.cpp')
    [IO.File]::WriteAllLines($fileList,@($compile|ForEach-Object {Join-Path $project $_}),[Text.UTF8Encoding]::new($false))
    $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $args=@($ubt,'Project_RPGEditor','Win64','Development',(Join-Path $project 'Project_RPG.uproject'),
        ('-FileList='+$fileList),'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),
        ('-UBATraceOutputFile='+(Join-Path $temp 'Build.uba')),'-NoPCH','-NoHotReloadFromIDE','-MaxParallelActions=1',('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.Command=@($dotnet)+$args
    Save-Proof
    Write-Output ('Compiling actual trading discovery transport/coordinator/owner/widget; proof: '+$proofRoot)
    & $dotnet @args *> (Join-Path $proofRoot 'BuildConsole.log')
    $proof.BuildExit=$LASTEXITCODE
    if($LASTEXITCODE -ne 0){throw 'Integration compile failed'}
    foreach($inputFile in $proof.Inputs) {
        if((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256){throw 'Original integration input changed'}
    }
    $proof.Passed=$true
} catch { $proof.Failure=$_.Exception.Message }
finally {
    try {
        Assert-Reservation
        if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
    } catch {$proof.Failure=$_.Exception.Message}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
    $proof.Passed=$proof.Passed -and !$proof.Failure -and $proof.CleanupComplete
    $proof.HistoricalHashesOnly=$true
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if(!$proof.Passed){throw $proof.Failure}
