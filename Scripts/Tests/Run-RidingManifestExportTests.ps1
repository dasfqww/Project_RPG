[CmdletBinding()]
param([string]$Python = 'D:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe',
    [ValidateSet('riding-manifest-export-20261116','riding-export-binding-20261116')][string]$Reservation='riding-manifest-export-20261116')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N');$task=$Reservation
$suite=if($task-eq'riding-export-binding-20261116'){'RidingExportBinding-20261116'}else{'RidingManifestExport-20261116'}
$root=Join-Path $workspace ('Saved/BuildChecks/'+$suite+'/'+$id)
$tempRoot=Join-Path $workspace 'Saved/ME';$temp=Join-Path $tempRoot $id.Substring(0,8);$repo=Join-Path $temp 'Repo'
$ownsTemp=$false
$proof=[ordered]@{Passed=$false;Base='749401a135d8217948ca29324816cc97abb1f7f5';Inputs=@();RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;Commands=@();Tests=@();Modules=@{};Cleanup=$null;Failure=$null}
$files=@('Scripts/Editor/RidingCatalogManifest.py','Scripts/Editor/ExportRidingCatalog.py','Scripts/Tests/TestRidingCatalogManifest.py',
    'Backend/ProjectRpg.Backend.RidingHttpTests/Program.cs','Backend/ProjectRpg.Backend.RidingHttpTests/ManifestExportCases.cs')
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof {if(Test-Path -LiteralPath $root){[IO.File]::WriteAllText((Join-Path $root 'Result.json'),($proof|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))}}
function Assert-Reservation {
    if(@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_-match('^\|[^|]* / '+[regex]::Escape($task)+' \|')}).Count-ne1){throw 'Own reservation missing'}
}
function Run-Private([string]$Executable,[string]$Arguments,[string]$Label,[switch]$JsonResult) {
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Executable;$start.Arguments=$Arguments
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WorkingDirectory=$repo;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$start.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    $start.EnvironmentVariables['DOTNET_CLI_HOME']=Join-Path $temp 'Cli'
    $start.EnvironmentVariables['NUGET_PACKAGES']=Join-Path $temp 'Nuget'
    $start.EnvironmentVariables['NUGET_HTTP_CACHE_PATH']=Join-Path $temp 'NugetHttp'
    $start.EnvironmentVariables['PYTHONDONTWRITEBYTECODE']='1'
    $start.EnvironmentVariables['DOTNET_CLI_UI_LANGUAGE']='en'
    $start.EnvironmentVariables['DOTNET_CLI_TELEMETRY_OPTOUT']='1'
    $child=[Diagnostics.Process]::new();$child.StartInfo=$start;$started=$false
    try {
        $started=$child.Start();if(!$started){throw 'Private process start failed'}
        $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$deadline=[DateTime]::UtcNow.AddMinutes(4)
        while(!$child.WaitForExit(250)){if([DateTime]::UtcNow-ge$deadline){throw ('Private process timeout: '+$Label)}}
        $child.WaitForExit();$out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $root ($Label+'.log')),$out+$err,[Text.UTF8Encoding]::new($false))
        $proof.Commands+=@{Label=$Label;Executable=$Executable;Arguments=$Arguments;Exit=$child.ExitCode}
        if($child.ExitCode-ne0){throw ('Private process failed: '+$Label)}
        if($JsonResult){$result=$out|ConvertFrom-Json;if(!$result.Passed){throw ('Failed cases: '+$Label)};$proof.Tests+=@{Label=$Label;Result=$result}}
    } finally {if($started-and!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
try {
    Assert-Reservation
    if(!(Test-Path -LiteralPath $Python -PathType Leaf)){throw 'Pass an existing Python 3 interpreter with -Python'}
    $ancestor=$temp
    while($ancestor){if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
    if(Test-Path -LiteralPath $temp){throw 'Own temporary directory already exists'}
    $null=[IO.Directory]::CreateDirectory($repo);$ownsTemp=$true;$null=[IO.Directory]::CreateDirectory($root)
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $proof.BackendTree=(git -C $workspace rev-parse ($proof.Base+':Backend')).Trim()
    & git -C $workspace archive --format=tar ('--output='+(Join-Path $temp 'Backend.tar')) $proof.Base -- Backend
    if($LASTEXITCODE){throw 'Backend archive failed'}
    & tar -xf (Join-Path $temp 'Backend.tar') -C $repo
    if($LASTEXITCODE){throw 'Backend extraction failed'}
    foreach($relative in $files) {
        $source=Join-Path $workspace $relative;$target=Join-Path $repo $relative;$hash=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $target));Copy-Item -LiteralPath $source -Destination $target
        if((Get-FileHash -LiteralPath $source).Hash-ne$hash-or(Get-FileHash -LiteralPath $target).Hash-ne$hash){throw 'Input changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$hash}
    }
    Save-Proof
    $tests=Join-Path $repo 'Scripts/Tests/TestRidingCatalogManifest.py';$fixtures=Join-Path $temp 'Fixtures'
    Run-Private $Python ('-B "'+$tests+'" --fixtures "'+$fixtures+'"') 'Python' -JsonResult
    $project=Join-Path $repo 'Backend/ProjectRpg.Backend.RidingHttpTests/ProjectRpg.Backend.RidingHttpTests.csproj'
    $dotnet=(Get-Command dotnet).Source
    Run-Private $dotnet ('build "'+$project+'" -c Release --nologo --disable-build-servers -p:UseSharedCompilation=false') 'Build'
    $dll=Join-Path (Split-Path -Parent $project) 'bin/Release/net9.0/ProjectRpg.Backend.RidingHttpTests.dll'
    $proof.Modules.HttpTests=(Get-FileHash -LiteralPath $dll).Hash
    $proof.Modules.Backend=(Get-FileHash -LiteralPath (Join-Path (Split-Path -Parent $dll) 'ProjectRpg.Backend.dll')).Hash
    Run-Private $dotnet ('"'+$dll+'" --manifest-exports "'+(Join-Path $fixtures 'Contract')+'"') 'ManifestMemory' -JsonResult
    Run-Private $dotnet ('"'+$dll+'"') 'ExistingRegistrationMemory' -JsonResult
    Run-Private $dotnet ('"'+$dll+'" --preferences') 'ExistingPreferencesMemory' -JsonResult
    $expected=@{Python=20;ManifestMemory=5;ExistingRegistrationMemory=7;ExistingPreferencesMemory=8}
    if($proof.Tests.Count-ne$expected.Count){throw 'Missing expected suites'}
    foreach($test in $proof.Tests){if($test.Result.CaseCount-ne$expected[$test.Label]){throw ('Missing cases: '+$test.Label)}}
    foreach($entry in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash-ne$entry.SHA256){throw 'Input changed during validation'}}
    if((Get-FileHash -LiteralPath $PSCommandPath).Hash-ne$proof.RunnerSHA256){throw 'Runner changed during validation'}
    $proof.Passed=$true
} catch {$proof.Failure=$_.Exception.Message}
finally {
    try{Assert-Reservation;if($ownsTemp-and(Test-Path -LiteralPath $temp)){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}}catch{$proof.Failure=([string]$proof.Failure+'; cleanup: '+$_.Exception.Message)}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp);$proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete
    $proof.HistoricalHashesOnly=$true;Save-Proof;Write-Output ('Passed='+$proof.Passed+'; proof='+$root)
}
if(!$proof.Passed){throw $proof.Failure}
