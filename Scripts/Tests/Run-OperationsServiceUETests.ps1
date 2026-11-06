[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:/UE_5.8',
    [ValidatePattern('^$|^[a-f0-9]{32}$')][string]$ResumeRunId = '',
    [string[]]$OverlayPaths = @(
        'Source/Project_RPG/Public/Online/RPGServiceStatus.h',
        'Source/Project_RPG/Private/Online/RPGServiceStatus.cpp',
        'Source/Project_RPG/Public/Online/RPGServiceStatusSubsystem.h',
        'Source/Project_RPG/Private/Online/RPGServiceStatusSubsystem.cpp',
        'Source/Project_RPG/Public/Manager/HttpWebManager.h',
        'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
        'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyWidget.h',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyServiceStatus.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyMenu.cpp',
        'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyActionAvailability.h',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyActionAvailability.cpp',
        'Source/Project_RPG/Private/Tests/RPGServiceStatusTests.cpp',
        'Backend/ProjectRpg.Operations.Tests/ServiceStatusUEHost.cs',
        'Backend/ProjectRpg.Operations.Tests/Program.cs'
    ),
    [string]$Tests = 'ProjectRPG.Operations.ServiceStatus+ProjectRPG.Online.DungeonAuthentication+ProjectRPG.Online.DungeonRecovery+ProjectRPG.Online.DungeonReturn',
    [int]$MinimumTests = 10,
    # Optional bounded repair window during this validation job. A matching local retry
    # marker is required; time alone never starts another attempt. Final exit always cleans.
    [ValidateRange(0,1800)][int]$RepairWindowSeconds = 0,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId = if ($ResumeRunId) { $ResumeRunId } else { [Guid]::NewGuid().ToString('N') }
$root = Join-Path $workspace ('Saved/BuildChecks/OperationsServiceUE-20261106/' + $runId)
$tempRoot = Join-Path $workspace 'Saved/OSU'
$temp = Join-Path $tempRoot $runId.Substring(0,8)
$project = Join-Path $temp 'Project'
$process = $null
$backendProcess = $null
$ownsTemp = $false
$buildNeeded = $true
$failure = $null
$proof = [ordered]@{ Passed=$false; Head=''; Overlay=@(); Tests=@(); Cleanup=$null }
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Proof {
    [IO.File]::WriteAllText((Join-Path $root 'Result.json'), ($proof | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
}
try {
    $rows=@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_-match'^\|.*operations-service-ue-20261106'})
    if($rows.Count-ne 1){throw 'Service UE task reservation missing'}
    if ($ResumeRunId) {
        $previous = Get-Content -LiteralPath (Join-Path $root 'Result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($previous.Passed -or $previous.CleanupComplete -or !$previous.Overlay.Count -or !$previous.Head) { throw 'Not an interrupted validation cohort' }
        $null = Assert-GuardianArtifactPath $project $tempRoot
        $active = @(Get-CimInstance Win32_Process | Where-Object {
            $_.Name -in @('dotnet.exe','cl.exe','link.exe','UnrealEditor-Cmd.exe') -and
            $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))
        })
        if ($active.Count) { throw 'Interrupted cohort still has an active build/test process' }
        foreach ($entry in $previous.Overlay) {
            if (!$entry.Path.StartsWith('Source/') -or $entry.Path.Contains('..')) { throw 'Invalid recorded overlay' }
            foreach ($base in @($workspace,$project)) {
                if ((Get-FileHash -LiteralPath (Join-Path $base $entry.Path)).Hash -ne $entry.SHA256) { throw 'Interrupted inputs changed; explicit repair is required' }
            }
        }
        foreach ($property in $previous.PSObject.Properties) { $proof[$property.Name] = $property.Value }
        $OverlayPaths = @($previous.Overlay | ForEach-Object Path)
        $ownsTemp = $true
        $proof.ResumedUtc = [DateTime]::UtcNow.ToString('o')
        $proof.RepairUntilUtc = $null
        $existingModule = Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
        if ($previous.BuildExit -eq 0 -and $previous.ModuleSHA256 -and
            (Test-Path -LiteralPath $existingModule) -and
            (Get-FileHash -LiteralPath $existingModule).Hash -eq $previous.ModuleSHA256) {
            $buildNeeded = $false
            $proof.ReusedBuiltModule = $previous.ModuleSHA256
        }
    } else {
    $null = New-Item -ItemType Directory -Path $root -Force
    $null = New-Item -ItemType Directory -Path $tempRoot -Force
    $null = New-Item -ItemType Directory -Path $temp
    $ownsTemp = $true
    $null = New-Item -ItemType Directory -Path $project
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve published source revision' }
    # Archive committed source only. Another agent's uncommitted source never enters this build.
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($p in @('Source','Config','Project_RPG.uproject','Backend')) { $paths.Add($p) }
    $pluginFiles = @(git -C $workspace ls-tree -r --name-only HEAD -- Plugins)
    $descriptors = @($pluginFiles | Where-Object { $_ -like '*.uplugin' })
    foreach ($descriptor in $descriptors) {
        $paths.Add($descriptor)
        $dir = $descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach ($name in @('Source','Config','Resources','Shaders')) {
            $child = $dir+'/'+$name
            if (@($pluginFiles | Where-Object { $_.StartsWith($child+'/') }).Count) { $paths.Add($child) }
        }
    }
    $archive = Join-Path $temp 'Inputs.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths.ToArray()
    if ($LASTEXITCODE -ne 0) { throw 'Source archive failed' }
    & tar -xf $archive -C $project
    if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed' }
    foreach ($relative in $OverlayPaths) {
        if ((!$relative.StartsWith('Source/') -and !$relative.StartsWith('Backend/')) -or $relative.Contains('..')) { throw 'Overlay must be an explicit feature source file' }
        $source = Join-Path $workspace $relative
        $before = (Get-FileHash -LiteralPath $source).Hash
        $destination = Join-Path $project $relative
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
        Copy-Item -LiteralPath $source -Destination $destination
        # Copy-Item preserves an old source mtime. UBT may otherwise reuse an
        # object compiled later from a different snapshot of the same path.
        [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
        if ((Get-FileHash -LiteralPath $source).Hash -ne $before -or (Get-FileHash -LiteralPath $destination).Hash -ne $before) { throw 'Overlay changed during capture' }
        $proof.Overlay += @{ Path=$relative; SHA256=$before }
    }
    $null = New-Item -ItemType Junction -Path (Join-Path $project 'Content') -Target (Join-Path $workspace 'Content')
    foreach ($descriptor in $descriptors) {
        $dir = $descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        $content = Join-Path $workspace ($dir+'/Content')
        if (Test-Path -LiteralPath $content) { $null = New-Item -ItemType Junction -Path (Join-Path $project ($dir+'/Content')) -Target $content }
    }
    }
    $uproject = Join-Path $project 'Project_RPG.uproject'
    $build = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
    $attempt = 0
    $proof.AttemptFailures = @()
    while ($true) {
    ++$attempt
    $proof.Attempt = $attempt
    try {
    $hostProject=Join-Path $project 'Backend/ProjectRpg.Operations.Tests/ProjectRpg.Operations.Tests.csproj'
    & dotnet build $hostProject -c Release --artifacts-path (Join-Path $temp 'BackendArtifacts') --nologo *> (Join-Path $root 'BackendBuild.log')
    if($LASTEXITCODE -ne 0){throw 'Private service fixture build failed'}
    $buildArgs = @('Project_RPGEditor','Win64','Development',$uproject,'-WaitMutex','-NoUBA','-NoHotReloadFromIDE','-MaxParallelActions=4',('-log='+ (Join-Path $root 'Build.log')))
    $proof.BuildCommand = @($build) + $buildArgs
    Write-Proof
    if ($buildNeeded) {
        Write-Output ('Building independent source snapshot: '+$root)
        & $build @buildArgs *> (Join-Path $root 'BuildConsole.log')
        $proof.BuildExit = $LASTEXITCODE
        if ($LASTEXITCODE -ne 0) { throw 'Independent Editor build failed; see BuildConsole.log' }
        $buildNeeded = $false
    }
    $module = Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
    $proof.ModuleSHA256 = (Get-FileHash -LiteralPath $module).Hash
    # Query only this already-built snapshot's precompiled rules. DesktopPlatform would
    # otherwise invoke Build.bat synchronously and wait behind another agent's build.
    $dotnet = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt = Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $queryArgs = @($ubt,'-Mode=QueryTargets',('-Project='+$uproject),
        ('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')),
        '-IncludeAllTargets','-DontIncludeParentAssembly','-DontIncludeProgramTargets',
        '-NoMutex','-SkipRulesCompile','-NoLog')
    $proof.TargetQueryCommand = @($dotnet) + $queryArgs
    & $dotnet @queryArgs *> (Join-Path $root 'TargetQuery.log')
    if ($LASTEXITCODE -ne 0) { throw 'Snapshot target query failed; see TargetQuery.log' }
    $runtime=Join-Path $temp 'Runtime';$null=New-Item -ItemType Directory -Path $runtime -Force
    foreach($name in @('Bootstrap.json','BackendResult.json')){$old=Join-Path $runtime $name;if(Test-Path -LiteralPath $old){Remove-Item -LiteralPath $old}}
    $oldRoot=$env:PROJECT_RPG_SERVICE_UE_ROOT;$env:PROJECT_RPG_SERVICE_UE_ROOT=$runtime
    try{$backendProcess=Start-Process (Get-Command dotnet).Source -ArgumentList @((Join-Path $temp 'BackendArtifacts/bin/ProjectRpg.Operations.Tests/release/ProjectRpg.Operations.Tests.dll'),'--service-status-ue-host') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'Backend.log') -RedirectStandardError (Join-Path $root 'Backend.stderr.log');$null=$backendProcess.Handle}finally{$env:PROJECT_RPG_SERVICE_UE_ROOT=$oldRoot}
    $bootstrap=Join-Path $runtime 'Bootstrap.json';$ready=[DateTime]::UtcNow.AddSeconds(30)
    while(!(Test-Path -LiteralPath $bootstrap)){if($backendProcess.HasExited-or[DateTime]::UtcNow-ge$ready){throw 'Service fixture startup failed'};Start-Sleep -Milliseconds 100}
    $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $arguments = @(('"'+$uproject+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$Tests+'"'),
        '-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen',
        # Prebuilt native tests need no SDK scan; avoid waiting on another snapshot's Build.bat lock.
        '-Multiprocess',
        '-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),
        ('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root 'Native.log')+'"'),('-ReportExportPath="'+(Join-Path $temp 'Report')+'"'))
    $arguments+=('-RPGServiceStatusBootstrap="'+$bootstrap+'"')
    $proof.NativeCommand = @($editor) + $arguments
    Write-Proof
    $process = Start-Process $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $loaded = $null
    while (!$process.HasExited) {
        if (!$loaded) { $loaded = @((Get-Process -Id $process.Id -ErrorAction SilentlyContinue).Modules | Where-Object ModuleName -eq 'UnrealEditor-Project_RPG.dll') | Select-Object -First 1 -ExpandProperty FileName }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Owned native test process timed out' }
        $null = $process.WaitForExit(200)
    }
    $process.WaitForExit()
    $proof.NativeExit = $process.ExitCode
    $proof.LoadedModule = $loaded
    if ($process.ExitCode -ne 0 -or !$loaded -or [IO.Path]::GetFullPath($loaded) -ine [IO.Path]::GetFullPath($module)) { throw 'Native runner failed or loaded another module' }
    $report = Get-Content -LiteralPath (Join-Path $temp 'Report/index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $proof.Tests = @($report.tests | Select-Object fullTestPath,state,errors,warnings)
    if ($proof.Tests.Count -lt $MinimumTests -or @($proof.Tests | Where-Object state -ne 'Success').Count) { throw 'Native assertions failed or requested tests missing' }
    if (!(Select-String -LiteralPath (Join-Path $root 'Native.log') -SimpleMatch 'ServiceStatusProducer.v2: bounded-scroll180, shutdown-observers, valid-old-Open fence.' -Quiet)) { throw 'Latest service verification producer was not loaded' }
    foreach ($entry in $proof.Overlay) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash -ne $entry.SHA256) { throw 'Original overlay changed after capture' }
    }
    $backendProof=Join-Path $runtime 'BackendResult.json'
    $proof.Backend=Get-Content -LiteralPath $backendProof -Raw|ConvertFrom-Json
    if(!$proof.Backend.passed){throw 'Backend service integration evidence missing'}
    Copy-Item -LiteralPath $backendProof -Destination (Join-Path $root 'BackendResult.json')
    $proof.Passed = $true
    break
    } catch {
        $attemptFailure = $_.Exception.Message
        if($backendProcess){if(!$backendProcess.HasExited){$backendProcess.Kill()};$null=$backendProcess.WaitForExit(10000);$backendProcess=$null}
        if ($process) {
            if (!$process.HasExited) { $process.Kill() }
            if (!$process.WaitForExit(10000)) { throw 'Owned process has not released repair inputs' }
            $process.Dispose()
            $process = $null
        }
        $proof.AttemptFailures += @{ Attempt=$attempt; Failure=$attemptFailure; Overlay=$proof.Overlay; Tests=$proof.Tests }
        foreach ($name in @('Build.log','BuildConsole.log','Native.log')) {
            $log = Join-Path $root $name
            if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $root ('Attempt'+$attempt+'-'+$name)) }
        }
        if (!$RepairWindowSeconds -or $attempt -ge 5) { throw $attemptFailure }
        $repairDeadline = [DateTime]::UtcNow.AddSeconds($RepairWindowSeconds)
        $proof.RepairUntilUtc = $repairDeadline.ToString('o')
        Write-Proof
        Write-Output ('Attempt '+$attempt+' needs source repair: '+$attemptFailure+'; bounded retention until '+$proof.RepairUntilUtc)
        # Only the owner of this run writes {"Attempt": N} after fixing/reviewing the
        # explicitly reserved overlay. No shared source or command is accepted from the marker.
        $retry = Join-Path $root 'RetryValidation.json'
        while (!(Test-Path -LiteralPath $retry)) {
            if ([DateTime]::UtcNow -ge $repairDeadline) { throw ($attemptFailure+'; repair window expired') }
            Start-Sleep -Milliseconds 500
        }
        $marker = Get-Content -LiteralPath $retry -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($marker.Attempt -ne $attempt) { throw 'Stale retry marker' }
        Remove-Item -LiteralPath $retry
        $nextOverlay = @()
        foreach ($relative in $OverlayPaths) {
            $source = Join-Path $workspace $relative
            $destination = Join-Path $project $relative
            $before = (Get-FileHash -LiteralPath $source).Hash
            if ((Get-FileHash -LiteralPath $destination).Hash -ne $before) {
                Copy-Item -LiteralPath $source -Destination $destination
                [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
                $buildNeeded = $true
            }
            if ((Get-FileHash -LiteralPath $source).Hash -ne $before -or (Get-FileHash -LiteralPath $destination).Hash -ne $before) { throw 'Repaired overlay changed during capture' }
            $nextOverlay += @{ Path=$relative; SHA256=$before }
        }
        $proof.Overlay = $nextOverlay
        $proof.Tests = @()
        $proof.RepairUntilUtc = $null
        $reportPath = Join-Path $temp 'Report'
        if (Test-Path -LiteralPath $reportPath) { $null = Remove-GuardianProbeArtifact $reportPath $tempRoot }
    }
    }
} catch { $failure = $_.Exception.Message }
finally {
    $released = $true
    if($backendProcess){try{if(!$backendProcess.HasExited){$backendProcess.Kill()};if(!$backendProcess.WaitForExit(10000)){throw "Backend process still active"}}catch{$released=$false;$failure=([string]$failure+"; "+$_.Exception.Message)}}
    if ($process) {
        try { if (!$process.HasExited) { $process.Kill() }; if (!$process.WaitForExit(10000)) { throw 'Owned process still running' } }
        catch { $released=$false; $failure=([string]$failure+'; '+$_.Exception.Message) }
    }
    $rows=@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_-match'^\|.*operations-service-ue-20261106'})
    if($rows.Count-ne 1){$released=$false;$failure=([string]$failure+'; reservation missing at cleanup')}
    if ($ownsTemp -and $released -and (Test-Path -LiteralPath $temp)) {
        try { $proof.Cleanup = Remove-GuardianProbeArtifact $temp $tempRoot }
        catch { $failure=([string]$failure+'; cleanup: '+$_.Exception.Message) }
    }
    $proof.Failure = $failure
    $proof.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $proof.Passed = $proof.Passed -and !$failure -and $proof.CleanupComplete
    $proof.HistoricalHashesOnly = $true
    Write-Proof
    Write-Output ('Passed='+$proof.Passed+'; Proof='+ (Join-Path $root 'Result.json'))
}
if (!$proof.Passed) { throw $failure }
