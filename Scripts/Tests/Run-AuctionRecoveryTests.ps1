[CmdletBinding()]
param([string]$EngineRoot = 'D:/UE_5.8')
# Recovery's coordinator now uses the shared owned-asset projection codec and inventory plugin.
# Keep this entrypoint while the maintained runner executes all original recovery/client cases too.
& (Join-Path $PSScriptRoot 'Run-AuctionAssetsTests.ps1') -EngineRoot $EngineRoot