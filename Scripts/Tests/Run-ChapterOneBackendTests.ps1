[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N')
$root=Join-Path $workspace ('Saved/M03/'+$id)
$proof=Join-Path $workspace ('Saved/BuildChecks/ChapterM03-20261115/'+$id)
$result=[ordered]@{Passed=$false;Inputs=@();DeletedBytes=0;Failure=$null}
$files=@('NarrativeTypes.cs','QuestManager.cs','NarrativeCatalog.cs','DungeonObjectiveCoordinator.cs','Content/chapter01.json') | ForEach-Object {'Backend/ProjectRpg.Backend/Narrative/'+$_}
$files+=@('Backend/ProjectRpg.Backend.ChapterOneTests/Program.cs','Backend/ProjectRpg.Backend.ChapterOneTests/ProjectRpg.Backend.ChapterOneTests.csproj')
try {
    if(!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Pattern '^\|[^|]* / chapter-m03-20261115' -Encoding UTF8)){throw 'Reservation missing'}
    if(Test-Path -LiteralPath $root){throw 'Temporary root exists'}
    $null=[IO.Directory]::CreateDirectory($root);$null=[IO.Directory]::CreateDirectory($proof)
    $result.Head=(git -C $workspace rev-parse HEAD).Trim()
    git -C $workspace archive --format=tar ('--output='+$root+'/base.tar') $result.Head Backend
    if($LASTEXITCODE){throw 'Archive failed'}
    tar -xf "$root/base.tar" -C $root
    if($LASTEXITCODE){throw 'Extraction failed'}
    foreach($f in $files){
        $src=Join-Path $workspace $f;$dst=Join-Path $root $f
        $hash=(Get-FileHash -LiteralPath $src).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path $dst));Copy-Item -LiteralPath $src -Destination $dst
        if((Get-FileHash -LiteralPath $dst).Hash -ne $hash){throw 'Capture changed'}
        $result.Inputs+=@{Path=$f;SHA256=$hash}
    }
    foreach($name in @('ChapterOneTests','NarrativeTests','NarrativeInteractionTests')){
        dotnet build "$root/Backend/ProjectRpg.Backend.$name" -c Release --artifacts-path "$root/artifacts" --nologo *> "$proof/$name-build.log"
        if($LASTEXITCODE){throw "Build failed: $name"}
        $args=@("$root/artifacts/bin/ProjectRpg.Backend.$name/release/ProjectRpg.Backend.$name.dll")
        if($name -eq 'ChapterOneTests'){$args+="$root/Backend/ProjectRpg.Backend/Narrative/Content/chapter01.json"}
        dotnet @args *> "$proof/$name.log"
        if($LASTEXITCODE){throw "Test failed: $name"}
        Write-Output "PASS $name"
    }
    foreach($f in $result.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $f.Path)).Hash -ne $f.SHA256){throw 'Source changed after validation'}}
    $result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally {
    if(Test-Path -LiteralPath $root){
        $resolved=[IO.Path]::GetFullPath($root)
        $parent=[IO.Path]::GetFullPath((Join-Path $workspace 'Saved/M03'))+[IO.Path]::DirectorySeparatorChar
        if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe cleanup root'}
        $entries=@(Get-ChildItem -LiteralPath $root -Recurse -Force)
        if(@($entries | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'Unexpected link in own tree'}
        $result.DeletedBytes=($entries | Where-Object {!$_.PSIsContainer}|Measure-Object Length -Sum).Sum
        Remove-Item -LiteralPath $root -Recurse -Force
    }
    $null=[IO.Directory]::CreateDirectory($proof)
    $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$proof/Result.json" -Encoding UTF8
}
if(!$result.Passed){throw $result.Failure}
