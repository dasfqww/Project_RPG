[CmdletBinding()]
param([string]$CapturedSnapshotPath='', [string]$CapturedVitalsPath='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'PlayerVitalsPIEObservation.ps1')
$sample=[pscustomobject]@{controls=@{health=@{text='200/200'};mana=@{text='100/100'}}}
$valid=@'
window "Editor" [pos=0,0 size=1920,1080] [ref=w1]
  text "200/200" [pos=0,0 size=0,0] [ref=editorText]
window "Game Preview [NetMode: Standalone 0]" [pos=100,100 size=1200,800] [ref=w2]
  progressbar [pos=200,600 size=390,30] [ref=pb1]
  text "200/200" [pos=300,598 size=114,36] [ref=x1]
  progressbar [pos=700,600 size=390,30] [ref=pb2]
  text "100/100" [pos=800,598 size=114,36] [ref=x2]
'@
$cases=[Collections.Generic.List[object]]::new()
function Case([string]$Name,[string]$Snapshot,$State,[bool]$Expected){
    $accepted=$false;$reason=$null
    try{$layout=Get-PlayerVitalsSlateLayout $Snapshot $State;$accepted=$true}catch{$reason=$_.Exception.Message}
    if($accepted -ne $Expected){throw ('Unexpected Slate contract result: '+$Name+' '+$reason)}
    $cases.Add(@{name=$Name;accepted=$accepted;reason=$reason})
}
Case 'ValidPairAndUnrelatedEditorLabel' $valid $sample $true
Case 'ZeroBarWidth' ($valid.Replace('size=390,30','size=0,30')) $sample $false
Case 'ZeroLabelHeight' ($valid.Replace('size=114,36','size=114,0')) $sample $false
Case 'MissingResourceLabel' ($valid.Replace('text "100/100"','text "unknown"')) $sample $false
Case 'DuplicateResourceLabel' ($valid+"`n  text `"100/100`" [pos=800,598 size=114,36] [ref=x3]") $sample $false
Case 'EditorOnly' (($valid -split '\r?\n')[0..1] -join "`n") $sample $false
Case 'MultiplePIEWindows' ($valid+"`nwindow `"Other [NetMode: Client 1]`" [pos=0,0 size=800,600] [ref=w3]") $sample $false
Case 'ClippedResource' ($valid.Replace('pos=700,600','pos=1200,600')) $sample $false
Case 'LabelOverAnotherArea' ($valid.Replace('pos=800,598','pos=400,598')) $sample $false
Case 'SharedBarRef' ($valid.Replace('[ref=pb2]','[ref=pb1]')) $sample $false
Case 'InvalidRectNumber' ($valid.Replace('size=390,30','size=NaN,30')) $sample $false
$same=[pscustomobject]@{controls=@{health=@{text='100/100'};mana=@{text='100/100'}}}
$equal=$valid.Replace('200/200','100/100')
Case 'EqualValuesUseDistinctBars' $equal $same $true
Case 'EqualValuesMissingSecondLabel' ($equal.Replace('text "100/100" [pos=800,598','text "unknown" [pos=800,598')) $same $false
if($CapturedSnapshotPath -or $CapturedVitalsPath){
    if(!$CapturedSnapshotPath -or !$CapturedVitalsPath){throw 'Both recorded snapshot and Vitals are required'}
    $record=Get-Content -Raw -Encoding UTF8 -LiteralPath $CapturedVitalsPath | ConvertFrom-Json
    Case 'CapturedNativeSlateWithZeroPythonGeometry' (Get-Content -Raw -Encoding UTF8 -LiteralPath $CapturedSnapshotPath) $record.samples[-1] $true
}
$cases | ConvertTo-Json -Depth 5
Write-Output ('PASS Slate contract cases='+$cases.Count+'; temporary artifacts=0')
