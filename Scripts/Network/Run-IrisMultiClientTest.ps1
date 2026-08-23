[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$ClientCount = 2,

    [ValidateRange(1024, 65535)]
    [int]$Port = 17777,

    [ValidateRange(10, 600)]
    [int]$TimeoutSeconds = 90,

    [string]$EngineRoot = "D:\UE_5.8",

    [string]$Map = "/Game/Maps/testmap",

    [string]$GameMode = "/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C",

    [switch]$SkipBuild,

    [switch]$VisibleClients,

    [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"

function Test-LogMarker {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Marker
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    return [bool](Select-String `
        -LiteralPath $Path `
        -SimpleMatch $Marker `
        -Quiet `
        -ErrorAction SilentlyContinue)
}

function Get-LogTail {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return "Log file was not created: $Path"
    }

    return (Get-Content -LiteralPath $Path -Tail 40 -ErrorAction SilentlyContinue) -join [Environment]::NewLine
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$projectPath = Join-Path $projectRoot "Project_RPG.uproject"
$editorPath = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$buildPath = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Project file was not found: $projectPath"
}
if (-not (Test-Path -LiteralPath $editorPath -PathType Leaf)) {
    throw "UnrealEditor.exe was not found. Pass -EngineRoot for the UE 5.8 installation."
}

if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath $buildPath -PathType Leaf)) {
        throw "Build.bat was not found: $buildPath"
    }

    Write-Host "Building Project_RPGEditor (Development Win64)..."
    & $buildPath `
        Project_RPGEditor `
        Win64 `
        Development `
        "-Project=$projectPath" `
        -WaitMutex `
        -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) {
        throw "Project_RPGEditor build failed with exit code $LASTEXITCODE."
    }

    Write-Host "Building Project_RPGServer (Development Win64)..."
    & $buildPath `
        Project_RPGServer `
        Win64 `
        Development `
        "-Project=$projectPath" `
        -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        throw "Project_RPGServer build failed with exit code $LASTEXITCODE."
    }
}

$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logRoot = Join-Path $projectRoot "Saved\Logs\IrisMultiClient\$runStamp"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

$serverLog = Join-Path $logRoot "Server.log"
$serverUrl = "$Map`?game=$GameMode"
$serverArguments = @(
    $projectPath,
    $serverUrl,
    "-server",
    "-port=$Port",
    "-log",
    "-abslog=$serverLog",
    "-unattended",
    "-nosplash",
    "-NoSound",
    "-nullrhi",
    "-multiprocess",
    "-nosteam",
    "-NoLiveCoding",
    "-UseIrisReplication=1",
    "-RPGNetTestMode",
    "-RPGRequireIris",
    "-RPGExpectedClients=$ClientCount"
)

$spawnedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$clientLogs = [System.Collections.Generic.List[string]]::new()
$succeeded = $false

try {
    Write-Host "Starting Iris dedicated server on 127.0.0.1:$Port..."
    $serverProcess = Start-Process `
        -FilePath $editorPath `
        -ArgumentList $serverArguments `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden `
        -PassThru
    $spawnedProcesses.Add($serverProcess)

    $serverDeadline = (Get-Date).AddSeconds([Math]::Min(45, $TimeoutSeconds))
    while ((Get-Date) -lt $serverDeadline) {
        if ($serverProcess.HasExited) {
            throw "Server exited before accepting clients.`n$(Get-LogTail -Path $serverLog)"
        }
        if (Test-LogMarker -Path $serverLog -Marker "RPG_NETTEST SERVER_STATE Iris=1") {
            break
        }
        if (Test-LogMarker -Path $serverLog -Marker "RPG_NETTEST IRIS_REQUIRED_BUT_INACTIVE") {
            throw "The GameNetDriver started without Iris.`n$(Get-LogTail -Path $serverLog)"
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not (Test-LogMarker -Path $serverLog -Marker "RPG_NETTEST SERVER_STATE Iris=1")) {
        throw "Server did not report an active Iris GameNetDriver.`n$(Get-LogTail -Path $serverLog)"
    }

    for ($index = 1; $index -le $ClientCount; ++$index) {
        $clientLog = Join-Path $logRoot "Client-$index.log"
        $clientLogs.Add($clientLog)
        $clientUrl = "127.0.0.1:$Port`?RPGNetTest=1"
        $clientArguments = @(
            $projectPath,
            $clientUrl,
            "-game",
            "-log",
            "-abslog=$clientLog",
            "-unattended",
            "-nosplash",
            "-NoSound",
            "-multiprocess",
            "-nosteam",
            "-NoLiveCoding",
            "-UseIrisReplication=1",
            "-RPGExpectedClients=$ClientCount",
            "-ResX=960",
            "-ResY=540",
            "-WinX=$((($index - 1) % 2) * 980)",
            "-WinY=$([Math]::Floor(($index - 1) / 2) * 580)",
            "-windowed"
        )
        if (-not $VisibleClients) {
            $clientArguments += "-nullrhi"
        }

        $windowStyle = if ($VisibleClients) { "Normal" } else { "Hidden" }
        Write-Host "Starting client $index of $ClientCount..."
        $clientProcess = Start-Process `
            -FilePath $editorPath `
            -ArgumentList $clientArguments `
            -WorkingDirectory $projectRoot `
            -WindowStyle $windowStyle `
            -PassThru
        $spawnedProcesses.Add($clientProcess)
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        foreach ($process in $spawnedProcesses) {
            if ($process.HasExited) {
                throw "A network test process exited before readiness. Logs: $logRoot"
            }
        }

        $serverReady = Test-LogMarker `
            -Path $serverLog `
            -Marker "RPG_NETTEST SERVER_READY"
        $readyClientCount = 0
        foreach ($clientLog in $clientLogs) {
            if (Test-LogMarker -Path $clientLog -Marker "RPG_NETTEST CLIENT_READY") {
                ++$readyClientCount
            }
        }

        if ($serverReady -and $readyClientCount -eq $ClientCount) {
            $succeeded = $true
            break
        }

        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)

    if (-not $succeeded) {
        throw "Timed out waiting for $ClientCount Iris clients. Server log tail:`n$(Get-LogTail -Path $serverLog)"
    }

    Write-Host "PASS: Iris server and $ClientCount clients observed the replicated ready state."
    Write-Host "Logs: $logRoot"
    if ($KeepRunning) {
        Write-Host "Processes were left running because -KeepRunning was supplied."
    }
}
finally {
    if (-not $KeepRunning -or -not $succeeded) {
        foreach ($process in $spawnedProcesses) {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
