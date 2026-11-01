[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:/UE_5.8',
    [string]$PythonScript = 'Scripts/Editor/InspectGreatSwordPrototype.py',
    [switch]$Playback,
    [switch]$Apply,
    [switch]$Native,
    [ValidateRange(30,900)][int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId = [Guid]::NewGuid().ToString('N')
$root = Join-Path $workspace ('Saved/BuildChecks/GreatSwordPrototype-20261102/' + $runId)
$temp = Join-Path $root 'Temp'
$runtime = Join-Path $temp 'RuntimeProject'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if ($Playback) {$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'}
$process = $null
$ownsTemp = $false
$failure = $null
$proof = [ordered]@{passed=$false;runId=$runId;tests=@();cleanup=$null;buildPerformed=$false;originalAssetsSaved=0}
$snapshots = [Collections.Generic.List[object]]::new()

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
    $allowed = [IO.Path]::GetFullPath((Join-Path $workspace ('Saved/BuildChecks/GreatSwordPrototype-20261102/'+$runId+'/Temp')))
    if ($runId -notmatch '^[a-f0-9]{32}$' -or $absolute -ine $allowed) {throw 'Invalid cleanup boundary'}
    $ancestor = Split-Path -Parent $absolute
    while ($ancestor -and $ancestor.Length -ge $workspace.Length) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Cleanup ancestor is a reparse point'}
        $ancestor = Split-Path -Parent $ancestor
    }
    $relative = 'Saved/BuildChecks/GreatSwordPrototype-20261102/'+$runId
    $claims = Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8
    foreach ($claim in $claims) {
        if ($claim -match '^\| ' -and $claim -match '\uC9C4\uD589 \uC911|\uC608\uC57D \uC911|\uAC80\uC99D \uC911' -and $claim -notmatch 'greatsword-prototype-20261102' -and $claim.Replace('\','/').Contains($relative)) {
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
    $common = @('-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-NoLoadingScreen','-DDC-ForceMemoryCache',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    $check = Join-Path $workspace $PythonScript
    $args = @(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'-run=pythonscript',('-Script="'+$check+'"'),
        ('-RPGGreatSwordProof="'+(Join-Path $root 'Inspection.json')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root 'Inspect.log')+'"')) + $common
    if ($Playback) {$args=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',('-ExecCmds="py '+$check+'"'),('-RPGGreatSwordProof="'+(Join-Path $root 'Inspection.json')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root 'Inspect.log')+'"'))+$common}
    if ($Apply) {$args += '-RPGConfigureGreatSwordPrototype'}
    if ($Native) {$args=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Skill.GreatSword.BerserkLifecycle"','-TestExit="Automation Test Queue Empty"','-DisablePython',('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root 'Inspect.log')+'"'),('-ReportExportPath="'+(Join-Path $temp 'Report')+'"'))+$common}
    Run-OwnedEditor 'Inspect' $args $module
    if ($Native) {
        $report=Get-Content -Raw -Encoding UTF8 (Join-Path $temp 'Report/index.json')|ConvertFrom-Json
        $proof.tests=@($report.tests|Select-Object fullTestPath,state,errors,warnings)
        if (@($report.tests).Count -ne 1 -or $report.failed -ne 0 -or $report.tests[0].state -ne 'Success') {throw 'Native test failed'}
    } else {
    if (!(Test-Path (Join-Path $root 'Inspection.json'))) {throw 'Inspection result missing'}
    if ($Playback -or $Apply -or $PythonScript -like '*ConfigureGreatSwordPrototype.py') {
        $inspectionResult=Get-Content -Raw -Encoding UTF8 (Join-Path $root 'Inspection.json') | ConvertFrom-Json
        if (!$inspectionResult.passed) {throw ('Inspection failed: '+$inspectionResult.error)}
        if ($Apply) {$proof.originalAssetsSaved=@($inspectionResult.skills | Where-Object saved).Count}
    }
    }
    foreach ($entry in $snapshots) {if ((Get-FileHash -LiteralPath $entry.copy).Hash -ne $entry.sha256) {throw 'Frozen input changed during execution'}}
    $proof.snapshotFiles = $snapshots.Count
    $proof.snapshotInputsUnchanged = $true
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
