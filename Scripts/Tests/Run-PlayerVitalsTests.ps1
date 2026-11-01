[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ModulePath,
    [string]$EngineRoot='D:/UE_5.8',
    [ValidateRange(30,900)][int]$TimeoutSeconds=300
)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId=[Guid]::NewGuid().ToString('N')
# Reuse the established UI snapshot/cleanup boundary, including junction protection.
$root=Join-Path $workspace ('Saved/Tests/ActionProgress/'+$runId)
$temp=Join-Path $root 'Temp'
$runtime=Join-Path $temp 'RuntimeProject'
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$moduleInput=[IO.Path]::GetFullPath($ModulePath)
$process=$null;$failure=$null;$ownsTemp=$false
$proof=[ordered]@{feature='PlayerVitals';passed=$false;tests=@();cleanup=$null;buildPerformed=$false;originalAssetsSaved=0}
$snapshots=[Collections.Generic.List[object]]::new()
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'Run-ActionProgressTests.ps1'),[ref]$null,[ref]$null)
foreach($name in @('Write-Json','Copy-FrozenFile','Copy-FrozenTree','Run-OwnedEditor','Remove-OwnedTemp')){
    $definition=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
    if(!$definition){throw ('Required UI snapshot helper missing: '+$name)}
    . ([scriptblock]::Create($definition.Extent.Text))
}
foreach($file in @($editor,$moduleInput)){if(!(Test-Path -LiteralPath $file)){throw ('Missing prerequisite: '+$file)}}
$stream=[IO.File]::OpenRead($moduleInput)
try{
    $buffer=[byte[]]::new(262144);$tail='';$asciiTail='';$found=$false
    while(($read=$stream.Read($buffer,0,$buffer.Length)) -gt 0){
        $text=$tail+[Text.Encoding]::Unicode.GetString($buffer,0,$read)
        $ascii=$asciiTail+[Text.Encoding]::ASCII.GetString($buffer,0,$read)
        if($text.Contains('ProjectRPG.UI.PlayerVitals.') -or $ascii.Contains('ProjectRPG.UI.PlayerVitals.')){$found=$true;break}
        $tail=$text.Substring([Math]::Max(0,$text.Length-64))
        $asciiTail=$ascii.Substring([Math]::Max(0,$ascii.Length-64))
    }
}finally{$stream.Dispose()}
if(!$found){throw 'Module lacks PlayerVitals tests; provide a current Development module'}
$expected=@('ProjectRPG.UI.PlayerVitals.InitialAndAttributeChanges','ProjectRPG.UI.PlayerVitals.SourceAndWidgetLifetime',
    'ProjectRPG.UI.PlayerVitals.OwningPawnChanges','ProjectRPG.UI.PlayerVitals.InvalidValuesAndMissingSource')
try{
    $null=New-Item -ItemType Directory -Path $runtime
    $ownsTemp=$true
    Copy-FrozenFile (Join-Path $workspace 'Project_RPG.uproject') (Join-Path $runtime 'Project_RPG.uproject')
    Copy-FrozenTree (Join-Path $workspace 'Config') (Join-Path $runtime 'Config')
    $binaries=Join-Path $runtime 'Binaries/Win64'
    Copy-FrozenFile $moduleInput (Join-Path $binaries 'UnrealEditor-Project_RPG.dll')
    # Do not read/copy the shared game DLL, PDB or project metadata owned by an installer.
    foreach($name in @('UnrealEditor-Project_RPGEditor.dll','tbb12.dll','tbbmalloc.dll')){
        $source=Join-Path $workspace ('Binaries/Win64/'+$name)
        if(Test-Path -LiteralPath $source){Copy-FrozenFile $source (Join-Path $binaries $name)}
    }
    $engineMetadata=Get-Content -Raw -LiteralPath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.modules') | ConvertFrom-Json
    $metadata=@{BuildId=$engineMetadata.BuildId;Modules=@{Project_RPG='UnrealEditor-Project_RPG.dll';Project_RPGEditor='UnrealEditor-Project_RPGEditor.dll'}}
    [IO.File]::WriteAllText((Join-Path $binaries 'UnrealEditor.modules'),($metadata | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $null=New-Item -ItemType Junction -Path (Join-Path $runtime 'Content') -Target (Join-Path $workspace 'Content')
    foreach($descriptor in Get-ChildItem -LiteralPath (Join-Path $workspace 'Plugins') -Filter *.uplugin -File -Recurse){
        $destination=Join-Path $runtime $descriptor.DirectoryName.Substring($workspace.Length+1)
        Copy-FrozenFile $descriptor.FullName (Join-Path $destination $descriptor.Name)
        Copy-FrozenTree (Join-Path $descriptor.DirectoryName 'Config') (Join-Path $destination 'Config')
        Copy-FrozenTree (Join-Path $descriptor.DirectoryName 'Binaries') (Join-Path $destination 'Binaries') -Binaries
        foreach($name in @('Content','Resources')){
            $source=Join-Path $descriptor.DirectoryName $name
            if(Test-Path -LiteralPath $source){$null=New-Item -ItemType Junction -Path (Join-Path $destination $name) -Target $source}
        }
    }
    foreach($entry in $snapshots){if((Get-FileHash -LiteralPath $entry.source).Hash -cne $entry.sha256){throw 'Snapshot source changed while copying'}}
    Write-Json 'Snapshot.json' $snapshots.ToArray()
    $module=Join-Path $binaries 'UnrealEditor-Project_RPG.dll'
    $proof.moduleSHA256=(Get-FileHash -LiteralPath $module).Hash
    $assets=@('Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset','Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerSkillBar.uasset')
    $assetHashes=@($assets | ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $workspace $_)).Hash}})
    Write-Json 'AssetHashes.json' $assetHashes
    $arguments=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.UI.PlayerVitals."','-TestExit="Automation Test Queue Empty"',
        '-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-NoLoadingScreen','-DDC-ForceMemoryCache','-DisablePython',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root 'Native.log')+'"'),
        ('-ReportExportPath="'+(Join-Path $temp 'Report')+'"'))
    Run-OwnedEditor 'Native' $arguments $module
    $report=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $temp 'Report/index.json') | ConvertFrom-Json
    Write-Json 'NativeResults.json' $report
    $log=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'Native.log')
    foreach($name in $expected){
        $entry=@($report.tests | Where-Object fullTestPath -ceq $name)
        if($entry.Count -ne 1){throw ('Missing/duplicate test: '+$name)}
        $proof.tests+=@{path=$name;state=$entry[0].state;errors=$entry[0].errors;warnings=$entry[0].warnings}
        $completed=[regex]::Matches($log,('Test Completed\. Result=\{Success\}.*Path=\{'+[regex]::Escape($name)+'\}')).Count
        if($entry[0].state -ne 'Success' -or $entry[0].errors -ne 0 -or $completed -ne 1){throw ('Test failed: '+$name)}
    }
    if(@($report.tests).Count -ne $expected.Count -or ($report.succeeded+$report.succeededWithWarnings) -ne $expected.Count -or
       $report.failed -ne 0 -or $report.notRun -ne 0 -or $report.inProcess -ne 0){throw 'Unexpected automation totals'}
    $discovery=[regex]::Match($log,('Found '+$expected.Count+' automation tests'))
    if(!$discovery.Success -or $log -match 'Fatal error:|Assertion failed:|Ensure condition failed:'){throw 'Invalid discovery/fatal native log'}
    $proof.preDiscoveryErrors=[regex]::Matches($log.Substring(0,$discovery.Index),'(?m)^.*: Error:.*$').Count
    $proof.postDiscoveryErrors=[regex]::Matches($log.Substring($discovery.Index),'(?m)^.*: Error:.*$').Count
    if($proof.postDiscoveryErrors){throw 'Error after automation discovery'}
    foreach($entry in $snapshots){if((Get-FileHash -LiteralPath $entry.copy).Hash -cne $entry.sha256){throw 'Frozen input changed during execution'}}
    foreach($entry in $assetHashes){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.path)).Hash -cne $entry.sha256){throw 'HUD asset changed during verification'}}
    $proof.snapshotFiles=$snapshots.Count;$proof.snapshotInputsUnchanged=$true;$proof.assetHashesUnchanged=$true;$proof.passed=$true
}catch{$failure=$_.Exception.Message}finally{
    $released=$true
    if($process){try{if(!$process.HasExited){$process.Kill()};if(!$process.WaitForExit(10000)){throw 'Own process did not exit'}}catch{$released=$false;$failure=([string]$failure+'; '+$_.Exception.Message)}}
    if($ownsTemp -and $released){
        for($attempt=1;$attempt -le 20;$attempt++){
            try{$proof.cleanup=Remove-OwnedTemp;break}catch{if($attempt -eq 20){$failure=([string]$failure+'; cleanup: '+$_.Exception.Message)}else{Start-Sleep -Milliseconds 500}}
        }
    }
    $proof.failure=$failure;$proof.passed=($proof.passed -and !$failure -and $proof.cleanup -and $proof.cleanup.complete)
    $proof.historicalSnapshotHashesOnly=$true;$proof.scope='Native UMG/Slate/GAS fixture; no authored HUD replacement, PIE or physical input'
    if(Test-Path -LiteralPath $root){Write-Json 'Result.json' $proof;Write-Output ('Result='+$proof.passed+'; proof='+(Join-Path $root 'Result.json'))}
}
if(!$proof.passed){throw $failure}
