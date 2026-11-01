[CmdletBinding()]
param([ValidateRange(1024,65535)][int]$Port=19112,[ValidateRange(60,600)][int]$TimeoutSeconds=300)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[guid]::NewGuid().ToString('N');$root=Join-Path $workspace ('Saved/Tests/CombatDebuffNetwork-20261101/'+$id)
$temp=Join-Path $workspace ('Saved/CDB/'+$id.Substring(0,12));$inputsRoot=Join-Path $temp 'Inputs';$runtime=Join-Path $temp 'R'
$objects=Join-Path $workspace 'Intermediate/Build/Win64/x64/UnrealEditor/Development/Project_RPG'
$module=Join-Path $workspace 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
. (Join-Path $PSScriptRoot 'GuardianMelee2Artifacts.ps1')
$processes=@{};$loaded=@{};$failure=$null;$cleanup=$null;$closed=$false;$records=[Collections.Generic.List[object]]::new()
$proof=@{passed=$false;gameSourceWritten=$false;originalBinariesWritten=$false;sharedBuildRun=$false;port=$Port}
New-Item -ItemType Directory -Path $root -Force|Out-Null
if(Test-Path -LiteralPath $temp){throw 'Owned temporary directory already exists'}
New-Item -ItemType Directory -Path $inputsRoot -Force|Out-Null
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path).Hash}
function Json([string]$Name,$Value){$Value|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $root $Name) -Encoding UTF8}
function TextFile([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Copy-Fixed([string]$Source,[string]$Destination){
 $before=Hash $Source;New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force|Out-Null
 $stream=[IO.File]::Open($Source,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$output=[IO.File]::Create($Destination);try{$stream.CopyTo($output)}finally{$output.Dispose()}}finally{$stream.Dispose()}
 if((Hash $Source) -ne $before -or (Hash $Destination) -ne $before){throw 'Mixed input copy'}
 $records.Add(@{path=$Source;copy=$Destination;sha256=$before})
}
function Copy-Tree([string]$Source,[string]$Destination){
 if(!(Test-Path -LiteralPath $Source)){return}
 foreach($file in Get-ChildItem -LiteralPath $Source -File -Recurse){Copy-Fixed $file.FullName (Join-Path $Destination $file.FullName.Substring($Source.Length+1))}
}
function Replace-Root([string]$Text){[regex]::Replace($Text,[regex]::Escape($workspace.Replace('\','/')),[Text.RegularExpressions.MatchEvaluator]{param($m)$inputsRoot.Replace('\','/')},[Text.RegularExpressions.RegexOptions]::IgnoreCase)}
function Snapshot-Content([string]$Source,[string]$Destination){
 New-Item -ItemType Directory -Path $Destination -Force|Out-Null
 $mutable=@((Join-Path $workspace 'Content/Blueprints/UI/PlayerHUD'),(Join-Path $workspace 'Content/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD.uasset'))
 foreach($item in Get-ChildItem -LiteralPath $Source -Force){
  $target=Join-Path $Destination $item.Name
  if($item.PSIsContainer){
   if(@($mutable|Where-Object {$_.StartsWith($item.FullName,[StringComparison]::OrdinalIgnoreCase) -or $item.FullName.StartsWith($_,[StringComparison]::OrdinalIgnoreCase)}).Count){Snapshot-Content $item.FullName $target}
   else{New-Item -ItemType Junction -Path $target -Target $item.FullName|Out-Null}
  }else{Copy-Fixed $item.FullName $target}
 }
}
function Signal([string]$Name){TextFile (Join-Path $root $Name) 'owned network fixture'}
function Number($Object,[string]$Name,[double]$Expected){
 $v=$Object.$Name
 if(($v -isnot [int] -and $v -isnot [long] -and $v -isnot [double] -and $v -isnot [decimal]) -or [double]::IsNaN([double]$v) -or [double]::IsInfinity([double]$v) -or [Math]::Abs([double]$v-$Expected) -gt .001){throw ('Invalid numeric field '+$Name)}
}
function Boolean($Object,[string]$Name,[bool]$Expected){if($Object.$Name -isnot [bool] -or $Object.$Name -ne $Expected){throw ('Invalid boolean field '+$Name)}}
function Validate-Result($Result,[string]$Role){
 Boolean $Result 'success' $true;Boolean $Result 'iris' $true;Boolean $Result 'nativeApprovedStimulus' $true
 Boolean $Result 'fixtureGrant' $true;Boolean $Result 'physicalInput' $false
 Number $Result 'stages' 49150;Number $Result 'lagMin' 80;Number $Result 'lagMax' 160;Number $Result 'loss' 5
 if($Result.role -cne $Role -or $Result.observations -isnot [array] -or $Result.observations.Count -ne 14){throw ('Incomplete '+$Role+' result')}
 $playerPath='/Game/Blueprints/Character/Player/BP_Player.BP_Player_C'
 $bossPath='/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan.BP_RaidSwordMan_C'
 if($Result.playerClass -cne $playerPath -or $Result.bossClass -cne $bossPath){throw 'Unexpected original pawn classes'}
 # stage, player HP/defense/multiplier, boss HP/defense/multiplier, boss owned effects
 $expected=@(@(1,9995,1,1.5,10000,2,1,0),@(2,9995,2,1,10000,2,1,0),@(3,9990,2,1,10000,2,1,0),
  @(4,9990,2,1,9995,1,1.5,2),@(5,9990,2,1,9980,.8,1.95,4),@(6,9990,2,1,9956,.8,1.95,4),
  @(7,9990,2,1,9956,1,1.5,2),@(8,9990,2,1,9956,2,1,0),@(9,9985,2,1,9956,2,1,0),
  @(10,9985,2,1,9951,2,1,0),@(11,9985,2,1,9946,1,1.5,2),@(12,9985,2,1,9946,2,1,0),
  @(13,9985,2,1,9941,1,1.5,2),@(15,9985,2,1,9941,2,1,0))
 for($i=0;$i -lt $expected.Count;$i++){
  $e=$expected[$i];$o=$Result.observations[$i];Number $o 'stage' $e[0]
  Number $o 'encounterState' $(if($e[0] -eq 15){3}else{1})
  if($Role -ne 'server'){Boolean $o 'success' $true;if($o.role -cne $Role){throw 'Acknowledgement role mismatch'}}
  foreach($kind in @('player','boss')){
   $a=$o.$kind;$isPlayer=$kind -eq 'player';$offset=if($isPlayer){1}else{4}
   if(!$a -or $a.class -cne $(if($isPlayer){$playerPath}else{$bossPath})){throw 'Missing/wrong observed actor'}
   Number $a 'hp' $e[$offset];Number $a 'defense' $e[($offset+1)]
   Number $a 'multiplier' $(if($Role -eq 'server'){$e[($offset+2)]}else{1})
   Boolean $a 'defenseTag' ($e[($offset+1)] -lt 2);Boolean $a 'takenTag' ($e[($offset+1)] -lt 2)
   Boolean $a 'authority' ($Role -eq 'server');Boolean $a 'actionBlocked' $false
   if($isPlayer){Boolean $a 'local' ($Role -eq 'owner')}
   else{Boolean $a 'hitImmune' $true;Boolean $a 'stunImmune' $true;Number $a 'replicationMode' 0;Number $a 'statusEffects' $(if($Role -eq 'server'){$e[7]}else{0})}
  }
 }
 if($Result.hits -isnot [array] -or $Result.hits.Count -ne $(if($Role -eq 'server'){9}else{0})){throw 'Damage stimulus count mismatch'}
 if($Role -eq 'server'){
  $damages=@(5,5,5,15,24,5,5,5,5)
  for($i=0;$i -lt $damages.Count;$i++){Number $Result.hits[$i] 'expectedDamage' $damages[$i];Number $Result.hits[$i] 'after' ([double]$Result.hits[$i].before-$damages[$i])}
 }
}
function Healthy {
 foreach($role in $processes.Keys){
  $p=$processes[$role];if($p.HasExited){throw "$role exited before accepted completion"}
  if(!$loaded.ContainsKey($role)){
   $mods=@();try{$mods=@((Get-Process -Id $p.Id).Modules|Where-Object {$_.ModuleName -eq 'UnrealEditor-Project_RPG.dll'})}catch{}
   if($mods.Count -eq 1){$actual=$mods[0].FileName;if($actual -ne $runtimeModule -or (Hash $actual) -ne $proof.moduleSHA256){throw "Unexpected $role actual module"};$loaded[$role]=@{pid=$p.Id;path=$actual;sha256=$proof.moduleSHA256};Json 'LoadedModules.json' $loaded}
  }
  $log=Join-Path $root ($role+'.log')
  if((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Quiet -Pattern 'COMBAT_DEBUFF_NETWORK_FAIL|COMBAT_DEBUFF_NETWORK_IO_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*): Error|Blueprint Runtime Error|LogScript: Error')){throw "Network fixture/log failure in $role"}
 }
}
function Wait-File([string]$Name){
 $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
 while([DateTime]::UtcNow -lt $deadline){Healthy;if(Test-Path -LiteralPath (Join-Path $root $Name)){return};Start-Sleep -Milliseconds 200}
 throw ('Missing '+$Name)
}
function Start-Role([string]$Role){
 $mode=if($Role -eq 'server'){'-server -port='+$Port}else{'-game'}
 $args=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry?game=/Script/Engine.GameModeBase',$mode,
  '-RPGCombatDebuffNetwork',('-RPGCombatDebuffRole='+$Role),('-RPGCombatDebuffDirectory="'+$root+'"'),('-RPGCombatDebuffURL=127.0.0.1:'+$Port),
  '-RPGNetTestMode','-RPGRequireIris','-UseIrisReplication=1','-PktLagMin=80','-PktLagMax=160','-PktLoss=5','-ExecCmds="t.MaxFPS 30"',
  '-unattended','-nop4','-nosplash','-nosound','-nosteam','-DisablePython','-NoLiveCoding','-nullrhi','-DDC=(EnginePak,Local)',
  ('-UserDir="'+(Join-Path $temp ($Role+'-U'))+'"'),('-abslog="'+(Join-Path $root ($Role+'.log'))+'"'),
  '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
  '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null')
 Json ($Role+'.command.json') @{arguments=$args}
 $processes[$Role]=Start-Process 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList $args -WindowStyle Hidden -PassThru
 Write-Output ('Started '+$Role+'; evidence='+$root)
}
$environment=@{}
foreach($name in @('UE_SKIP_UBT_SDK_SETUP','PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')){$environment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
 if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw 'UDP port is in use'}
 $proof.baselineModuleSHA256=Hash $module
 $critical=Get-Content -Raw -Encoding UTF8 (Join-Path $workspace 'Saved/Tests/DamageTakenIncrease-20261101/Normal3/SourceInputs.json')|ConvertFrom-Json
 foreach($entry in $critical){if((Hash $entry.path) -ne $entry.sha256){throw 'Debuff source differs from completed native baseline; review before network test'}}
 Json 'ProductionInputs.json' @($critical|ForEach-Object {@{path=[string]$_.path;sha256=[string]$_.sha256}})
 # Freeze the installed target's link graph, without invoking shared UHT/UBT.
 $linkSource=Join-Path $objects 'UnrealEditor-Project_RPG.dll.rsp'
 $link=Get-Content -Raw -Encoding UTF8 $linkSource
 Copy-Fixed $linkSource (Join-Path $inputsRoot 'Link.original.rsp')
 $linkFiles=@([regex]::Matches($link,'"([^"\r\n]+\.(?:obj|res|lib))"')|ForEach-Object {$_.Groups[1].Value}|Where-Object {[IO.Path]::IsPathRooted($_)}|Sort-Object -Unique)
 foreach($file in $linkFiles){
  $destination=Join-Path $inputsRoot ('Link/'+[guid]::NewGuid().ToString('N')+[IO.Path]::GetExtension($file))
  Copy-Fixed $file $destination;$link=$link.Replace($file,$destination.Replace('\','/'))
 }
 $closure=@()
 foreach($leaf in @('RPGGuardianMelee2NetworkProbe','RPGDamageTakenIncreaseTests','RPGDefenseReductionTests','RPGSkillDamageSecurityTests')){
  $dep=Get-Content -Raw -Encoding UTF8 (Join-Path $objects ($leaf+'.cpp.dep.json'))|ConvertFrom-Json
  $closure+=@($dep.Data.Includes|Where-Object {$_.StartsWith($workspace,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($_) -in @('.h','.inl')})
 }
 # UBT's per-module definitions include this shared definitions header even
 # when the private fixture intentionally compiles without the shared PCH.
 $closure+=@(Get-ChildItem -LiteralPath (Join-Path $workspace 'Intermediate/Build/Win64/x64/Project_RPGEditor/Development/UnrealEd') -Filter *.h -File|ForEach-Object {$_.FullName})
 foreach($file in @($closure|Sort-Object -Unique)){
  $destination=Join-Path $inputsRoot $file.Substring($workspace.Length+1);Copy-Fixed $file $destination
 }
 $cppRsp=Join-Path $objects 'RPGGuardianMelee2NetworkProbe.cpp.obj.rsp'
 $sharedRsp=Join-Path $objects 'Project_RPG.Shared.rsp'
 Copy-Fixed $cppRsp (Join-Path $inputsRoot 'Compile.original.rsp')
 Copy-Fixed $sharedRsp (Join-Path $inputsRoot $sharedRsp.Substring($workspace.Length+1))
 $source=Join-Path $PSScriptRoot 'Fixtures/CombatDebuffNetworkProbe.cpp';Copy-Fixed $source (Join-Path $temp 'Probe.cpp')
 $compile=Get-Content -Raw -Encoding UTF8 $cppRsp
 $compile=Replace-Root $compile
 $compile=[regex]::Replace($compile,'(?m)^"[^"\r\n]+\.cpp"',('"'+$temp.Replace('\','/')+'/Probe.cpp"'))
 $compile=[regex]::Replace($compile,'(?m)^/(?:Yu|Fp|Fo|sourceDependencies|experimental:log)[^\r\n]*\r?\n','')
 $compile=[regex]::Replace($compile,'(?m)^/FI[^\r\n]*SharedPCH[^\r\n]*\r?\n','')
 $newObject=Join-Path $temp 'Probe.obj';$compile+="`r`n/Fo`"$($newObject.Replace('\','/'))`"`r`n"
 TextFile (Join-Path $temp 'Compile.rsp') $compile
 $sharedCopy=Join-Path $inputsRoot $sharedRsp.Substring($workspace.Length+1)
 TextFile $sharedCopy (Replace-Root ([IO.File]::ReadAllText($sharedCopy)))
 TextFile (Join-Path $root 'Compile.rsp') $compile;TextFile (Join-Path $root 'Shared.rsp') ([IO.File]::ReadAllText($sharedCopy))
 Json 'Snapshot.json' @($records.ToArray())
 foreach($entry in $records){if((Hash $entry.path) -ne $entry.sha256){throw 'Original inputs changed during snapshot'}}
 if((Hash $module) -ne $proof.baselineModuleSHA256){throw 'Installed module changed while freezing its objects'}
 $proof.originalInputCount=$records.Count
 $compiler='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe'
 Json 'Compiler.command.json' @{executable=$compiler;arguments=@('@'+(Join-Path $temp 'Compile.rsp'));workingDirectory='D:/UE_5.8/Engine/Source';sharedPCH=$false}
 Push-Location 'D:/UE_5.8/Engine/Source'
 try{& $compiler ('@'+(Join-Path $temp 'Compile.rsp')) *> (Join-Path $root 'Compile.log');$proof.compileExit=$LASTEXITCODE}finally{Pop-Location}
 if($proof.compileExit -ne 0){throw 'Private probe compilation failed'}
 $link=[regex]::Replace($link,'(?m)^/(?:IMPLIB|EXP):[^\r\n]*\r?\n','')
 foreach($pair in @(@('OUT','dll'),@('PDB','pdb'))){$link=[regex]::Replace($link,('/'+$pair[0]+':"[^"]+"'),('/'+$pair[0]+':"'+$temp.Replace('\','/')+'/UnrealEditor-Project_RPG.'+$pair[1]+'"'))}
 $link+="`r`n`"$($newObject.Replace('\','/'))`"`r`n/NOIMPLIB`r`n/NOEXP`r`n"
 TextFile (Join-Path $temp 'Link.rsp') $link
 TextFile (Join-Path $root 'Link.rsp') $link
 Push-Location 'D:/UE_5.8/Engine/Source'
 $linker=$compiler.Replace('/cl.exe','/link.exe')
 try{& $linker ('@'+(Join-Path $temp 'Link.rsp')) *> (Join-Path $root 'Link.log');$proof.linkExit=$LASTEXITCODE}finally{Pop-Location}
 if($proof.linkExit -ne 0){throw 'Private probe link failed'}
 # Runtime files close separately; the game DLL is our private output, never the original.
 Copy-Fixed (Join-Path $workspace 'Project_RPG.uproject') (Join-Path $runtime 'Project_RPG.uproject')
 Copy-Tree (Join-Path $workspace 'Config') (Join-Path $runtime 'Config')
 $bin=Join-Path $runtime 'Binaries/Win64';$runtimeModule=Join-Path $bin 'UnrealEditor-Project_RPG.dll'
 Copy-Fixed (Join-Path $temp 'UnrealEditor-Project_RPG.dll') $runtimeModule;$proof.moduleSHA256=Hash $runtimeModule
 foreach($name in @('UnrealEditor-Project_RPGEditor.dll','tbb12.dll','tbbmalloc.dll')){if(Test-Path -LiteralPath (Join-Path $workspace ('Binaries/Win64/'+$name))){Copy-Fixed (Join-Path $workspace ('Binaries/Win64/'+$name)) (Join-Path $bin $name)}}
 $engine=Get-Content -Raw 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.modules'|ConvertFrom-Json
 TextFile (Join-Path $bin 'UnrealEditor.modules') (@{BuildId=$engine.BuildId;Modules=@{Project_RPG='UnrealEditor-Project_RPG.dll';Project_RPGEditor='UnrealEditor-Project_RPGEditor.dll'}}|ConvertTo-Json)
 foreach($descriptor in Get-ChildItem -LiteralPath (Join-Path $workspace 'Plugins') -Filter *.uplugin -File -Recurse){
  $destination=Join-Path $runtime $descriptor.DirectoryName.Substring($workspace.Length+1)
  Copy-Fixed $descriptor.FullName (Join-Path $destination $descriptor.Name)
  Copy-Tree (Join-Path $descriptor.DirectoryName 'Config') (Join-Path $destination 'Config')
  Copy-Tree (Join-Path $descriptor.DirectoryName 'Binaries') (Join-Path $destination 'Binaries')
  foreach($name in @('Content','Resources')){if(Test-Path -LiteralPath (Join-Path $descriptor.DirectoryName $name)){New-Item -ItemType Junction -Path (Join-Path $destination $name) -Target (Join-Path $descriptor.DirectoryName $name)|Out-Null}}
 }
 $hud=Join-Path $workspace 'Content/Blueprints/UI/PlayerHUD'
 $hudAbility=Join-Path $workspace 'Content/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD.uasset'
 $before=@(Get-ChildItem -LiteralPath $hud -File -Recurse|Sort-Object FullName|ForEach-Object {$_.FullName+'|'+(Hash $_.FullName)});$before+=($hudAbility+'|'+(Hash $hudAbility))
 Snapshot-Content (Join-Path $workspace 'Content') (Join-Path $runtime 'Content')
 $after=@(Get-ChildItem -LiteralPath $hud -File -Recurse|Sort-Object FullName|ForEach-Object {$_.FullName+'|'+(Hash $_.FullName)});$after+=($hudAbility+'|'+(Hash $hudAbility))
 if(@(Compare-Object $before $after).Count){throw 'HUD cohort changed while closing runtime'}
 # Snapshot records include rewritten compiler response copies; compare originals,
 # not the intentional relocated copies. No original file remains a fixed input.
 foreach($entry in $records){if((Hash $entry.path) -ne $entry.sha256){throw 'Original cohort changed before closure'}}
 $closed=$true;$proof.closedUtc=[DateTime]::UtcNow.ToString('o');Json 'ClosedModule.proof.json' $proof
 $env:UE_SKIP_UBT_SDK_SETUP='1';foreach($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
 Start-Role 'server'
 $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
 while(!(Select-String -LiteralPath (Join-Path $root 'server.log') -SimpleMatch 'COMBAT_DEBUFF_NETWORK_SERVER_READY' -Quiet -ErrorAction SilentlyContinue)){
  Healthy;if([DateTime]::UtcNow -gt $deadline){throw 'Server startup timeout'};Start-Sleep -Milliseconds 200
 }
 Start-Role 'owner';Wait-File 'owner.boot.json';Signal 'owner.connect';Wait-File 'owner.connected.json'
 Start-Role 'observer';Wait-File 'observer.boot.json';Signal 'start';Wait-File 'stage.json';Signal 'observer.connect'
 foreach($role in @('server','owner','observer')){Wait-File ($role+'.result.json')}
 $results=@{}
 foreach($role in @('server','owner','observer')){
  $r=Get-Content -Raw -Encoding UTF8 (Join-Path $root ($role+'.result.json'))|ConvertFrom-Json
  Validate-Result $r $role
  $results[$role]=$r
 }
 if($loaded.Count -ne 3){throw 'Actual modules not observed in all three roles'}
 Json 'Observations.proof.json' @{passed=$true;results=$results;actualModules=$loaded;nativeApprovedStimulus=$true;physicalInput=$false;lateJoin=$true}
 # Clients release their connections before the host. Exit failures remain failures.
 foreach($role in @('observer','owner','server')){
  Signal ($role+'.exit');if(!$processes[$role].WaitForExit(30000)){throw ('Exit timeout '+$role)}
  if($processes[$role].ExitCode -ne 0){throw ('Nonzero exit '+$role)}
 }
 $proof.processExitCodes=@{};foreach($role in $processes.Keys){$proof.processExitCodes[$role]=$processes[$role].ExitCode}
 $proof.passed=$true;$proof.results=$results;$proof.actualModules=$loaded
}catch{$failure=$_.Exception.Message;$proof.passed=$false;Write-Output $failure}finally{
 foreach($entry in $environment.GetEnumerator()){[Environment]::SetEnvironmentVariable($entry.Key,$entry.Value,'Process')}
 $released=$true
 foreach($role in $processes.Keys){
  $p=$processes[$role]
  try{
   if(!$p.HasExited){$own=Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)";if(!$own.CommandLine.Contains($temp) -or !$own.CommandLine.Contains('-RPGCombatDebuffNetwork')){throw 'Owned process identity mismatch'};Stop-Process -Id $p.Id}
   if(!$p.WaitForExit(10000)){throw 'Owned process did not release runtime'}
  }catch{$released=$false;$failure=([string]$failure+'; '+$_.Exception.Message)}
 }
 if(!$released){$proof.passed=$false}
 if($released){try{$cleanup=Remove-GuardianProbeArtifact $temp (Join-Path $workspace 'Saved/CDB')}catch{$failure=([string]$failure+'; '+$_.Exception.Message);$proof.passed=$false}}
 $proof.failure=$failure;$proof.cleanup=$cleanup;$proof.temporaryArtifactsRemoved=(!(Test-Path -LiteralPath $temp));$proof.privateHashesHistorical=$true
 Json 'Verification.proof.json' $proof
}
if(!$proof.passed){throw $failure}
Write-Output ('PASS combat debuff network; evidence='+$root+'; deletedBytes='+$cleanup.deletedBytes)
