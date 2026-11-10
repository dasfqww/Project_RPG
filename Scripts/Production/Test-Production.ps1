[CmdletBinding()]
param([ValidateSet('contracts','queue')][string]$Group = 'contracts', [switch]$UpdateLock)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$toolRoot = Join-Path $repoRoot 'Tools/Production'
$sdk = Join-Path $repoRoot 'Saved/ProductionToolchain/dotnet-10.0.403/dotnet.exe'
if (!(Test-Path -LiteralPath $sdk)) { throw 'Run Scripts/Production/Install-ProductionSdk.ps1 first.' }
$proofRoot = Join-Path $repoRoot 'Saved/BuildChecks/ProductionFoundation-20261110'
$runRoot = Join-Path $proofRoot ('Temp/' + [Guid]::NewGuid().ToString('N'))
$artifacts = Join-Path $runRoot 'artifacts'
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$passed = $false
$deletedBytes = 0L
$lines = @()
$oldTelemetry = $env:DOTNET_CLI_TELEMETRY_OPTOUT
try {
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    Push-Location $toolRoot
    try {
        $lockMode = if ($UpdateLock) { 'false' } else { 'true' }
        & $sdk build Tests/ProjectRpg.Production.Tests.csproj -c Release --artifacts-path $artifacts "-p:RestoreLockedMode=$lockMode" --nologo
        if ($LASTEXITCODE -ne 0) { throw 'Production tests build failed.' }
        $testDll = Join-Path $artifacts 'bin/ProjectRpg.Production.Tests/release/ProjectRpg.Production.Tests.dll'
        $lines = @(& $sdk $testDll $Group $repoRoot $runRoot)
        $testExit = $LASTEXITCODE
        $lines | Write-Output
        if ($testExit -ne 0) { throw 'Production regression failed.' }
        $passed = $true
    } finally { Pop-Location }
}
finally {
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = $oldTelemetry
    $resolved = [IO.Path]::GetFullPath($runRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $proofRoot 'Temp')) + [IO.Path]::DirectorySeparatorChar
    if (!$resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup path escaped owned root.' }
    $stack = New-Object 'System.Collections.Generic.Stack[string]'
    $stack.Push($resolved)
    while ($stack.Count -gt 0) {
        $directory = $stack.Pop()
        if ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cleanup refused a reparse point.' }
        foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cleanup refused a reparse point.' }
            if ($item.PSIsContainer) { $stack.Push($item.FullName) } else { $deletedBytes += $item.Length }
        }
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
    [ordered]@{ group=$Group; passed=$passed; utc=[DateTime]::UtcNow.ToString('o'); result=$lines; deleted_bytes=$deletedBytes; cleanup_exceptions=0 } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $proofRoot ($Group + '-result.json')) -Encoding UTF8
    Write-Output "Cleaned $deletedBytes bytes of owned temporary output."
}
