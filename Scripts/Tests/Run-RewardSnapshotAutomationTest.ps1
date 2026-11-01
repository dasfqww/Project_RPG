[CmdletBinding()]
param(
    [string] $EngineRoot = 'D:\UE_5.8',
    [ValidateRange(30, 600)][int] $TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
if (-not (Test-Path -LiteralPath $editorPath)) { throw "Missing editor: $editorPath" }
$runRoot = Join-Path $projectRoot ('Saved\Tests\RewardSnapshot\Automation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$process = $null
try {
    $arguments = @(
        ('"' + $projectPath + '"'), '/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.Economy.RewardSnapshot"',
        '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-DisablePython',
        '-DDC-ForceMemoryCache', ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Write-Host "Started reward-snapshot automation (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Automation timed out; inspect $logPath" }
        Start-Sleep -Milliseconds 250
    }
    if ($process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode); inspect $logPath" }
    foreach ($name in @('Codec', 'Generation', 'WalletAndNotice')) {
        $marker = "Test Completed. Result={Success} Name={$name} Path={ProjectRPG.Economy.RewardSnapshot.$name}"
        if (-not (Select-String -LiteralPath $logPath -SimpleMatch $marker -Quiet)) { throw "Missing success: $name; inspect $logPath" }
    }
    if (Select-String -LiteralPath $logPath -Pattern 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet) {
        throw "Automation/runtime failure; inspect $logPath"
    }
    Write-Host "PASS: all three reward-snapshot automation tests. Log: $logPath"
}
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
