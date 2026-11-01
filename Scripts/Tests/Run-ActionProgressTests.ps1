[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:/UE_5.8',
    [ValidateRange(30,900)][int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId = [Guid]::NewGuid().ToString('N')
$root = Join-Path $workspace ('Saved/Tests/ActionProgress/' + $runId)
$temp = Join-Path $root 'Temp'
$runtime = Join-Path $temp 'RuntimeProject'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$process = $null
$ownsTemp = $false
$failure = $null
$proof = [ordered]@{passed=$false;runId=$runId;tests=@();cleanup=$null;buildPerformed=$false;originalAssetsSaved=0}
$snapshots = [Collections.Generic.List[object]]::new()
$expected = @(
    'ProjectRPG.UI.ActionProgress.ModesAndReadiness',
    'ProjectRPG.UI.ActionProgress.ExecutionHandoff',
    'ProjectRPG.UI.ActionProgress.SourceAndOwnerLifetime',
    'ProjectRPG.UI.ActionProgress.InvalidInputPreservesState',
    'ProjectRPG.UI.ActionProgressWidget.Presentation',
    'ProjectRPG.UI.ActionProgressWidget.BindingLifetime',
    'ProjectRPG.UI.ActionProgressHUD.ViewportPossessionLifecycle',
    'ProjectRPG.UI.ActionProgressHUD.PresentationConfiguration',
    'ProjectRPG.UI.SkillActionProgress.ChargeReadyAndRelease',
    'ProjectRPG.UI.SkillActionProgress.HoldingAndCasting',
    'ProjectRPG.UI.SkillActionProgress.HandoffAndReentrantCancel',
    'ProjectRPG.UI.SkillActionProgress.FrozenDisplayName',
    'ProjectRPG.UI.LegacyChargeProgress.LifetimeAndHandoff',
    'ProjectRPG.Progression.TrainingCharge.DefaultReleaseAndReset',
    'ProjectRPG.Progression.TrainingCharge.CancellationReuseAndValidation'
)
function Write-Json([string]$Name, $Value) {
    [IO.File]::WriteAllText((Join-Path $root $Name), ($Value | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
}
function Copy-FrozenFile([string]$Source, [string]$Destination) {
    if ((Get-Item -LiteralPath $Source -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Snapshot file is a reparse point'}
    $before = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force
    Copy-Item -LiteralPath $Source -Destination $Destination
    if ((Get-FileHash -LiteralPath $Destination).Hash -ne $before -or (Get-FileHash -LiteralPath $Source).Hash -ne $before) {
        throw ('Snapshot changed while copying: ' + $Source)
    }
    $snapshots.Add([pscustomobject]@{source=$Source;copy=$Destination;sha256=$before})
}
function Copy-FrozenTree([string]$Source, [string]$Destination, [switch]$Binaries) {
    if (!(Test-Path -LiteralPath $Source)) {return}
    if ((Get-Item -LiteralPath $Source -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Snapshot tree is a reparse point'}
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($item.PSIsContainer) {Copy-FrozenTree $item.FullName (Join-Path $Destination $item.Name) -Binaries:$Binaries}
        elseif (!$Binaries -or $item.Extension -notin @('.pdb','.obj','.pch','.lib','.exp')) {
            Copy-FrozenFile $item.FullName (Join-Path $Destination $item.Name)
        }
    }
}
function Run-OwnedEditor([string]$Stage, [string[]]$Arguments, [string]$Module) {
    Write-Json ($Stage+'Command.json') @{file=$editor;arguments=$Arguments}
    $script:process = Start-Process $editor -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    $null = $script:process.Handle
    Write-Output ($Stage+' PID='+$script:process.Id)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $loaded = $null
    while (!$script:process.HasExited) {
        if (!$loaded) {
            $entries = @((Get-Process -Id $script:process.Id -ErrorAction SilentlyContinue).Modules | Where-Object ModuleName -eq 'UnrealEditor-Project_RPG.dll')
            if ($entries.Count -eq 1) {$loaded = $entries[0].FileName}
        }
        if ([DateTime]::UtcNow -ge $deadline) {throw ($Stage+' timed out')}
        $null = $script:process.WaitForExit(200)
    }
    $script:process.WaitForExit()
    $exitCode = $script:process.ExitCode
    $proof[$Stage] = @{pid=$script:process.Id;exitCode=$exitCode;loadedModule=$loaded}
    if ($exitCode -ne 0 -or !$loaded -or [IO.Path]::GetFullPath($loaded) -ine [IO.Path]::GetFullPath($Module)) {
        throw ($Stage+' failed or loaded the wrong module')
    }
}
function Remove-OwnedTemp {
    $absolute = [IO.Path]::GetFullPath($temp)
    $allowed = [IO.Path]::GetFullPath((Join-Path $workspace ('Saved/Tests/ActionProgress/'+$runId+'/Temp')))
    if ($runId -notmatch '^[a-f0-9]{32}$' -or $absolute -ine $allowed) {throw 'Invalid cleanup boundary'}
    $ancestor = Split-Path -Parent $absolute
    while ($ancestor -and $ancestor.Length -ge $workspace.Length) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Cleanup ancestor is a reparse point'}
        $ancestor = Split-Path -Parent $ancestor
    }
    $relative = 'Saved/Tests/ActionProgress/'+$runId
    $claims = Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8
    foreach ($claim in $claims) {
        if ($claim -match '^\| ' -and $claim -match '\uC9C4\uD589 \uC911|\uC608\uC57D \uC911|\uAC80\uC99D \uC911' -and $claim -notmatch 'action-progress-installed-audit-20261031' -and $claim.Replace('\','/').Contains($relative)) {
            throw 'Another active reservation references this Temp'
        }
    }
    $nodes = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Stack[string]]::new()
    if (Test-Path -LiteralPath $absolute) {$pending.Push($absolute)}
    while ($pending.Count) {
        $item = Get-Item -LiteralPath $pending.Pop() -Force
        if (!$item.FullName.StartsWith($absolute+'\',[StringComparison]::OrdinalIgnoreCase) -and $item.FullName -ine $absolute) {throw 'Cleanup escaped Temp'}
        $link = !!($item.Attributes -band [IO.FileAttributes]::ReparsePoint)
        $nodes.Add([pscustomobject]@{path=$item.FullName;directory=$item.PSIsContainer;link=$link;length=$(if(!$link -and !$item.PSIsContainer){$item.Length}else{0L})})
        if ($item.PSIsContainer -and !$link) {foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) {$pending.Push($child.FullName)}}
    }
    # Check file use before removing anything. Junctions are entries, never recursive inputs.
    foreach ($node in $nodes) {
        if (!$node.directory -and !$node.link) {$stream=[IO.File]::Open($node.path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$stream.Dispose()}
    }
    $bytes = 0L; $links = 0
    foreach ($node in ($nodes | Sort-Object {$_.path.Length} -Descending)) {
        $item = Get-Item -LiteralPath $node.path -Force
        if (!!($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne $node.link) {throw 'Cleanup entry changed after preflight'}
        if ($node.directory) {[IO.Directory]::Delete($node.path,$false)} else {Remove-Item -LiteralPath $node.path -Force}
        $bytes += $node.length
        if ($node.link) {$links++}
    }
    @{deletedBytes=$bytes;unlinked=$links;complete=!(Test-Path -LiteralPath $absolute);retainedLargeArtifacts=@()}
}
$null = New-Item -ItemType Directory -Path $root
try {
    if (!(Test-Path -LiteralPath $editor)) {throw 'UnrealEditor-Cmd not found'}
    $null = New-Item -ItemType Directory -Path $runtime
    $ownsTemp = $true
    Copy-FrozenFile (Join-Path $workspace 'Project_RPG.uproject') (Join-Path $runtime 'Project_RPG.uproject')
    Copy-FrozenTree (Join-Path $workspace 'Config') (Join-Path $runtime 'Config')
    Copy-FrozenTree (Join-Path $workspace 'Binaries/Win64') (Join-Path $runtime 'Binaries/Win64') -Binaries
    $null = New-Item -ItemType Junction -Path (Join-Path $runtime 'Content') -Target (Join-Path $workspace 'Content')
    foreach ($descriptor in Get-ChildItem -LiteralPath (Join-Path $workspace 'Plugins') -Filter *.uplugin -File -Recurse) {
        $relative = $descriptor.DirectoryName.Substring($workspace.Length+1)
        $destination = Join-Path $runtime $relative
        Copy-FrozenFile $descriptor.FullName (Join-Path $destination $descriptor.Name)
        Copy-FrozenTree (Join-Path $descriptor.DirectoryName 'Config') (Join-Path $destination 'Config')
        Copy-FrozenTree (Join-Path $descriptor.DirectoryName 'Binaries') (Join-Path $destination 'Binaries') -Binaries
        foreach ($name in @('Content','Resources')) {
            $source = Join-Path $descriptor.DirectoryName $name
            if (Test-Path -LiteralPath $source) {$null=New-Item -ItemType Junction -Path (Join-Path $destination $name) -Target $source}
        }
    }
    # The other owner may build original modules later; reject a mixed snapshot now.
    foreach ($entry in $snapshots) {if ((Get-FileHash -LiteralPath $entry.source).Hash -ne $entry.sha256) {throw 'Original changed during snapshot creation'}}
    Write-Json 'Snapshot.json' $snapshots.ToArray()
    $module = Join-Path $runtime 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
    $proof.moduleSHA256 = (Get-FileHash -LiteralPath $module).Hash
    $assets = @('Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset','Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerSkillBar.uasset')
    $assetHashes = @($assets | ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $workspace $_)).Hash}})
    Write-Json 'AssetHashes.json' $assetHashes
    $filter = 'ProjectRPG.UI.ActionProgress.+ProjectRPG.UI.ActionProgressWidget.+ProjectRPG.UI.ActionProgressHUD.+ProjectRPG.UI.SkillActionProgress.+ProjectRPG.UI.LegacyChargeProgress.+ProjectRPG.Progression.TrainingCharge.'
    $common = @('-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-NoLoadingScreen','-DDC-ForceMemoryCache',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    $args = @(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$filter+'"'),'-TestExit="Automation Test Queue Empty"','-DisablePython',
        ('-UserDir="'+(Join-Path $temp 'NativeUser')+'"'),('-abslog="'+(Join-Path $root 'Native.log')+'"'),('-ReportExportPath="'+(Join-Path $temp 'Report')+'"')) + $common
    Run-OwnedEditor 'Native' $args $module
    $report = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $temp 'Report/index.json') | ConvertFrom-Json
    foreach ($name in $expected) {
        $entry = @($report.tests | Where-Object fullTestPath -CEQ $name)
        if ($entry.Count -ne 1) {throw ('Missing/duplicate test: '+$name)}
        $proof.tests += @{path=$name;state=$entry[0].state;errors=$entry[0].errors;warnings=$entry[0].warnings}
        $line = @(Get-Content -Encoding UTF8 -LiteralPath (Join-Path $root 'Native.log') | Where-Object {$_ -match ('Test Completed\. Result=\{Success\}.*Path=\{'+[regex]::Escape($name)+'\}')})
        if ($entry[0].state -ne 'Success' -or $entry[0].errors -ne 0 -or $line.Count -ne 1) {throw ('Test failed: '+$name)}
    }
    if (@($report.tests).Count -ne $expected.Count -or ($report.succeeded+$report.succeededWithWarnings) -ne $expected.Count -or $report.failed -ne 0 -or $report.notRun -ne 0 -or $report.inProcess -ne 0) {throw 'Unexpected automation totals'}
    $log = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'Native.log')
    $discovery = [regex]::Match($log,'Found 15 automation tests')
    if (!$discovery.Success -or $log -match 'Fatal error:|Assertion failed:|Ensure condition failed:') {throw 'Invalid discovery/fatal native log'}
    $proof.preDiscoveryErrors = [regex]::Matches($log.Substring(0,$discovery.Index),'(?m)^.*: Error:.*$').Count
    $proof.postDiscoveryErrors = [regex]::Matches($log.Substring($discovery.Index),'(?m)^.*: Error:.*$').Count
    if ($proof.postDiscoveryErrors) {throw 'Error after automation discovery'}
    $check = Join-Path $PSScriptRoot 'Verify-ActionProgressPresentation.py'
    $args = @(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'-run=pythonscript',('-Script="'+$check+'"'),
        ('-RPGActionProgressProof="'+(Join-Path $root 'Presentation.json')+'"'),('-UserDir="'+(Join-Path $temp 'ReloadUser')+'"'),('-abslog="'+(Join-Path $root 'Reload.log')+'"')) + $common
    Run-OwnedEditor 'Reload' $args $module
    $presentation = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'Presentation.json') | ConvertFrom-Json
    if (!$presentation.passed) {throw 'Presentation reflection/reload proof missing'}
    foreach ($entry in $snapshots) {if ((Get-FileHash -LiteralPath $entry.copy).Hash -ne $entry.sha256) {throw 'Frozen input changed during execution'}}
    foreach ($entry in $assetHashes) {if ((Get-FileHash -LiteralPath (Join-Path $workspace $entry.path)).Hash -ne $entry.sha256) {throw 'HUD asset changed during read-only verification'}}
    $proof.snapshotFiles = $snapshots.Count
    $proof.snapshotInputsUnchanged = $true
    $proof.assetHashesUnchanged = $true
    $proof.passed = $true
} catch {
    $failure = $_.Exception.Message
} finally {
    $released = $true
    if ($process) {
        try {
            # Retained Process handle identifies only the process this runner started.
            if (!$process.HasExited) {$process.Kill()}
            if (!$process.WaitForExit(10000)) {throw 'Own process has not exited'}
        } catch {$released=$false;$failure=([string]$failure+'; '+$_.Exception.Message)}
    }
    if ($ownsTemp -and $released) {
        try {$proof.cleanup=Remove-OwnedTemp} catch {$failure=([string]$failure+'; Cleanup: '+$_.Exception.Message)}
    }
    $proof.failure = $failure
    $proof.passed = ($proof.passed -and !$failure -and $proof.cleanup -and $proof.cleanup.complete)
    $proof.historicalSnapshotHashesOnly = $true
    $proof.completedUtc = [DateTime]::UtcNow.ToString('o')
    Write-Json 'Result.json' $proof
    Write-Output ('Result='+$proof.passed+'; proof='+ (Join-Path $root 'Result.json'))
}
if (!$proof.passed) {throw $failure}
