[CmdletBinding()]
param([string]$ProjectFile=(Join-Path $PSScriptRoot '../../Project_RPG.uproject'),[string]$EngineRoot='D:/UE_5.8',[switch]$All,[switch]$ChargeReleaseOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TrainingGuideArtifacts.ps1')
if ($All -and $ChargeReleaseOnly) {throw 'All and ChargeReleaseOnly cannot be combined'}
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$sourceRoot=Split-Path -Parent ([IO.Path]::GetFullPath($ProjectFile))
$runRoot=Join-Path $workspace ('Saved/Tests/TrainingGuideAutomation/'+[Guid]::NewGuid().ToString('N'))
$temp=Join-Path $runRoot 'Temp'
$runtime=Join-Path $temp 'RuntimeProject'
$tests=@('ProjectRPG.Progression.TrainingGuide.NativeViewportLifecycle')
if ($All) {$tests+=@('ProjectRPG.Progression.TrainingGuide.ProgressAndCompletion','ProjectRPG.Progression.TrainingGuide.PawnAndWidgetLifetime','ProjectRPG.Progression.TrainingGuide.CurrentInputAndSlots')}
if ($ChargeReleaseOnly) {$tests=@('ProjectRPG.Progression.TrainingCharge.DefaultReleaseAndReset','ProjectRPG.Progression.TrainingCharge.CancellationReuseAndValidation')}
$process=$null; $exitCode=$null; $loaded=$null; $failure=$null; $results=@(); $cleanup=$null; $moduleHash=$null; $beforeErrors=$null; $afterErrors=$null
$logPath=Join-Path $runRoot 'Native.log'
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
try {
    New-Item -ItemType Directory -Path (Join-Path $runtime 'Binaries/Win64') -Force | Out-Null
    Copy-Item -LiteralPath $ProjectFile -Destination (Join-Path $runtime 'Project_RPG.uproject')
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'Config') -Destination $runtime -Recurse
    foreach($directory in @('Content','Plugins')) {New-Item -ItemType Junction -Path (Join-Path $runtime $directory) -Target (Join-Path $sourceRoot $directory) | Out-Null}
    $sourceModule=Join-Path $sourceRoot 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
    $moduleHash=(Get-FileHash -LiteralPath $sourceModule).Hash
    Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'Binaries/Win64') -File | Where-Object {$_.Extension -eq '.dll' -or $_.Name -eq 'UnrealEditor.modules'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $runtime 'Binaries/Win64')}
    $module=Join-Path $runtime 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
    if ((Get-FileHash -LiteralPath $module).Hash -ne $moduleHash -or (Get-FileHash -LiteralPath $sourceModule).Hash -ne $moduleHash) {throw 'Module changed during snapshot'}
    $arguments=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',
        ('-ExecCmds="Automation RunTests '+($tests -join '+')+'"'),'-TestExit="Automation Test Queue Empty"',
        '-unattended','-nullrhi','-nosound','-nosteam','-nop4','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-DDC=(EnginePak,Local)',
        ('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+$logPath+'"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
    $arguments | Set-Content -LiteralPath (Join-Path $runRoot 'Command.txt') -Encoding UTF8
    $process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $processHandle=$process.Handle
    $deadline=[DateTime]::UtcNow.AddMinutes(4)
    while (!$process.HasExited) {
        if (!$loaded) {
            try {$candidate=@((Get-Process -Id $process.Id).Modules | Where-Object {$_.ModuleName -eq 'UnrealEditor-Project_RPG.dll'}); if($candidate.Count -eq 1){$loaded=[pscustomobject]@{path=$candidate[0].FileName;sha256=(Get-FileHash -LiteralPath $candidate[0].FileName).Hash}}} catch {}
        }
        if ([DateTime]::UtcNow -ge $deadline) {throw 'Owned native process timeout'}
        $process.WaitForExit(1000) | Out-Null
    }
    $process.WaitForExit(); $exitCode=$process.ExitCode
    if ($exitCode -ne 0 -or $null -eq $exitCode) {throw "Native exit=$exitCode"}
    if (!$loaded -or [IO.Path]::GetFullPath($loaded.path) -ne [IO.Path]::GetFullPath($module) -or $loaded.sha256 -ne $moduleHash) {throw 'Expected snapshot module was not observed'}
    $log=Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $matches=[regex]::Matches($log,'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>[^}]+)\}')
    $results=@($matches | ForEach-Object {[pscustomobject]@{path=$_.Groups['path'].Value;result=$_.Groups['result'].Value}})
    foreach($test in $tests) {if(@($results | Where-Object {$_.path -ceq $test -and $_.result -ceq 'Success'}).Count -ne 1){throw ('Test not successful: '+$test)}}
    if ($results.Count -ne $tests.Count) {throw 'Unexpected test count'}
    $discovery=[regex]::Match($log,'(?m)^.*Found '+$tests.Count+' automation tests.*$')
    if (!$discovery.Success) {throw 'Missing automation discovery'}
    $beforeErrors=[regex]::Matches($log.Substring(0,$discovery.Index),'(?m)^.*: Error:.*$').Count
    $afterErrors=[regex]::Matches($log.Substring($discovery.Index),'(?m)^.*: Error:.*$').Count
    if($afterErrors -or $log -match 'Fatal error:|Assertion failed:|Ensure condition failed:'){throw 'Native runtime errors'}
} catch {$failure=$_.Exception.Message}
finally {
    if ($process -and !$process.HasExited) {Stop-Process -Id $process.Id; $process.WaitForExit()}
    try {$cleanup=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $runRoot} catch {$failure=([string]$failure+'; cleanup: '+$_.Exception.Message).Trim(';',' ')}
    [pscustomobject]@{passed=($null -eq $failure);failure=$failure;tests=$results;nativeExit=$exitCode;loadedModule=$loaded;moduleSHA256=$moduleHash;preDiscoveryErrors=$beforeErrors;postDiscoveryErrors=$afterErrors;cleanup=$cleanup;historicalHashesOnly=$true;temporaryRuntimeDeleted=($cleanup -and $cleanup.absent);gpu=$false;hardwareInput=$false;serverGoalApproval=$false;iris=$false;steam=$false} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
}
Write-Output ('Evidence='+$runRoot)
if ($failure) {throw $failure}
Write-Output ('PASS: '+$tests.Count+' native tests; temporary runtime automatically deleted; bytes='+$cleanup.deletedBytes)
