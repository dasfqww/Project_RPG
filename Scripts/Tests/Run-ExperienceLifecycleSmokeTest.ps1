# Runs isolated game processes; never starts/stops the user's editor or PIE.
[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(8, 60)][int]$RunSeconds = 15,
    [ValidateRange(60, 600)][int]$TimeoutSeconds = 120,
    [switch]$CreateFixturesOnly
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$projectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$fixtureScript = Join-Path $projectRoot 'Scripts\Editor\CreateExperienceSandbox.py'
$mode = '/Game/System/Experiences/Examples/BP_ExperienceSandboxGameMode.BP_ExperienceSandboxGameMode_C'
$mapRoot = '/Game/System/Experiences/Examples/Maps'
$presentation = '/Game/System/Experiences/DA_ClientPresentation.DA_ClientPresentation'
$composed = '/Game/System/Experiences/DA_ClientPresentationAndOptionsHint.DA_ClientPresentationAndOptionsHint'
$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$logRoot = Join-Path $projectRoot ("Saved\Logs\ExperienceLifecycle-" + $runId)
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
foreach ($required in @($projectPath,$editorPath,$fixtureScript)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}

function Start-IsolatedRun([string[]]$Arguments, [string]$Name) {
    # Every generated argument is quoted; embedded quotes are not accepted.
    foreach ($argument in $Arguments) {
        if ($argument.Contains('"')) { throw "Unexpected embedded quote in argument: $argument" }
    }
    $quoted = @($Arguments | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process -FilePath $editorPath -ArgumentList $quoted -PassThru -WindowStyle Hidden
    Write-Host ("Started {0}, PID {1}" -f $Name, $process.Id)
    try {
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            throw "$Name exceeded $TimeoutSeconds seconds; inspect $logRoot"
        }
        $process.Refresh()
        return $process.ExitCode
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

$hashRoots = @(
    'Content\System\Experiences',
    'Plugins\GameFeatures\RPGClientPresentation\Content',
    'Plugins\GameFeatures\RPGOptionsUI\Content',
    'Plugins\GameFeatures\RPGInputMapping\Content',
    'Plugins\GameFeatures\RPGClientUI\Content'
)
$before = @{}
foreach ($root in $hashRoots) {
    Get-ChildItem -LiteralPath (Join-Path $projectRoot $root) -Recurse -File |
        Where-Object { $_.Extension -in @('.uasset','.umap') } |
        ForEach-Object { $before[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
}
$results = [System.Collections.Generic.List[object]]::new()
$fatalFailure = $null
$fixtureLog = Join-Path $logRoot 'Fixtures.log'
try {
    $fixtureExit = Start-IsolatedRun @(
        $projectPath, '-run=PythonScript', ("-Script=" + $fixtureScript),
        '-unattended', '-nop4', '-nosplash', '-NullRHI', '-nosound',
        '-nosteam', '-NoLiveCoding', '-multiprocess', ("-abslog=" + $fixtureLog)
    ) 'Fixtures'
    $fixtureContent = Get-Content -LiteralPath $fixtureLog -Raw -Encoding UTF8
    if ($fixtureExit -ne 0 -or $fixtureContent -notmatch 'RPG_EXPERIENCE_SANDBOX ASSETS_READY') {
        throw "Fixture generation failed (exit $fixtureExit); inspect $fixtureLog"
    }
    if (-not $CreateFixturesOnly) {
        $cases = @(
            @{Name='MapSelection'; Map="$mapRoot/L_ExperienceLoad"; Extra=@(); Expected=$composed; FeatureCount=4; Actions=4; Selection='Load'},
            @{Name='URLPrecedence'; Map="$mapRoot/L_ExperienceLoad?RPGExperience=RPGExperienceDefinition:DA_ClientPresentation"; Extra=@('-RPGExperience=RPGExperienceDefinition:DA_ClientPresentationAndOptionsHint'); Expected=$presentation; FeatureCount=1; Actions=1; Selection=$null},
            @{Name='GlobalDefaultSelection'; Map='/Engine/Maps/Entry'; Extra=@("-ini:Game:[/Script/Project_RPG.RPGExperienceSettings]:DefaultExperience=$presentation"); Expected=$presentation; FeatureCount=1; Actions=1; Selection=$null},
            @{Name='DisabledSelection'; Map="$mapRoot/L_ExperienceDisabled"; Extra=@("-ini:Game:[/Script/Project_RPG.RPGExperienceSettings]:DefaultExperience=$presentation"); Expected=$null; FeatureCount=0; Actions=0; Selection='Disabled'},
            @{Name='ExplicitOverride'; Map="$mapRoot/L_ExperienceDisabled"; Extra=@('-RPGExperience=RPGExperienceDefinition:DA_ClientPresentation'); Expected=$presentation; FeatureCount=1; Actions=1; Selection=$null},
            @{Name='DuplicateSelection'; Map="$mapRoot/L_ExperienceDuplicate"; Extra=@("-ini:Game:[/Script/Project_RPG.RPGExperienceSettings]:DefaultExperience=$presentation"); Expected=$null; FeatureCount=0; Actions=0; Failure='Multiple enabled Experience selection actors'},
            @{Name='MissingDefinition'; Map="$mapRoot/L_ExperienceLoad"; Extra=@('-RPGExperience=/Game/System/Experiences/DA_DoesNotExist.DA_DoesNotExist'); Expected=$null; FeatureCount=0; Actions=0; Failure='Selected asset is not an RPG experience'}
        )
        foreach ($case in $cases) {
            $logPath = Join-Path $logRoot ($case.Name + '.log')
            $checks = [System.Collections.Generic.List[string]]::new()
            try {
                $url = $case.Map + "?game=$mode"
                $arguments = @(
                    $projectPath, $url, '-game', '-unattended', '-nop4', '-nosplash',
                    '-NullRHI', '-nosound', '-nosteam', '-NoLiveCoding', '-DisablePython',
                    '-multiprocess', ("-seconds=" + $RunSeconds), ("-abslog=" + $logPath),
                    '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
                    '-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient',
                    '-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer'
                ) + $case.Extra
                $exitCode = Start-IsolatedRun $arguments $case.Name
                $content = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
                if ($exitCode -ne 0) { throw "Game process exited with code $exitCode" }
                if ($content -match 'Fatal error:|Assertion failed:|Ensure condition failed:') { throw 'Runtime fatal/assert/ensure detected' }
                $ready = [regex]::Matches($content, 'RPG_EXPERIENCE READY World=(?<world>\S+) Definition=(?<definition>\S+) NetMode=\d+ Session=(?<session>[A-Fa-f0-9-]+)')
                $failed = [regex]::Matches($content, 'RPG_EXPERIENCE FAILED [^\r\n]*')
                $activated = [regex]::Matches($content, "Game feature '(?<plugin>RPGClientPresentation|RPGOptionsUI|RPGInputMapping|RPGClientUI)' transitioned successfully\. Ending state: Active")
                if ($case.Selection) {
                    if ($content -notmatch ("RPG_EXPERIENCE MAP_SELECTION [^\r\n]*Mode=" + $case.Selection)) { throw 'Map selection marker missing' }
                    $checks.Add('map-selection')
                }
                if ($case.Expected) {
                    if ($ready.Count -ne 1 -or $ready[0].Groups['definition'].Value -ne $case.Expected) { throw 'Ready definition differs from expected selection' }
                    if ($failed.Count -ne 0) { throw 'Unexpected Experience failure' }
                    if ($activated.Count -ne $case.FeatureCount) { throw 'Unexpected number of activated GameFeatures' }
                    $checks.Add('definition-and-features')
                    $session = $ready[0].Groups['session'].Value
                    $lifecycle = @(
                        "RPG_EXPERIENCE END Session=$session ",
                        "RPG_EXPERIENCE ACTIONS_RELEASED Session=$session [^\r\n]*Actions=$($case.Actions) ",
                        "RPG_EXPERIENCE OWNERSHIP_RELEASED Session=$session "
                    )
                    $position = $ready[0].Index
                    foreach ($marker in $lifecycle) {
                        $match = [regex]::Match($content, $marker)
                        if (-not $match.Success -or $match.Index -le $position) { throw "Missing/out-of-order lifecycle marker: $marker" }
                        $position = $match.Index
                    }
                    if ($content -notmatch 'RPG_PRESENTATION APPLIED' -or $content -notmatch 'RPG_PRESENTATION RELEASED [^\r\n]*RemainingVotes=0') { throw 'Presentation ownership did not apply/release' }
                    if ($case.FeatureCount -eq 4) {
                        if ($content -notmatch 'RPG_INPUT APPLIED' -or $content -notmatch 'RPG_INPUT RELEASED [^\r\n]*Remaining=0') { throw 'Input mapping ownership did not apply/release' }
                        if ($content -notmatch 'RPG_CLIENT_UI APPLIED [^\r\n]*ID=OptionsShortcutHint' -or $content -notmatch 'RPG_CLIENT_UI RELEASED [^\r\n]*ID=OptionsShortcutHint') { throw 'Client widget ownership did not apply/release' }
                    }
                    $checks.Add('world-teardown-and-ownership')
                } else {
                    if ($ready.Count -ne 0 -or $activated.Count -ne 0) { throw 'Unexpected readiness/feature activation after disabled/invalid selection' }
                    if ($case.Failure) {
                        if ($failed.Count -ne 1 -or $failed[0].Value -notmatch [regex]::Escape($case.Failure)) { throw 'Expected selection/load failure missing' }
                        $checks.Add('expected-failure')
                    } elseif ($failed.Count -ne 0) { throw 'Disabled selection must stay unassigned without a failure' }
                    else { $checks.Add('disabled-keeps-unassigned') }
                }
                $unrelatedErrors = @([regex]::Matches($content, '(?m)^.*\bError:.*$') |
                    ForEach-Object { $_.Value } | Where-Object {
                        $_ -notmatch 'LogRPGExperience: Error: RPG_EXPERIENCE FAILED' -and
                        -not ($case.Name -eq 'MissingDefinition' -and $_ -match 'DA_DoesNotExist')
                    })
                $results.Add([PSCustomObject]@{Name=$case.Name;Passed=$true;Checks=@($checks);Log=$logPath;OtherErrorLines=$unrelatedErrors})
                Write-Host ("PASS {0}: {1}" -f $case.Name, ($checks -join ', '))
            } catch {
                $results.Add([PSCustomObject]@{Name=$case.Name;Passed=$false;Checks=@($checks);Log=$logPath;Error=$_.Exception.Message})
                Write-Host ("FAIL {0}: {1}" -f $case.Name, $_.Exception.Message)
            }
        }
    }
} catch {
    $fatalFailure = $_.Exception.Message
} finally {
    $changed = @($before.Keys | Where-Object {
        -not (Test-Path -LiteralPath $_ -PathType Leaf) -or
        (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash -ne $before[$_]
    })
    $report = [PSCustomObject]@{
        RunId=$runId;FixturesLog=$fixtureLog;CreateFixturesOnly=[bool]$CreateFixturesOnly
        FatalFailure=$fatalFailure;ExistingAssetCount=$before.Count;ChangedExistingAssets=$changed
        Cases=@($results.ToArray());Note='NullRHI lifecycle checks; F10 input, visual UI, async waiter and multiplayer are not covered.'
    }
    $reportPath = Join-Path $logRoot 'Report.json'
    [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    Write-Host ("Report: " + $reportPath)
}
if ($fatalFailure) { throw $fatalFailure }
if ($changed.Count -gt 0) { throw 'Existing module assets changed; inspect Report.json' }
if (@($results | Where-Object { -not $_.Passed }).Count -gt 0) { throw 'Experience lifecycle checks failed; inspect Report.json' }
Write-Host 'PASS Experience lifecycle smoke run'
