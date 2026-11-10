[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('init','status','events','ready','enqueue','approve','get','attempts','sweep','cancel','pause','resume')][string]$Command,
    [string]$Database,
    [string]$TaskFile,
    [string]$TaskId,
    [string]$Reason
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$sdk = Join-Path $repoRoot 'Saved/ProductionToolchain/dotnet-10.0.403/dotnet.exe'
if (!(Test-Path -LiteralPath $sdk)) { throw 'Run Install-ProductionSdk.ps1 first.' }
if (!$Database) { $Database = Join-Path $repoRoot 'Saved/Production/state.db' }
$Database = [IO.Path]::GetFullPath($Database)
$toolRoot = Join-Path $repoRoot 'Tools/Production'
$artifacts = Join-Path $repoRoot 'Saved/Production/ToolBuild'
$arguments = @($Database, (Join-Path $repoRoot 'Production/Policies/local.json'), $Command)
switch ($Command) {
    'enqueue' { if (!$TaskFile) { throw 'TaskFile is required.' }; $arguments += [IO.Path]::GetFullPath($TaskFile) }
    { $_ -in 'approve','get','attempts' } { if (!$TaskId) { throw 'TaskId is required.' }; $arguments += $TaskId }
    'cancel' { if (!$TaskId -or !$Reason) { throw 'TaskId and Reason are required.' }; $arguments += @($TaskId, $Reason) }
}
Push-Location $toolRoot
try {
    & $sdk build Coordinator.Cli/ProjectRpg.Production.Coordinator.Cli.csproj -c Release --artifacts-path $artifacts -p:RestoreLockedMode=true --nologo | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Coordinator build failed.' }
    & $sdk (Join-Path $artifacts 'bin/ProjectRpg.Production.Coordinator.Cli/release/ProjectRpg.Production.Coordinator.Cli.dll') @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Coordinator command failed.' }
} finally { Pop-Location }
