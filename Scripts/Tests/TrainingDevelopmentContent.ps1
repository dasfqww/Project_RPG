# Closed operator plan shared by launch and retirement. It grants no admission.
function Get-RPGTrainingDevelopmentContent {
    param([string]$Profile='Training')
    $maps=[ordered]@{'Progression.Training'='/Game/Maps/Progression/L_ProgressionTraining'}
    switch -CaseSensitive ($Profile) {
        'Training' { }
        'Intro1' { $maps['Progression.Intro1']='/Game/Maps/Progression/L_ProgressionIntro1' }
        'Intro2' {
            $maps['Progression.Intro1']='/Game/Maps/Progression/L_ProgressionIntro1'
            $maps['Progression.Intro2']='/Game/Maps/Progression/L_ProgressionIntro2'
        }
        'Intro3' {
            $maps['Progression.Intro1']='/Game/Maps/Progression/L_ProgressionIntro1'
            $maps['Progression.Intro2']='/Game/Maps/Progression/L_ProgressionIntro2'
            $maps['Progression.Intro3']='/Game/Maps/Progression/L_ProgressionIntro3'
        }
        'ChapterOne' {
            $maps['Progression.Intro1']='/Game/Maps/Progression/L_ProgressionIntro1'
            $maps['Progression.Intro2']='/Game/Maps/Progression/L_ProgressionIntro2'
            $maps['Progression.Intro3']='/Game/Maps/Progression/L_ProgressionIntro3'
            $maps['Progression.FirstBoss']='/Game/Maps/Progression/L_ChapterOneBoss'
            $maps['Story.ChapterOneReturn']='/Game/Maps/Progression/L_ChapterOneReturn'
        }
        default { throw 'Unknown development content profile; expected Training, Intro1, Intro2, Intro3 or ChapterOne' }
    }
    return $maps
}

function Assert-RPGTrainingDevelopmentAllocation {
    param($Allocation,[string]$Profile='Training')
    $maps=Get-RPGTrainingDevelopmentContent $Profile
    $supportedDifficulty=$Allocation -and ([string]$Allocation.Difficulty -ceq 'Normal' -or
        ($Profile -ceq 'ChapterOne' -and [string]$Allocation.DungeonId -ceq 'Progression.FirstBoss' -and [string]$Allocation.Difficulty -ceq 'Hard'))
    if(!$supportedDifficulty -or
       [string]$Allocation.DungeonId -cnotin @($maps.Keys) -or
       [string]$Allocation.MapPackage -cne [string]$maps[[string]$Allocation.DungeonId]) {
        throw 'Allocator content/map/difficulty differs from the selected development profile'
    }
}
