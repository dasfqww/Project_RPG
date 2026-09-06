[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$ClientCount = 2,

    [ValidateRange(1024, 65535)]
    [int]$Port = 17777,

    [ValidateRange(10, 600)]
    [int]$TimeoutSeconds = 90,

    [ValidateRange(10, 240)]
    [int]$MaxFPS = 60,

    [string]$EngineRoot = "D:\UE_5.8",

    [string]$Map = "/Game/Maps/testmap",

    [string]$GameMode = "/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C",

    [ValidateSet("Editor", "Game")]
    [string]$Runtime = "Editor",

    [switch]$SkipBuild,

    [switch]$BuildServerTarget,

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
$gamePath = Join-Path $projectRoot "Binaries\Win64\Project_RPG.exe"
$cookedRoot = Join-Path $projectRoot "Saved\Cooked\Windows"

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Project file was not found: $projectPath"
}
if (-not (Test-Path -LiteralPath $buildPath -PathType Leaf)) {
    throw "Build.bat was not found. Pass -EngineRoot for the UE 5.8 installation."
}

if (-not $SkipBuild) {
	$runtimeTarget = if ($Runtime -eq "Editor") {
		"Project_RPGEditor"
	} else {
		"Project_RPG"
	}
    Write-Host "Building $runtimeTarget (Development Win64)..."
    & $buildPath `
        $runtimeTarget `
        Win64 `
        Development `
        "-Project=$projectPath" `
        -WaitMutex `
        -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) {
		if ($Runtime -eq "Editor") {
			throw "Project_RPGEditor build failed. Close every Unreal Editor process using this project, then retry. Exit code: $LASTEXITCODE."
		}
		throw "Project_RPG build failed with exit code $LASTEXITCODE."
    }

	if ($BuildServerTarget) {
		Write-Host "Building Project_RPGServer (Development Win64)..."
		& $buildPath `
			Project_RPGServer `
			Win64 `
			Development `
			"-Project=$projectPath" `
			-WaitMutex
		if ($LASTEXITCODE -ne 0) {
			throw "Project_RPGServer requires a source-built engine distribution. Exit code: $LASTEXITCODE."
		}
    }
}

$runtimePath = if ($Runtime -eq "Editor") { $editorPath } else { $gamePath }
if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
	throw "Runtime executable was not found: $runtimePath"
}
if (($Runtime -eq "Game") -and
	-not (Test-Path -LiteralPath (Join-Path $cookedRoot "Project_RPG\AssetRegistry.bin") -PathType Leaf)) {
	throw "Game runtime needs Windows cooked data under $cookedRoot. Use -Runtime Editor for the uncooked development test."
}

$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logRoot = Join-Path $projectRoot "Saved\Logs\IrisMultiClient\$runStamp"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

$serverLog = Join-Path $logRoot "Server.log"
$serverUrl = "$Map`?listen?game=$GameMode"
$serverArguments = @()
if ($Runtime -eq "Editor") {
	$serverArguments += $projectPath
} else {
	$serverArguments += "-project=$projectPath"
	$serverArguments += "-basedir=$cookedRoot"
}
$serverArguments += @(
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
	"-ExecCmds=`"t.MaxFPS $MaxFPS`"",
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
        -FilePath $runtimePath `
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
        $clientArguments = @()
		if ($Runtime -eq "Editor") {
			$clientArguments += $projectPath
		} else {
			$clientArguments += "-project=$projectPath"
			$clientArguments += "-basedir=$cookedRoot"
		}
		$clientArguments += @(
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
			"-ExecCmds=`"t.MaxFPS $MaxFPS`"",
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
            -FilePath $runtimePath `
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
