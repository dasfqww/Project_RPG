[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$version = '10.0.403'
$sdkRoot = Join-Path $repoRoot "Saved/ProductionToolchain/dotnet-$version"
$exe = Join-Path $sdkRoot 'dotnet.exe'
if (Test-Path -LiteralPath $exe) {
    $installed = & $exe --version
    if ($LASTEXITCODE -ne 0 -or $installed -ne $version) { throw 'Unexpected installed SDK version.' }
    Write-Output $exe
    return
}
if (Test-Path -LiteralPath $sdkRoot) { throw 'Partial SDK directory exists; inspect it before reinstalling.' }
$downloadRoot = Join-Path $repoRoot ('Saved/ProductionToolchain/download-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $downloadRoot | Out-Null
$archive = Join-Path $downloadRoot 'sdk.zip'
try {
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -UseBasicParsing -Uri "https://builds.dotnet.microsoft.com/dotnet/Sdk/$version/dotnet-sdk-$version-win-x64.zip" -OutFile $archive
    $expected = '7a114c318fa7f1d9822991b534b2814d0e78386ceda816b371f5141acefe741d87ccb606ede06cadac8f9ac5070e32b708a67bbd1619ff5e5a7858a27874ef48'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $expected) { throw 'SDK archive hash mismatch.' }
    New-Item -ItemType Directory -Path $sdkRoot | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $sdkRoot)
    & $exe --version
    if ($LASTEXITCODE -ne 0) { throw 'SDK verification failed; partial installation retained for inspection.' }
    Write-Output $exe
}
finally {
    # Only this invocation's archive and empty directory. No recursive SDK deletion.
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    [IO.Directory]::Delete($downloadRoot, $false)
}
