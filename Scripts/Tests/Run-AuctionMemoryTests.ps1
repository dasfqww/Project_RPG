[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N')
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/AuctionMemory-20261112/'+$run)
$allowed=Join-Path $workspace 'Saved/AM'
$temp=Join-Path $allowed $run.Substring(0,8)
$inputs=Join-Path $temp 'Inputs'
$result=[ordered]@{Passed=$false;Scope='Actual Memory auction registration/return/purchase/claim with shared assets, rollback and concurrent commands, existing Memory trade HTTP/session regressions; no PG/auction API/UE or process restart persistence';Head='';Inputs=@();Checks=@();Failure=$null;Cleanup=$null}
$ownsTemp=$false
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof {
    $null=[IO.Directory]::CreateDirectory($proofRoot)
    [IO.File]::WriteAllText((Join-Path $proofRoot 'Result.json'),($result|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
}
function Assert-Reservation {
    $rows=@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object {$_ -match '^\|[^|]* / auction-memory-20261112 \|'})
    if($rows.Count -ne 1){throw 'Missing auction domain reservation'}
}
function Invoke-Logged([string[]]$Arguments,[string]$Label) {
    $previous=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & dotnet @Arguments *> (Join-Path $proofRoot ($Label+'.log'))
        $code=$LASTEXITCODE
    } finally {$ErrorActionPreference=$previous}
    $result.Checks+=@{Label=$Label;Command=@('dotnet')+$Arguments;Exit=$code}
    Get-Content -LiteralPath (Join-Path $proofRoot ($Label+'.log')) -Tail 8
    if($code -ne 0){throw ('Failed '+$Label)}
}
try {
    Assert-Reservation
    $ancestor=$temp
    while($ancestor){
        if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor rejected'}
        $ancestor=[IO.Path]::GetDirectoryName($ancestor)
    }
    if(Test-Path -LiteralPath $temp){throw 'Owned temporary path already exists'}
    $null=[IO.Directory]::CreateDirectory($inputs);$ownsTemp=$true
    $result.Head=(git -C $workspace rev-parse HEAD).Trim()
    & git -C $workspace archive --format=tar ('-o'+(Join-Path $temp 'Backend.tar')) $result.Head -- Backend
    if($LASTEXITCODE -ne 0){throw 'Backend archive failed'}
    & tar -xf (Join-Path $temp 'Backend.tar') -C $inputs
    if($LASTEXITCODE -ne 0){throw 'Archive extraction failed'}
    $files=@('Backend/ProjectRpg.Backend/Data/InMemoryTransactionGate.cs','Backend/ProjectRpg.Backend/Data/InMemoryItemRepository.cs','Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.Trading.cs','Backend/ProjectRpg.Backend/Data/InMemoryAuctionRepository.cs','Backend/ProjectRpg.Backend.TradingTests/AuctionMemoryCases.cs','Backend/ProjectRpg.Backend.TradingTests/MemorySessionCases.cs','Backend/ProjectRpg.Backend.TradingTests/Program.cs')
    foreach($file in $files){
        $source=Join-Path $workspace $file;$destination=Join-Path $inputs $file
        $hash=(Get-FileHash -LiteralPath $source).Hash
        Copy-Item -LiteralPath $source -Destination $destination
        if((Get-FileHash -LiteralPath $source).Hash -ne $hash -or (Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw 'Input changed during snapshot'}
        $result.Inputs+=@{Path=$file;SHA256=$hash}
    }
    $result.RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
    Save-Proof
    $project=Join-Path $inputs 'Backend/ProjectRpg.Backend.TradingTests/ProjectRpg.Backend.TradingTests.csproj'
    Invoke-Logged @('build',$project,'-c','Release','--disable-build-servers','-p:UseSharedCompilation=false') 'Build'
    $module=Join-Path $inputs 'Backend/ProjectRpg.Backend.TradingTests/bin/Release/net9.0/ProjectRpg.Backend.TradingTests.dll'
    $result.ModuleSHA256=(Get-FileHash -LiteralPath $module).Hash
    $result.BackendModuleSHA256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $module) 'ProjectRpg.Backend.dll')).Hash
    Invoke-Logged @($module,'--auction-memory') 'AuctionMemory'
    Invoke-Logged @($module,'--auction-domain') 'AuctionDomain'
    Invoke-Logged @($module,'--sessions-memory') 'MemorySessions'

    Invoke-Logged @($module,'--api-memory') 'MemoryHttp'
    Invoke-Logged @($module) 'DirectTradeRegression'
    foreach($file in $result.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $file.Path)).Hash -ne $file.SHA256){throw 'Validated input changed'}}
    if((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $result.RunnerSHA256){throw 'Runner changed during validation'}
    $result.Passed=$true
} catch {$result.Failure=$_.Exception.Message}
finally {
    try {
        Assert-Reservation
        if($ownsTemp -and (Test-Path -LiteralPath $temp)){$result.Cleanup=Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $allowed}
    } catch {$result.Failure=([string]$result.Failure+'; cleanup: '+$_.Exception.Message)}
    $result.TempDeleted=!(Test-Path -LiteralPath $temp)
    $result.HistoricalHashesOnly=$true
    $result.Passed=$result.Passed -and !$result.Failure -and $result.TempDeleted
    Save-Proof
    Write-Output ('PASSED='+$result.Passed+'; proof='+$proofRoot)
}
if(!$result.Passed){throw $result.Failure}
