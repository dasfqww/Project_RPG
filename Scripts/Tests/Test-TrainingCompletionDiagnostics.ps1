$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'Run-TrainingPlaytest.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Runner parser failed'}
$function=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-TrainingRuntimeDiagnostics'},$true)
Invoke-Expression $function.Extent.Text
$ready='RPG_TRAINING_PLAYTEST COMPLETION_UI_READY Widgets=1 ReturnEnabled=1'
$returned='RPG_TRAINING_PLAYTEST COMPLETION_UI_RETURNED Lobby=1 IdentityPreserved=1'
$connection='LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost, ErrorString = Your connection to the host has been lost., Driver = Name:GameNetDriver Def:GameNetDriver IpNetDriver_0'
$start="LogInit: Error: pre-start fixture`nRPG_TRAINING_PLAYTEST START DevelopmentOnly=1`n"
$normal=$start+$ready+"`n"+$connection+"`n"+$returned
$cases=@(
    @{text=$normal;allow=$true;server=1;raw=1;expected=1;unexpected=0},
    @{text=$normal.Replace("`n","`r`n");allow=$true;server=1;raw=1;expected=1;unexpected=0},
    @{text=$normal;allow=$false;server=1;raw=1;expected=0;unexpected=1},
    @{text=$normal;allow=$true;server=0;raw=1;expected=0;unexpected=1},
    @{text=$normal;allow=$true;server=2;raw=1;expected=0;unexpected=1},
    @{text=$normal.Replace('ConnectionLost','ConnectionTimeout');allow=$true;server=1;raw=1;expected=0;unexpected=1},
    @{text=$start+$connection+"`n"+$ready+"`n"+$returned;allow=$true;server=1;raw=1;expected=0;unexpected=1},
    @{text=$start+$ready+"`n"+$returned+"`n"+$connection;allow=$true;server=1;raw=1;expected=0;unexpected=1},
    @{text=$normal.Replace($returned,'');allow=$true;server=1;raw=1;expected=0;unexpected=1},
    @{text=$start+$ready+"`n"+$connection+"`n"+$connection+"`n"+$returned;allow=$true;server=1;raw=2;expected=0;unexpected=2},
    @{text=$start+$ready+"`n"+$connection+"`nLogOther: Error: unrelated`n"+$returned;allow=$true;server=1;raw=2;expected=1;unexpected=1},
    @{text=$start;allow=$false;server=0;raw=0;expected=0;unexpected=0}
)
foreach($case in $cases){$r=Get-TrainingRuntimeDiagnostics $case.text $case.allow $case.server;if($r.afterProbeStart -ne $case.raw -or $r.plannedReturnConnectionLost -ne $case.expected -or $r.unexpected -ne $case.unexpected){throw ('Diagnostic classification mismatch at case '+[array]::IndexOf($cases,$case))}}
Write-Output ('PASS '+$cases.Count+' planned-return/error boundary cases; no UE or backend started.')
