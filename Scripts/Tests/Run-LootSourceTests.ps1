param([string]$EngineRoot='D:/UE_5.8',[switch]$CompileOnly,[ValidateRange(1,4)][int]$MaxParallelActions=2,[string]$ResumeResult,[switch]$InstalledSnapshot,[string]$InstalledBinaryRoot,[string]$TestFilter='ProjectRPG.Item.LootSources+ProjectRPG.Item.NPCDrop+ProjectRPG.Item.GreatSword',[ValidateRange(1,4)][int]$ExpectedLootTests=4)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$InstalledBinaryRoot){$InstalledBinaryRoot=$workspace}
if(!$InstalledSnapshot -and $InstalledBinaryRoot -ne $workspace){throw 'InstalledBinaryRoot requires InstalledSnapshot'}
$run=[Guid]::NewGuid().ToString('N')
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/LootSources-20261116/'+$run)
$tempRoot=Join-Path $workspace 'Saved/LS'
$temp=Join-Path $tempRoot $run.Substring(0,8)
$project=Join-Path $temp 'Project'
$dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
$ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
$proof=[ordered]@{Passed=$false;Head=(git -C $workspace rev-parse HEAD).Trim();Inputs=@();Temp=$temp;Failure=$null;CompileOnly=[bool]$CompileOnly}
$process=$null
$resume=$null
if($CompileOnly -and $InstalledSnapshot){throw 'CompileOnly and InstalledSnapshot are separate verification modes'}
if($ResumeResult) {
 $resume=Get-Content -LiteralPath $ResumeResult -Raw -Encoding UTF8|ConvertFrom-Json
 $temp=Assert-GuardianArtifactPath $resume.Temp $tempRoot
 if($resume.Passed -or !(Test-Path -LiteralPath (Join-Path $temp 'Project/Project_RPG.uproject'))){throw 'Resume requires an unfinished own snapshot'}
 $project=Join-Path $temp 'Project';$proof.Temp=$temp;$proof.Head=$resume.Head;$proof.ResumeResult=$ResumeResult
}
function Save-Proof {
 [IO.Directory]::CreateDirectory($proofRoot)|Out-Null
 [IO.File]::WriteAllText((Join-Path $proofRoot 'Result.json'),($proof|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
}
function Copy-FrozenFile([string]$Source,[string]$Target) {
 $sha=(Get-FileHash -LiteralPath $Source).Hash
 [IO.Directory]::CreateDirectory((Split-Path -Parent $Target))|Out-Null;Copy-Item -LiteralPath $Source -Destination $Target -Force
 if((Get-FileHash -LiteralPath $Target).Hash -ne $sha -or (Get-FileHash -LiteralPath $Source).Hash -ne $sha){throw 'Binary changed while copying'}
}
function Copy-RuntimeBinaries([string]$Source,[string]$Target) {
 if(!(Test-Path -LiteralPath $Source)){return}
 foreach($item in Get-ChildItem -LiteralPath $Source -Force) {
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Binary input link rejected'}
  if($item.PSIsContainer){Copy-RuntimeBinaries $item.FullName (Join-Path $Target $item.Name)}
  elseif($item.Extension -notin @('.pdb','.obj','.pch','.lib','.exp','.exe')){Copy-FrozenFile $item.FullName (Join-Path $Target $item.Name)}
 }
}
try {
 if(!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -SimpleMatch '/ loot-sources-20261116 |' -Quiet -Encoding UTF8)){throw 'Reservation missing'}
 [IO.Directory]::CreateDirectory($project)|Out-Null
 $paths=@('Source','Config','Project_RPG.uproject')
 $pluginFiles=@(git -C $workspace ls-tree -r --name-only $proof.Head -- Plugins)
 foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})) {
  $paths+=$descriptor;$dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
  foreach($sub in @('Source','Config','Resources','Shaders')) {if(@($pluginFiles|Where-Object {$_.StartsWith($dir+'/'+$sub+'/')}).Count){$paths+=($dir+'/'+$sub)}}
 }
 $archive=Join-Path $temp 'Inputs.tar'
 if(!$resume) {
  & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths
  if($LASTEXITCODE -ne 0){throw 'Archive failed'}
  & tar -xf $archive -C $project
  if($LASTEXITCODE -ne 0){throw 'Extraction failed'}
 }
 $files=@('Source/Project_RPG/Private/Ability/NPC/RPGNPCDropItemAbility.cpp','Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp',
 'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp','Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp','Source/Project_RPG/Public/Item/PickUp/RPGPickUpBase.h',
 'Source/Project_RPG/Private/Item/PickUp/RPGPickUpBase.cpp','Source/Project_RPG/Public/Item/PickUp/RPGCurrencyPickup.h',
 'Source/Project_RPG/Private/Item/PickUp/RPGCurrencyPickup.cpp','Source/Project_RPG/Private/Tests/RPGLootSourceTests.h',
 'Source/Project_RPG/Private/Tests/RPGLootSourceTests.cpp')
 foreach($relative in @('Public/Item/Loot','Private/Item/Loot')) {
  foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $workspace ('Source/Project_RPG/'+$relative)) -File)) {
   $files+=('Source/Project_RPG/'+$relative+'/'+$file.Name)
  }
 }
 foreach($relative in $files) {
  $source=Join-Path $workspace $relative;$target=Join-Path $project $relative;$sha=(Get-FileHash -LiteralPath $source).Hash
  [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;Copy-Item -LiteralPath $source -Destination $target
  if((Get-FileHash -LiteralPath $target).Hash -ne $sha){throw ('Input changed: '+$relative)}
  $proof.Inputs+=@{Path=$relative;SHA256=$sha}
 }
 $arguments=@($ubt,'Project_RPGEditor','Win64','Development',(Join-Path $project 'Project_RPG.uproject'),'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote','-NoHotReloadFromIDE',('-MaxParallelActions='+$MaxParallelActions),('-log='+(Join-Path $proofRoot 'Build.log')))
 if($CompileOnly) {
  $fileList=Join-Path $temp 'Compile.txt';[IO.File]::WriteAllLines($fileList,@($files|Where-Object {$_ -like '*.cpp'}|ForEach-Object {Join-Path $project $_}),[Text.UTF8Encoding]::new($false))
  $arguments+=('-FileList='+$fileList)
 }
 if(!$InstalledSnapshot) {
  $proof.BuildCommand=@($dotnet)+$arguments;Save-Proof
  & $dotnet @arguments *> (Join-Path $proofRoot 'BuildConsole.log')
  $proof.BuildExit=$LASTEXITCODE;Save-Proof
  if($LASTEXITCODE -ne 0){throw 'Build failed'}
 } else {
  $proof.Scope='Frozen installed Editor module; all own inputs verified against compiler PDB SHA256 and DLL/PDB GUID; no own full build claim'
  Copy-RuntimeBinaries (Join-Path $InstalledBinaryRoot 'Binaries') (Join-Path $project 'Binaries')
  foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})) {
   $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'));Copy-RuntimeBinaries (Join-Path $workspace ($dir+'/Binaries')) (Join-Path $project ($dir+'/Binaries'))
  }
  $dll=Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
  $pdb=Join-Path $temp 'UnrealEditor-Project_RPG.pdb'
  Copy-FrozenFile (Join-Path $InstalledBinaryRoot 'Binaries/Win64/UnrealEditor-Project_RPG.pdb') $pdb
  $llvm=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin'
  $dump=@(& (Join-Path $llvm 'llvm-pdbutil.exe') dump -files -summary $pdb)
  if($LASTEXITCODE -ne 0){throw 'PDB source dump failed'}
  $debug=@(& (Join-Path $llvm 'llvm-readobj.exe') --coff-debug-directory $dll)
  if($LASTEXITCODE -ne 0){throw 'DLL debug directory failed'}
  $pdbGuid=@($dump|Where-Object {$_ -match '^\s*GUID: (\{[^}]+\})$'}|ForEach-Object {$Matches[1]})
  $dllGuid=@($debug|Where-Object {$_ -match '^\s*PDBGUID: (\{[^}]+\})$'}|ForEach-Object {$Matches[1]})
  if($pdbGuid.Count -ne 1 -or $dllGuid.Count -ne 1 -or $pdbGuid[0] -ne $dllGuid[0]){throw 'DLL/PDB GUID mismatch'}
  $map=@{}
  foreach($line in $dump){if($line -match '^\s*- \(SHA-256: ([A-F0-9]{64})\) (.+)$'){$key=$Matches[2].ToLowerInvariant();if(!$map.ContainsKey($key)){$map[$key]=@()};$map[$key]+=$Matches[1]}}
  $proof.PdbSourceChecks=@(foreach($input in $proof.Inputs){$key=[IO.Path]::GetFullPath((Join-Path $workspace $input.Path)).ToLowerInvariant();$hashes=@($map[$key]|Select-Object -Unique);@{Path=$input.Path;SHA256=$input.SHA256;PdbChecksums=$hashes;Match=($hashes.Count -eq 1 -and $hashes[0] -eq $input.SHA256)}})
  if(@($proof.PdbSourceChecks|Where-Object {!$_.Match}).Count){throw 'Installed module does not contain exact own source inputs'}
  $proof.ModuleSHA256=(Get-FileHash -LiteralPath $dll).Hash;$proof.PdbSHA256=(Get-FileHash -LiteralPath $pdb).Hash;$proof.PdbGuid=$pdbGuid[0]
  Save-Proof
 }
 if(!$CompileOnly) {
  if(!(Test-Path -LiteralPath (Join-Path $project 'Content'))){$null=New-Item -ItemType Junction -Path (Join-Path $project 'Content') -Target (Join-Path $workspace 'Content')}
  foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})) {
   $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'));$content=Join-Path $workspace ($dir+'/Content')
   if((Test-Path -LiteralPath $content) -and !(Test-Path -LiteralPath (Join-Path $project ($dir+'/Content')))){$null=New-Item -ItemType Junction -Path (Join-Path $project ($dir+'/Content')) -Target $content}
  }
  $runArgs=@(('"'+(Join-Path $project 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',
   ('-ExecCmds="Automation RunTests '+$TestFilter+'"'),
   '-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding',
   '-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',
   ('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),
   ('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),
   ('-ReportExportPath="'+(Join-Path $temp 'Report')+'"'))
  $proof.NativeCommand=@($editor)+$runArgs;Save-Proof
  $previousSkip=$env:UE_SKIP_UBT_SDK_SETUP
  try {$env:UE_SKIP_UBT_SDK_SETUP='1';$process=Start-Process -FilePath $editor -ArgumentList $runArgs -WindowStyle Hidden -PassThru}
  finally {if($null -eq $previousSkip){Remove-Item Env:UE_SKIP_UBT_SDK_SETUP -ErrorAction SilentlyContinue}else{$env:UE_SKIP_UBT_SDK_SETUP=$previousSkip}}
  $deadline=[DateTime]::UtcNow.AddMinutes(10)
  while(!$process.HasExited){
   if(!$proof.Contains('LoadedModule')){try{$loaded=@((Get-Process -Id $process.Id -ErrorAction Stop).Modules|Where-Object ModuleName -eq 'UnrealEditor-Project_RPG.dll');if($loaded.Count -eq 1){$proof.LoadedModule=$loaded[0].FileName}}catch{}}
   if([DateTime]::UtcNow -gt $deadline){throw 'Native timeout'};$null=$process.WaitForExit(200)
  }
  $process.WaitForExit();$proof.NativeExit=$process.ExitCode
  if($process.ExitCode -ne 0){throw 'Native execution failed'}
  if(!$proof.Contains('LoadedModule') -or [IO.Path]::GetFullPath($proof.LoadedModule) -ine (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')){throw 'Native process loaded an unexpected module'}
  $report=Get-Content -LiteralPath (Join-Path $temp 'Report/index.json') -Raw -Encoding UTF8|ConvertFrom-Json
  $proof.Tests=@($report.tests|Select-Object fullTestPath,state,errors,warnings)
  if(@($proof.Tests|Where-Object {$_.fullTestPath -like 'ProjectRPG.Item.LootSources.*'}).Count -ne $ExpectedLootTests -or
   @($proof.Tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0}).Count){throw 'Native tests failed or missing'}
 }
 foreach($input in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $input.Path)).Hash -ne $input.SHA256){throw ('Source changed: '+$input.Path)}}
 $proof.Passed=$true
}catch{$proof.Failure=$_.Exception.Message}
finally {
 if($process -and !$process.HasExited){$process.Kill();$null=$process.WaitForExit(10000)}
 if(!$process -or $process.HasExited){if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}}
 $proof.CleanupComplete=!(Test-Path -LiteralPath $temp);$proof.Passed=$proof.Passed -and $proof.CleanupComplete;Save-Proof
}
if(!$proof.Passed){throw $proof.Failure}
