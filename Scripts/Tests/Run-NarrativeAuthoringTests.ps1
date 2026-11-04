[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=Join-Path $workspace ('Saved/BuildChecks/NarrativeAuthoring-20261105/'+[Guid]::NewGuid().ToString('N'))
$temp=Join-Path $run 'Temp';$inputs=Join-Path $temp 'Backend';$artifacts=Join-Path $temp 'artifacts'
$result=[ordered]@{Passed=$false;Tests=$false;FileHttp=$false;OfflineValidation=$false;InvalidStartup=$false;InputsUnchanged=$false;Cleanup=$null;TempDeleted=$false;Failure=$null}
$copies=[Collections.Generic.List[object]]::new();$hostProcess=$null;$hostOut=$null;$hostErr=$null
$null=New-Item -ItemType Directory -Path $inputs -Force
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Reservation {
    if(@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match '^\|[^|]*narrative-authoring-20261105'}).Count -ne 1){throw 'Own reservation missing'}
}
function Start-Child([string]$Arguments,[string]$CatalogPath='',[string]$WorkingDirectory=(Join-Path $inputs 'ProjectRpg.Backend')){
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=(Get-Command dotnet).Source
    $start.Arguments=$Arguments;$start.WorkingDirectory=$WorkingDirectory
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden'
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($key in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','STEAM_WEB_API_PUBLISHER_KEY','STEAM_APP_ID','STEAM_TICKET_IDENTITY','PROJECT_RPG_NARRATIVE_TEST_POSTGRES')){$start.EnvironmentVariables.Remove($key)}
    $start.EnvironmentVariables['ASPNETCORE_ENVIRONMENT']='Development';$start.EnvironmentVariables['DOTNET_ENVIRONMENT']='Development'
    $start.EnvironmentVariables['Storage__Provider']='Memory';$start.EnvironmentVariables['Auth__AdminToken']=''
    $start.EnvironmentVariables['Steam__AllowDevelopmentAuthentication']='true'
    $start.EnvironmentVariables['Narrative__EnableDevelopmentContent']='false';$start.EnvironmentVariables['Narrative__CatalogPath']=$CatalogPath
    $start.EnvironmentVariables['Kestrel__Endpoints__Http__Url']=$url
    $child=[Diagnostics.Process]::new();$child.StartInfo=$start
    if(!$child.Start()){throw 'Own dotnet child did not start'}
    return $child
}
function Run-Child([string]$Arguments,[string]$Log,[string]$CatalogPath='', [switch]$ExpectFailure){
    $child=Start-Child $Arguments $CatalogPath
    try{
        $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
        if(!$child.WaitForExit(180000)){$child.Kill();$child.WaitForExit();throw ('Own child timed out: '+$Log)}
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $run $Log),$text)
        if(($child.ExitCode -ne 0) -ne [bool]$ExpectFailure){throw ('Unexpected child exit; inspect '+$Log)}
        return $text
    }finally{if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
function Stop-Host {
    if(!$script:hostProcess){return}
    if(!$script:hostProcess.HasExited){$script:hostProcess.Kill();$script:hostProcess.WaitForExit()}
    [IO.File]::WriteAllText((Join-Path $run 'Host.log'),$script:hostOut.GetAwaiter().GetResult()+$script:hostErr.GetAwaiter().GetResult())
    $script:hostProcess.Dispose();$script:hostProcess=$null
}
function Api([string]$Method,[string]$Path,$Body=$null){
    $parameters=@{Uri=($url+'/api'+$Path);Method=$Method;TimeoutSec=15}
    if($headers){$parameters.Headers=$headers}
    if($null -ne $Body){$parameters.ContentType='application/json';$parameters.Body=($Body|ConvertTo-Json -Depth 6 -Compress)}
    Invoke-RestMethod @parameters
}
try{
    Reservation
    foreach($directory in @('ProjectRpg.Backend','ProjectRpg.Backend.NarrativeTests','Database')){
        $source=Join-Path $workspace ('Backend/'+$directory)
        foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object {$_.FullName -notmatch '[\\/](bin|obj)[\\/]' -and $_.Extension -in '.cs','.csproj','.json','.sql'}){
            if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked input refused'}
            $before=(Get-FileHash -LiteralPath $file.FullName).Hash
            $target=Join-Path $inputs ($directory+'/'+$file.FullName.Substring($source.Length+1))
            $null=New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
            Copy-Item -LiteralPath $file.FullName -Destination $target
            if((Get-FileHash -LiteralPath $file.FullName).Hash -cne $before -or (Get-FileHash -LiteralPath $target).Hash -cne $before){throw 'Mixed input copy'}
            $copies.Add(@{Path=$file.FullName;SHA256=$before})
        }
    }
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start()
    $url='http://127.0.0.1:'+$listener.LocalEndpoint.Port;$listener.Stop()
    $null=Run-Child ('build "'+(Join-Path $inputs 'ProjectRpg.Backend.NarrativeTests/ProjectRpg.Backend.NarrativeTests.csproj')+'" --artifacts-path "'+$artifacts+'" -v minimal') 'Build.log'
    $tests=Join-Path $artifacts 'bin/ProjectRpg.Backend.NarrativeTests/debug/ProjectRpg.Backend.NarrativeTests.dll'
    $testOutput=Run-Child ('"'+$tests+'"') 'Tests.log'
    if(!$testOutput.Contains('PASS Narrative authoring:') -or !$testOutput.Contains('PASS Narrative: all requested groups completed.')){throw 'Required test groups missing'}
    $result.Tests=$true;Write-Output 'PASS authoring definitions, Memory and authenticated HTTP regression'
    $dll=Join-Path $artifacts 'bin/ProjectRpg.Backend/debug/ProjectRpg.Backend.dll'
    $catalog=Join-Path $temp 'authored.json'
    $authored=([IO.File]::ReadAllText((Join-Path $inputs 'ProjectRpg.Backend/Narrative/Content/development.json'))).Replace('DEV.','FILE.').Replace('"catalogVersion": 1','"catalogVersion": 7')
    [IO.File]::WriteAllText($catalog,$authored,[Text.UTF8Encoding]::new($false))
    $validated=Run-Child ('"'+$dll+'" --validate-narrative-catalog --Storage:Provider=Postgres --ConnectionStrings:Postgres="Host=127.0.0.1;Port=1;Timeout=1"') 'OfflineValidation.log' $catalog
    if(!$validated.Contains('Narrative catalog valid: version 7, quests 1, chapters 1, dialogues 4. No host or database started.')){throw 'Offline validation did not finish before database access'}
    $modeConflict=Run-Child ('"'+$dll+'" --validate-narrative-catalog --migrate-schema') 'ModeConflict.log' $catalog -ExpectFailure
    if(!$modeConflict.Contains('Choose one startup mode')){throw 'Conflicting startup modes accepted'}
    $result.OfflineValidation=$true
    $invalid=Join-Path $temp 'invalid.json';[IO.File]::WriteAllText($invalid,'{"schemaVersion":99,"catalogVersion":1,"quests":[],"chapters":[],"dialogues":[]}')
    $failure=Run-Child ('"'+$dll+'" --migrate-schema') 'InvalidStartup.log' $invalid -ExpectFailure
    if(!$failure.Contains('unsupported_narrative_catalog_schema') -or $failure.Contains('migrations ready')){throw 'Invalid content not rejected before migration'}
    $missing=Run-Child ('"'+$dll+'"') 'MissingStartup.log' (Join-Path $temp 'missing.json') -ExpectFailure
    if(!$missing.Contains('narrative_catalog_file_unavailable')){throw 'Missing content silently accepted'}
    $result.InvalidStartup=$true
    $hostProcess=Start-Child ('"'+$dll+'"') $catalog
    $hostOut=$hostProcess.StandardOutput.ReadToEndAsync();$hostErr=$hostProcess.StandardError.ReadToEndAsync()
    $ready=$false
    for($i=0;$i -lt 60;$i++){
        if($hostProcess.HasExited){throw 'Own authored host exited'}
        try{$null=Invoke-RestMethod -Uri ($url+'/health/ready') -TimeoutSec 2;$ready=$true;break}catch{}
        Start-Sleep -Milliseconds 250
    }
    if(!$ready){throw 'Own host readiness timeout'}
    $headers=$null;$login=Api POST '/auth/steam-ticket' @{ticket='dev:76561198000000001'}
    $headers=@{Authorization=('Bearer '+$login.accessToken)};$login=$null
    $character=Api POST '/characters' @{name=('File'+[Guid]::NewGuid().ToString('N').Substring(0,10))}
    $path='/characters/'+$character.characterId+'/narrative'
    $initial=Api GET $path
    if($initial.catalogVersion -ne 7 -or $initial.availableDialogues[0].id -cne 'FILE.Contact.Offer'){throw 'Host did not load configured external content'}
    $nodes=@('Contact.Offer','Contact.Explain','Instructor.Explain','Contact.Report');$choices=@('accept','confirm','confirm','report')
    for($i=0;$i -lt 4;$i++){
        $done=Api POST ($path+'/choices') @{requestId=[Guid]::NewGuid().ToString();expectedRevision=([string]$i);catalogVersion=7;dialogueId=('FILE.'+$nodes[$i]);choiceId=$choices[$i]}
    }
    if($done.revision -cne '4' -or $done.completedChapters -notcontains 'FILE.Prologue' -or $done.storyUnlocks -notcontains 'FILE.Story.Next'){throw 'File-authored HTTP flow failed'}
    $result.FileHttp=$true;Write-Output 'PASS file-authored catalog v7 real host/HTTP accept -> objectives -> report; invalid startup rejected'
    foreach($entry in $copies){if((Get-FileHash -LiteralPath $entry.Path).Hash -cne $entry.SHA256){throw 'Original input changed during validation'}}
    $result.InputsUnchanged=$true;$result.Passed=$true
}catch{$result.Failure=$_.Exception.Message;throw}
finally{
    try{Stop-Host}catch{$result.Failure=([string]$result.Failure+'; host cleanup: '+$_.Exception.Message)}
    try{Reservation;$result.Cleanup=Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $run;$result.TempDeleted=!(Test-Path -LiteralPath $temp)}catch{$result.Failure=([string]$result.Failure+'; file cleanup: '+$_.Exception.Message)}
    if($result.Failure -or !$result.TempDeleted){$result.Passed=$false}
    $copies|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'Inputs.json') -Encoding UTF8
    $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding UTF8
    Write-Output ('Result: '+(Join-Path $run 'Result.json'))
    if(!$result.Passed){throw ('Authoring verification failed: '+$result.Failure)}
}
