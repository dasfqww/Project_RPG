[CmdletBinding()]
param(
    [string] $EngineRoot = 'D:\UE_5.8',
    [ValidateRange(30, 600)][int] $TimeoutSeconds = 180,
    [switch] $IncludeIndependentWindows
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
if (-not (Test-Path -LiteralPath $editorPath)) { throw "Missing editor: $editorPath" }
if (-not (Test-Path -LiteralPath $modulePath)) {
    throw 'Build Project_RPGEditor Win64 Development before running this test.'
}

$runRoot = Join-Path $projectRoot ('Saved\Tests\InventoryWindowDrag\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$testScope = if ($IncludeIndependentWindows) { 'ProjectRPG.Item.UI.' } else { 'ProjectRPG.Item.UI.WindowDrag' }
$requiredPaths = @('ProjectRPG.Item.UI.WindowDrag.LeftButtonTitleOnly',
    'ProjectRPG.Item.UI.WindowDrag.ExistingWindowsKeepLeftButton',
    'ProjectRPG.Item.UI.WindowDrag.TitleBarStaysInBounds')
if ($IncludeIndependentWindows) {
    $requiredPaths += @('ProjectRPG.Item.UI.IndependentWindows.BagToggleAndTopClose',
        'ProjectRPG.Item.UI.IndependentWindows.StackAndInput')
}
$process = $null
try {
    $arguments = @(
        ('"' + $projectPath + '"'), '/Engine/Maps/Entry',
        ('-ExecCmds="Automation RunTests ' + $testScope + '"'),
        '-TestExit="Automation Test Queue Empty"',
        ('-ReportExportPath="' + $runRoot + '"'),
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi',
        '-nosteam', '-NoLiveCoding', '-DisablePython',
        '-DDC=(EnginePak,Local)',
        ('-LocalDataCachePath="' + (Join-Path $runRoot 'Cache') + '"'),
        '-stdout', '-FullStdOutLogOutput', ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runRoot 'stdout.log') `
        -RedirectStandardError (Join-Path $runRoot 'stderr.log')
    # Keep the process handle before exit so Windows PowerShell can read ExitCode.
    $processHandle = $process.Handle
    Write-Host "Started inventory drag automation (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Automation timed out; inspect $logPath" }
        Start-Sleep -Milliseconds 250
    }
    $process.WaitForExit()
    if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0) {
        throw "Editor exited with code $($process.ExitCode); inspect $runRoot"
    }

    # TestExit alone can report zero even if assertions fail or no tests run.
    $log = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    foreach ($testPath in $requiredPaths) {
        $testName = ($testPath -split '\.')[-1]
        $marker = "Test Completed. Result={Success} Name={$testName} Path={$testPath}"
        if (-not $log.Contains($marker)) { throw "Missing successful test: $testName; inspect $logPath" }
    }
    if ($log -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:') {
        throw "Automation/runtime failure; inspect $logPath"
    }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) {
        throw 'Editor module changed during inventory automation; rerun required.'
    }
    Write-Host "PASS: $($requiredPaths.Count)/$($requiredPaths.Count) item window tests; editor exit=0. Module SHA256: $moduleHash"
    Write-Host "Report: $runRoot (native Slate input/geometry only; real mouse and GPU visuals are not covered)."
}
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
