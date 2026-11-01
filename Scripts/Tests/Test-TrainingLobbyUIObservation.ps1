$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TrainingLobbyUIObservation.ps1')
$snapshot=@'
textbox "editor search" [pos=4,4 size=100,20] [ref=tb0]
textbox [pos=40,80 size=300,40] [ref=tb1]
textbox "collapsed" [pos=0,0 size=0,0] [ref=tb2]
button "Login" [pos=40,130 size=300,40] [ref=b1]
button "Login" disabled [pos=40,180 size=300,40] [ref=b2]
textbox "later" [pos=40,230 size=300,40] [ref=tb3]
'@
$nodes=@(Get-TrainingLobbyNodes $snapshot)
if($nodes.Count -ne 5 -or @($nodes | Where-Object ref -eq 'tb2').Count){throw 'Collapsed geometry was accepted'}
$button=Find-TrainingLobbyButton $nodes 'Login'
if(!$button -or $button.ref -cne 'b1'){throw 'Disabled action was accepted'}
$field=Find-TrainingLobbyField $nodes $button 'textbox'
if($field.ref -cne 'tb1'){throw 'Field discovery escaped its preceding control context'}
$disabledSnapshot=$snapshot.Replace('textbox [pos=40,80','textbox disabled [pos=40,80')
if(Find-TrainingLobbyField @(Get-TrainingLobbyNodes $disabledSnapshot) $button 'textbox'){throw 'Disabled field caused fallback to an unrelated editor field'}
$rejected=$false
try{$null=Find-TrainingLobbyButton $nodes 'Login' -AllowDisabled}catch{$rejected=$true}
if(!$rejected){throw 'Ambiguous action was not rejected'}
foreach($case in @(@{json='[]';count=0},@{json='[{"characterId":"one"}]';count=1},@{json='[{"characterId":"one"},{"characterId":"two"}]';count=2})){
    $roster=@(Expand-TrainingLobbyRoster ($case.json | ConvertFrom-Json))
    if($roster.Count -ne $case.count){throw 'JSON roster cardinality changed across the PowerShell pipeline'}
}
$launcher=Join-Path $PSScriptRoot '../Start-TrainingDevelopment.ps1'
$rejected=$false
try{& $launcher -SmokeTest -LobbyUISmoke}catch{if($_.Exception.Message -match 'Choose gameplay smoke or lobby UI smoke'){$rejected=$true}else{throw}}
if(!$rejected){throw 'Conflicting smoke modes were not rejected before startup'}
Write-Output 'PASS: geometry/disabled filtering, contextual fields, duplicate action rejection, JSON roster cardinality, mode preflight. No UE or HTTP launched.'
