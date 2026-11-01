Set-StrictMode -Version Latest

function Remove-RPGTrainingGuideTemporaryTree {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$OwnedParent)
    $workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\','/')
    $parent=[IO.Path]::GetFullPath($OwnedParent).TrimEnd('\','/')
    $target=[IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    $relative=$parent.Substring([Math]::Min($workspace.Length+1,$parent.Length)).Replace('\','/')
    if (!$parent.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -or
        $relative -notmatch '^Saved/(TrainingDevelopment/[a-f0-9]{32}|Tests/(TrainingGuideAutomation/[a-f0-9]{32}|TrainingPlaytest/[a-f0-9]{32}|TP/[a-f0-9]{12}|TrainingChargeRelease)|BuildChecks/(TrainingLobbyUI-20261031|TrainingDevelopmentLauncher-20261031|TrainingPlaytest-20261030|TrainingChargeRelease-20261030|TrainingGuideViewport-20261030|TrainingGuideHUD-20261029|ProgressionTrainingReward-20261029|ProgressionOnlyReward-20261029|ContributionHUD(Live)?-20261029|LobbySkillPreview-20261028|LobbySkillPreviewLive-20261029)|Maintenance/TrainingGuideCleanup-20261030)(/|$)' -or
        !$target.StartsWith($parent+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Refusing cleanup outside the owned artifact directory'}
    # Even an allowed spelling must not traverse a junction to the original workspace.
    $ancestor=Split-Path -Parent $target
    while ($ancestor -and $ancestor.Length -ge $workspace.Length) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Refusing cleanup through a reparse ancestor'}
        $ancestor=Split-Path -Parent $ancestor
    }
    if (!(Test-Path -LiteralPath $target)) {return [pscustomobject]@{path=$target;deletedBytes=0L;deletedFiles=0;unlinked=0;absent=$true}}
    $roadmap=Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md'
    $relativeTarget=$target.Substring($workspace.Length+1).Replace('\','/')
    foreach($line in (Get-Content -LiteralPath $roadmap -Encoding UTF8)) {
        if ($line -notmatch '^\| ' -or $line -notmatch '\uC9C4\uD589 \uC911|\uC608\uC57D \uC911|\uAC80\uC99D \uC911' -or $line -match 'training-guide-viewport-20261030|training-charge-release-20261030|training-playtest-20261030|training-development-launcher-20261031|training-lobby-ui-20261031') {continue}
        if ($line.Replace('\','/').Contains($relativeTarget)) {throw 'Temporary tree is referenced by another active reservation'}
        if ($relativeTarget -like 'Saved/BuildChecks/TrainingGuideHUD-20261029/RuntimeProject*' -and $line -match 'progression-training-content-20261029') {throw 'Training content owner still protects the baseline DLL'}
    }
    $nodes=[Collections.Generic.List[object]]::new()
    $pending=[Collections.Generic.Stack[string]]::new(); $pending.Push($target)
    while ($pending.Count) {
        $item=Get-Item -LiteralPath $pending.Pop() -Force
        $link=!!($item.Attributes -band [IO.FileAttributes]::ReparsePoint)
        $nodes.Add([pscustomobject]@{Path=$item.FullName;Directory=$item.PSIsContainer;Link=$link;Length=if(!$item.PSIsContainer -and !$link){$item.Length}else{0L}})
        if ($item.PSIsContainer -and !$link) {foreach($child in Get-ChildItem -LiteralPath $item.FullName -Force) {$pending.Push($child.FullName)}}
    }
    # Preflight all regular files before deleting anything; never terminate a reader.
    foreach($node in $nodes) {
        if (!$node.Directory -and !$node.Link) {$stream=[IO.File]::Open($node.Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None); $stream.Dispose()}
    }
    $bytes=0L; $files=0; $links=0
    foreach($node in ($nodes | Sort-Object {$_.Path.Length} -Descending)) {
        $item=Get-Item -LiteralPath $node.Path -Force
        if (!!($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne $node.Link) {throw 'Cleanup node changed after preflight'}
        if ($node.Directory) {[IO.Directory]::Delete($node.Path,$false)}
        else {Remove-Item -LiteralPath $node.Path -Force}
        $bytes+=$node.Length
        if ($node.Link) {$links++} elseif (!$node.Directory) {$files++}
    }
    [pscustomobject]@{path=$target;deletedBytes=$bytes;deletedFiles=$files;unlinked=$links;absent=!(Test-Path -LiteralPath $target)}
}
