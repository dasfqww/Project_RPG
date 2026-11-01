$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../Network/DungeonPartyHudObservation.ps1')
$script:checks=0
function Check([bool]$Value,[string]$Name) {
    if (-not $Value) { throw "HUD parser check failed: $Name" }
    $script:checks++
}
$healthy=@'
text "파티 전투 정보 2명 · 서버 조회 기준"
text "[나] Alpha · 생존\n체력 75 / 100"
progressbar [ref=pb1]
text "Beta · 생존\n체력 1,000 / 1,200"
progressbar [ref=pb2]
text "접속·탈퇴 상태는 서버 확인 기준"
'@
$normal=Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha'
Check ($null -ne $normal -and $normal.rows.Count -eq 2) 'two real row labels and visible bars'
Check ($normal.rows[0].health -eq 75 -and $normal.rows[1].maxHealth -eq 1200) 'localized health numbers'
$grace=$healthy.Replace('Beta · 생존\n체력 1,000 / 1,200','Beta\n재접속 대기 · 약 09:55').Replace('progressbar [ref=pb2]','')
$waiting=Get-PartyHudSlateEvidence $grace @('Alpha','Beta') 'Alpha' 'Beta'
Check ($null -ne $waiting -and $waiting.rows[1].reconnectGrace -and -not $waiting.rows[1].healthBarVisible) 'grace retains member without stale health'
Check ($null -eq (Get-PartyHudSlateEvidence ($grace.Replace('text "접속·탈퇴','progressbar [ref=pb2]' + "`n" + 'text "접속·탈퇴')) @('Alpha','Beta') 'Alpha' 'Beta')) 'stale grace bar rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy.Replace('2명','1명')) @('Alpha','Beta') 'Alpha')) 'wrong header count rejected'
Check ($null -eq (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Beta')) 'wrong self marker rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy.Replace('75 / 100','101 / 100')) @('Alpha','Beta') 'Alpha')) 'out-of-range health rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy.Replace('progressbar [ref=pb1]','')) @('Alpha','Beta') 'Alpha')) 'missing visible health bar rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy + "`n" + 'text "Beta · 생존\n체력 1,000 / 1,200"') @('Alpha','Beta') 'Alpha')) 'duplicate or stale viewport row rejected'
$noticeOnly=$healthy.Replace('text "Beta · 생존\n체력 1,000 / 1,200"','text "Beta: 서버에서 재접속 확인"')
Check ($null -eq (Get-PartyHudSlateEvidence $noticeOnly @('Alpha','Beta') 'Alpha')) 'notice is not member-row evidence'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy.Replace('Alpha · 생존','Alpha · 상태 확인 중')) @('Alpha','Beta') 'Alpha')) 'unknown is not healthy'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy.Replace('75 / 100','0 / 100')) @('Alpha','Beta') 'Alpha')) 'zero health is not initial living state'
Check ($null -ne (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha')) 'reconnected healthy roster accepted'
$expected=@{Alpha=75;Beta=1000}
Check ($null -ne (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha' -ExpectedHealth $expected)) 'health agrees with authoritative sample'
Check ($null -eq (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha' -ExpectedHealth @{Alpha=74})) 'stale health label rejected'
Check ($null -ne (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha' -ExpectedHealth @{Alpha=75.04})) 'one-decimal display rounding accepted'
Check ($null -eq (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha' -ExpectedHealth @{Alpha=[double]::NaN})) 'non-finite expected health rejected'
Check ($null -eq (Get-PartyHudSlateEvidence $healthy @('Alpha','Beta') 'Alpha' -ExpectedHealth @{Alpha=-1})) 'negative expected health rejected'
$dead=$healthy.Replace('Alpha · 생존\n체력 75 / 100','Alpha · 전투 불능\n체력 0 / 100')
$defeated=Get-PartyHudSlateEvidence $dead @('Alpha','Beta') 'Alpha' -IncapacitatedName 'Alpha' -ExpectedHealth @{Alpha=0;Beta=1000}
Check ($null -ne $defeated -and $defeated.rows[0].incapacitated -and $defeated.rows[0].health -eq 0 -and
    -not $defeated.rows[1].incapacitated) 'one defeated member with surviving peer'
Check ($null -eq (Get-PartyHudSlateEvidence $dead @('Alpha','Beta') 'Alpha')) 'death is not accepted in the initial living phase'
Check ($null -eq (Get-PartyHudSlateEvidence ($dead.Replace('0 / 100','1 / 100')) @('Alpha','Beta') 'Alpha' -IncapacitatedName 'Alpha')) 'positive stale health rejected for defeated phase'
Check ($null -eq (Get-PartyHudSlateEvidence ($dead.Replace('전투 불능','생존')) @('Alpha','Beta') 'Alpha' -IncapacitatedName 'Alpha')) 'zero alone does not prove the defeated label'
Check ($null -eq (Get-PartyHudSlateEvidence ($dead.Replace('progressbar [ref=pb1]','')) @('Alpha','Beta') 'Alpha' -IncapacitatedName 'Alpha')) 'defeated bar must remain rendered at zero'
$otherView=$dead.Replace('[나] Alpha','Alpha').Replace('"Beta ·','"[나] Beta ·')
Check ($null -ne (Get-PartyHudSlateEvidence $otherView @('Alpha','Beta') 'Beta' -IncapacitatedName 'Alpha')) 'remote peer sees the same defeat with its own self marker'
Check ($null -eq (Get-PartyHudSlateEvidence $dead @('Alpha','Beta') 'Alpha' -IncapacitatedName 'Alpha' -ExpectedHealth @{Beta=999})) 'surviving peer health changes rejected'
$rejected=$false
try { $null=Get-PartyHudSlateEvidence $dead @('Alpha','Beta') 'Alpha' -ReconnectingName 'Alpha' -IncapacitatedName 'Alpha' } catch { $rejected=$true }
Check $rejected 'grace and defeat expectations cannot describe the same member'
$removed=$healthy.Replace('2명','1명').Replace('text "Beta · 생존\n체력 1,000 / 1,200"' + "`n" + 'progressbar [ref=pb2]',
    'text "Beta: 서버에서 탈퇴 확인"')
$single=Get-PartyHudSlateEvidence $removed @('Alpha') 'Alpha' -RemovedNames @('Beta')
Check ($null -ne $single -and $single.headerCount -eq 1 -and $single.rows.Count -eq 1 -and
    $single.removedNames[0] -ceq 'Beta') 'confirmed departure retains self and permits a transient notice'
Check ($null -eq (Get-PartyHudSlateEvidence ($healthy.Replace('2명','1명')) @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'stale departed health row rejected'
Check ($null -eq (Get-PartyHudSlateEvidence $grace.Replace('2명','1명') @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'stale departed grace row rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($removed.Replace('text "Beta:','progressbar [ref=stale]' + "`n" + 'text "Beta:')) @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'orphan departed health bar rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($removed.Replace('1명','2명')) @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'stale party total rejected'
Check ($null -eq (Get-PartyHudSlateEvidence ($removed.Replace('Beta: 서버에서 탈퇴 확인','Beta · 상태 확인 중')) @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'unknown departed row is not a removal'
Check ($null -ne (Get-PartyHudSlateEvidence ($removed + "`n" + 'progressbar [ref=player-hud]') @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'unrelated player health bar remains outside party panel'
Check ($null -eq (Get-PartyHudSlateEvidence ($removed + "`n" + $removed) @('Alpha') 'Alpha' -RemovedNames @('Beta'))) 'duplicate party viewport rejected'
$rejected=$false
try { $null=Get-PartyHudSlateEvidence $removed @('Alpha') 'Alpha' -RemovedNames @('Alpha') } catch { $rejected=$true }
Check $rejected 'cannot remove the retained self'
[pscustomobject]@{passed=$true;parserChecks=$script:checks;actualGameRun=$false} | ConvertTo-Json -Compress
