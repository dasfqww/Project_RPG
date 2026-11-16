$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N');$tempRoot=Join-Path $workspace 'Saved/LS';$temp=Join-Path $tempRoot $run.Substring(0,8)
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/LootSources-20261116/Http-'+$run)
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
$proof=[ordered]@{Passed=$false;Head=(git -C $workspace rev-parse HEAD).Trim();Inputs=@();Temp=$temp;Failure=$null}
function Save-Proof {
 [IO.Directory]::CreateDirectory($proofRoot)|Out-Null
 [IO.File]::WriteAllText((Join-Path $proofRoot 'Result.json'),($proof|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
}
try {
 if(!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -SimpleMatch '/ loot-sources-20261116 |' -Quiet -Encoding UTF8)){throw 'Reservation missing'}
 [IO.Directory]::CreateDirectory($temp)|Out-Null
 $archive=Join-Path $temp 'Backend.tar'; & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- Backend
 if($LASTEXITCODE -ne 0){throw 'Archive failed'}
 & tar -xf $archive -C $temp
 if($LASTEXITCODE -ne 0){throw 'Extraction failed'}
 foreach($name in @('Program.cs','ProjectRpg.Backend.LootCurrencyTests.csproj')) {
  $relative='Backend/ProjectRpg.Backend.LootCurrencyTests/'+$name;$source=Join-Path $workspace $relative;$target=Join-Path $temp $relative
  [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;$sha=(Get-FileHash -LiteralPath $source).Hash;Copy-Item -LiteralPath $source -Destination $target
  if((Get-FileHash -LiteralPath $target).Hash -ne $sha){throw 'Input changed'};$proof.Inputs+=@{Path=$relative;SHA256=$sha}
 }
 $csproj=Join-Path $temp 'Backend/ProjectRpg.Backend.LootCurrencyTests/ProjectRpg.Backend.LootCurrencyTests.csproj'
 Save-Proof
 & dotnet build $csproj -c Release --nologo *> (Join-Path $proofRoot 'Build.log')
 $proof.BuildExit=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'HTTP fixture build failed'}
 & dotnet (Join-Path $temp 'Backend/ProjectRpg.Backend.LootCurrencyTests/bin/Release/net9.0/ProjectRpg.Backend.LootCurrencyTests.dll') *> (Join-Path $proofRoot 'Http.log')
 $proof.HttpExit=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'HTTP regression failed'}
 foreach($input in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $input.Path)).Hash -ne $input.SHA256){throw 'Original input changed'}}
 $proof.Passed=$true
}catch{$proof.Failure=$_.Exception.Message}
finally{
 if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
 $proof.CleanupComplete=!(Test-Path -LiteralPath $temp);$proof.Passed=$proof.Passed -and $proof.CleanupComplete;Save-Proof
}
if(!$proof.Passed){throw $proof.Failure}
