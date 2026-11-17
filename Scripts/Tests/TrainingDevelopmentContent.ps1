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
        default { throw 'Unknown development content profile; expected Training, Intro1, Intro2 or Intro3' }
    }
    return $maps
}

function Assert-RPGTrainingDevelopmentAllocation {
    param($Allocation,[string]$Profile='Training')
    $maps=Get-RPGTrainingDevelopmentContent $Profile
    if(!$Allocation -or [string]$Allocation.Difficulty -cne 'Normal' -or
       [string]$Allocation.DungeonId -cnotin @($maps.Keys) -or
       [string]$Allocation.MapPackage -cne [string]$maps[[string]$Allocation.DungeonId]) {
        throw 'Allocator content/map/difficulty differs from the selected development profile'
    }
}
