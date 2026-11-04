[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# Leave room for the longer Operations.Core project name in nested fixture roots
# when this regression runs under Windows PowerShell 5's Copy-Item path limit.
$taskRoot=Join-Path $workspace 'Saved/BuildChecks/ConsumeSnap'
$id=[guid]::NewGuid().ToString('N');$temp=Join-Path $taskRoot ('Temp/'+$id)
$resultDir=Join-Path $taskRoot $id
$helper=Join-Path $workspace 'Scripts/Network/Fixtures/InventoryConsumeBackendSnapshot.ps1'
$runner=Join-Path $workspace 'Scripts/Network/Run-DungeonBossReturnE2ETest.ps1'
$controls=@($helper,$runner,(Join-Path $workspace 'Scripts/Network/Fixtures/InventoryRecovery.ps1'),$PSCommandPath);$before=@{};foreach($path in $controls){$before[$path]=(Get-FileHash -LiteralPath $path).Hash}
$groups=[Collections.Generic.List[string]]::new();$links=[Collections.Generic.List[string]]::new()
$proof=[ordered]@{passed=$false;scope='Small isolated source-copy/guard fixtures only; no actual backend source copy/build, UE/UBT/service/database execution';groups=@();cleanupBytes=0L;cleanupExceptions=@()}
function Assert-That([bool]$condition,[string]$reason){if(-not $condition){throw $reason}}
function Reject([scriptblock]$action,[string]$expected){$reason=$null;try{& $action | Out-Null}catch{$reason=$_.Exception.Message};if(-not $reason -or $reason -notmatch $expected){throw ('Expected guarded refusal: '+$expected)}}
function Write-Fixture([string]$path,[string]$body){New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null;[IO.File]::WriteAllText($path,$body,[Text.UTF8Encoding]::new($false))}
function New-Destination {Join-Path $project ('Saved/Tests/DungeonBossReturn/'+[guid]::NewGuid().ToString('N')+'/backend-build/inputs')}
New-Item -ItemType Directory -Path $temp,$resultDir | Out-Null
try {
    $rows=@(Get-Content (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match '^\|.*consume-death-integration-20261031' -and $_ -match 'InventoryConsumeBackendSnapshot'})
    Assert-That ($rows.Count -eq 1) 'Snapshot fixture reservation missing.'
    foreach($path in $controls){$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors);Assert-That (@($errors).Count -eq 0) 'PowerShell parser errors.'}
    . $helper
    $project=Join-Path $temp 'Project';$application=Join-Path $project 'Backend/ProjectRpg.Backend'
    $projectFile=Join-Path $application 'ProjectRpg.Backend.csproj'
    $projectXml='<Project Sdk="Microsoft.NET.Sdk.Web"><PropertyGroup><TargetFramework>net9.0</TargetFramework></PropertyGroup><ItemGroup><Content Include="..\Database\schema.sql" Link="Database\schema.sql" /></ItemGroup></Project>'
    Write-Fixture $projectFile $projectXml
    $program=Join-Path $application 'Program.cs';$originalProgram='// isolated fixture original'
    Write-Fixture $program $originalProgram
    Write-Fixture (Join-Path $application 'appsettings.json') '{"Fixture":true}'
    Write-Fixture (Join-Path $application 'DevelopmentProfiles/Training.json') '{"Profile":"Fixture"}'
    Write-Fixture (Join-Path $project 'Backend/Database/schema.sql') 'select 1;'
    Write-Fixture (Join-Path $project 'Backend/Database/Migrations/0002_fixture.sql') 'select 2;'
    Write-Fixture (Join-Path $project 'Directory.Build.props') '<Project><PropertyGroup><Nullable>enable</Nullable></PropertyGroup></Project>'
    $ignored=@((Join-Path $application 'bin/old.dll'),(Join-Path $application 'obj/project.assets.json'),(Join-Path $application 'artifacts/old.txt'))
    foreach($path in $ignored){Write-Fixture $path 'owned-original-output-sentinel'}
    $sentinels=@{};foreach($path in $ignored){$sentinels[$path]=(Get-FileHash -LiteralPath $path).Hash}
    $destination=New-Destination
    $snapshot=New-InventoryConsumeBackendSnapshot $project $destination
    Assert-That ($snapshot.SourceCohortVerified -and $snapshot.Files.Count -eq 7) 'Application/control/SQL cohort was not preserved.'
    Assert-That ($snapshot.ProjectPath -eq (Join-Path $destination 'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj')) 'Build target escaped its copied source.'
    foreach($entry in $snapshot.Files){Assert-That ((Get-FileHash -LiteralPath (Join-Path $project $entry.RelativePath)).Hash -ceq $entry.SHA256) 'Source or copied input changed.'}
    foreach($name in @('bin','obj','artifacts')){Assert-That (-not (Test-Path -LiteralPath (Join-Path $destination ('Backend/ProjectRpg.Backend/'+$name)))) 'Original build output was copied.'}
    $groups.Add('Self-contained application/SQL/profile/build-control bytes preserved; original bin/obj/artifacts excluded')
    $dotnet=(Get-Command dotnet -ErrorAction Stop).Source
    $output=Join-Path (Split-Path $destination) 'runtime'
    $arguments=@('msbuild',$snapshot.ProjectPath,'-nologo','-verbosity:quiet','-getItem:Compile',
        '-getProperty:OutputPath,MSBuildProjectExtensionsPath',('-p:OutputPath='+$output+'\'))
    $evaluated=& $dotnet @arguments
    if($LASTEXITCODE -ne 0){throw 'Private fixture MSBuild evaluation failed.'}
    $evaluation=($evaluated -join "`n") | ConvertFrom-Json
    $expectedProgram=Join-Path $destination 'Backend/ProjectRpg.Backend/Program.cs'
    Assert-That (@($evaluation.Items.Compile | Where-Object {$_.FullPath -eq $expectedProgram}).Count -eq 1) 'SDK excluded the copied source from Compile.'
    Assert-That ([IO.Path]::GetFullPath($evaluation.Properties.OutputPath).TrimEnd('\') -eq $output) 'SDK output is not the runtime sibling.'
    Assert-That ([IO.Path]::GetFullPath($evaluation.Properties.MSBuildProjectExtensionsPath).StartsWith($destination+'\',[StringComparison]::OrdinalIgnoreCase)) 'SDK obj escaped private inputs.'
    $proof.msbuildEvaluation=[ordered]@{copiedProgramIncluded=$true;privateObj=$true;runtimeSibling=$true;buildTargetsRun=$false}
    $groups.Add('SDK evaluation includes copied Program.cs with runtime sibling output and private obj; no build targets run')
    Write-Fixture $program '// later independent original edit'
    Assert-InventoryConsumeBackendSnapshot $project $snapshot
    $groups.Add('Closed private input validation does not freeze or reread later original edits')
    $copiedProgram=Join-Path $destination 'Backend/ProjectRpg.Backend/Program.cs'
    Write-Fixture $copiedProgram '// changed private input'
    Reject {Assert-InventoryConsumeBackendSnapshot $project $snapshot} 'input bytes changed'
    Write-Fixture $copiedProgram $originalProgram
    Write-Fixture (Join-Path $destination 'Backend/ProjectRpg.Backend/Unexpected.cs') '// unexpected private source'
    Reject {Assert-InventoryConsumeBackendSnapshot $project $snapshot} 'input set changed'
    $groups.Add('Changed copied bytes and extra source files both refused')
    Reject {New-InventoryConsumeBackendSnapshot $project $destination} 'already exists'
    Reject {New-InventoryConsumeBackendSnapshot $project (Join-Path $project 'Backend')} 'outside the allocated'
    $groups.Add('Existing/private destination reuse and broad source targets refused before writes')
    $outside=Join-Path $temp 'Outside';Write-Fixture (Join-Path $outside 'keep.txt') 'outside sentinel'
    $outsideHash=(Get-FileHash -LiteralPath (Join-Path $outside 'keep.txt')).Hash
    $sourceLink=Join-Path $application 'LinkedSource'
    New-Item -ItemType Junction -Path $sourceLink -Target $outside | Out-Null;$links.Add($sourceLink)
    Reject {New-InventoryConsumeBackendSnapshot $project (New-Destination)} 'linked source'
    [IO.Directory]::Delete($sourceLink,$false);$links.Remove($sourceLink) | Out-Null
    $ancestorLink=Join-Path $project ('Saved/Tests/DungeonBossReturn/'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Junction -Path $ancestorLink -Target $outside | Out-Null;$links.Add($ancestorLink)
    Reject {New-InventoryConsumeBackendSnapshot $project (Join-Path $ancestorLink 'backend-build/inputs')} 'reparse ancestor'
    [IO.Directory]::Delete($ancestorLink,$false);$links.Remove($ancestorLink) | Out-Null
    Assert-That ((Get-FileHash -LiteralPath (Join-Path $outside 'keep.txt')).Hash -ceq $outsideHash) 'Link target was modified.'
    $groups.Add('Source and destination-ancestor junctions refused; both targets preserved')
    foreach($element in @('<ProjectReference Include="..\Other\Other.csproj" />','<Import Project="..\Other\shared.props" />')){
        Write-Fixture $projectFile ('<Project><ItemGroup>'+$element+'</ItemGroup></Project>')
        Reject {New-InventoryConsumeBackendSnapshot $project (New-Destination)} 'self-contained application'
    }
    Write-Fixture $projectFile $projectXml
    $buildControl=Join-Path $project 'Directory.Build.props'
    $originalControl=[IO.File]::ReadAllText($buildControl)
    Write-Fixture $buildControl '<Project><Import Project="../Other/shared.props" /></Project>'
    Reject {New-InventoryConsumeBackendSnapshot $project (New-Destination)} 'self-contained application'
    Write-Fixture $buildControl $originalControl
    $groups.Add('Uncaptured project-reference and explicit-import graphs refused')
    & {
        $counter=[pscustomobject]@{Reads=0}
        function Get-FileHash {
            param([string]$LiteralPath)
            $value=Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath
            if($LiteralPath -eq $program){$counter.Reads++;if($counter.Reads -eq 1){[IO.File]::AppendAllText($program,'// edit during copy')}}
            $value
        }
        Reject {New-InventoryConsumeBackendSnapshot $project (New-Destination)} 'changed while copying|cohort bytes changed'
    }
    $groups.Add('Concurrent source-byte change refused rather than producing a trusted mixed snapshot')
    $text=[IO.File]::ReadAllText($runner);$guards=[scriptblock]::Create($text.Substring(0,$text.IndexOf('$projectRoot = [IO.Path]::GetFullPath')))
    $valid=@{InventoryRecovery=$true;InventoryRecoveryOperation='Consume';ClientCount=1;InventoryRecoveryConsumeDeath=$true}
    & $guards @valid
    $invalid=$valid.Clone();$invalid.NoBackendRestore=$true
    Reject {& $guards @invalid} 'requires package restore'
    $groups.Add('Fresh-private-input mode permits normal restore and refuses NoBackendRestore before run allocation')
    foreach($path in $sentinels.Keys){Assert-That ((Get-FileHash -LiteralPath $path).Hash -ceq $sentinels[$path]) 'Original build output sentinel changed.'}
    foreach($path in $controls){Assert-That ((Get-FileHash -LiteralPath $path).Hash -ceq $before[$path]) 'Runner/helper/test input changed during verification.'}
    $core=Join-Path $project 'Backend/ProjectRpg.Operations.Core'
    Write-Fixture (Join-Path $core 'ProjectRpg.Operations.Core.csproj') '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net9.0</TargetFramework></PropertyGroup></Project>'
    Write-Fixture (Join-Path $core 'Core.cs') 'class OperationsCoreFixture {}'
    Write-Fixture (Join-Path $application 'Pages/Ops/Index.cshtml') '@page'
    Write-Fixture $projectFile ($projectXml.Replace('</ItemGroup>','<ProjectReference Include="../ProjectRpg.Operations.Core/ProjectRpg.Operations.Core.csproj" /></ItemGroup>'))
    $opsSnapshot=New-InventoryConsumeBackendSnapshot $project (New-Destination)
    Assert-That (Test-Path -LiteralPath (Join-Path $opsSnapshot.SnapshotRoot 'Backend/ProjectRpg.Operations.Core/Core.cs')) 'Operations Core was not frozen.'
    Assert-That (Test-Path -LiteralPath (Join-Path $opsSnapshot.SnapshotRoot 'Backend/ProjectRpg.Backend/Pages/Ops/Index.cshtml')) 'Razor source was not frozen.'
    $groups.Add('Explicit Operations Core reference and Razor source captured with the host')
    $proof.passed=$true
} catch {$proof.failure=$_.Exception.Message;$proof.failureLocation=$_.ScriptStackTrace}
finally {
    try {
        $expected=[IO.Path]::GetFullPath((Join-Path $taskRoot ('Temp/'+$id)))
        if([IO.Path]::GetFullPath($temp) -cne $expected -or $id -cnotmatch '^[a-f0-9]{32}$'){throw 'Fixture cleanup boundary mismatch.'}
        foreach($link in $links){
            if(-not $link.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase) -or -not ((Get-Item -LiteralPath $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Owned link identity mismatch.'}
            [IO.Directory]::Delete($link,$false)
        }
        $stack=[Collections.Generic.Stack[string]]::new();$stack.Push($temp)
        $files=[Collections.Generic.List[string]]::new();$directories=[Collections.Generic.List[string]]::new()
        while($stack.Count){$path=$stack.Pop();$item=Get-Item -LiteralPath $path -Force;if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or -not ($path -eq $temp -or $path.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase))){throw 'Linked or outside fixture cleanup refused.'};if($item.PSIsContainer){$directories.Add($path);foreach($child in Get-ChildItem -LiteralPath $path -Force){$stack.Push($child.FullName)}}else{$files.Add($path)}}
        foreach($file in $files){$handle=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$handle.Dispose()}
        foreach($file in $files){$bytes=(Get-Item -LiteralPath $file).Length;Remove-Item -LiteralPath $file -Force;$proof.cleanupBytes+=$bytes}
        foreach($directory in $directories | Sort-Object Length -Descending){[IO.Directory]::Delete($directory,$false)}
        $proof.tempRemoved=-not (Test-Path -LiteralPath $temp)
    } catch {$proof.cleanupExceptions=@($_.Exception.Message);$proof.passed=$false}
    $proof.groups=$groups.ToArray();$proof.inputHashes=$before;$proof.completedUtc=[DateTime]::UtcNow.ToString('o')
    $proof | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resultDir 'Result.json') -Encoding UTF8
}
Write-Output ('RESULT '+$resultDir+' Passed='+$proof.passed+' Groups='+$groups.Count+' CleanupBytes='+$proof.cleanupBytes)
if(-not $proof.passed){exit 1}
