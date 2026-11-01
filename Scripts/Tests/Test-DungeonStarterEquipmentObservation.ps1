$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'Scripts/Network/Run-DungeonBossReturnE2ETest.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Production runner does not parse' }
$fn = @($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-DungeonFirstSliceStarterEquipment'}, $false))
if ($fn.Count -ne 1) { throw 'Expected one actual runner observation helper' }
. ([ScriptBlock]::Create($fn[0].Extent.Text))
$character = '10203040-5060-7080-90a0-b0c0d0e0f001'
function New-Fixture {
    $names = @('GameItem.Equipment.Progression.Starter.GreatSword','GameItem.Equipment.Progression.Starter.Chest','GameItem.Equipment.Progression.Starter.Gloves')
    $slots = @(9,1,4)
    @(for ($index=0; $index -lt 3; ++$index) {
        [pscustomobject]@{ definitionType='RPGItemDefinition'; definitionName=$names[$index]; definitionVersion=1
            owner=[pscustomobject]@{type='Character';ownerId=$character}
            location=[pscustomobject]@{containerType='Equipment';containerId=$character;slotIndex=$slots[$index]}
            state=[pscustomobject]@{instanceId=[Guid]::NewGuid().ToString('D');quantity=1;equipmentProgression=[pscustomobject]@{growthBandId='FirstSlice';ruleVersion=1;level=0}}
            revision='1';lifecycleState='Active';metadata=[pscustomobject]@{bindState='CharacterBound';creationSource='CharacterCreation';isLocked=$false;expiresAtUtc=$null}
        }
    })
}
$cases=0
$valid=New-Fixture
Assert-DungeonFirstSliceStarterEquipment $valid $character
Assert-DungeonFirstSliceStarterEquipment @($valid[2],$valid[0],$valid[1]) $character
$cases+=2
function Reject([scriptblock]$Change) {
    $items=New-Fixture
    $items=@(& $Change $items)
    $rejected=$false
    try { Assert-DungeonFirstSliceStarterEquipment $items $character } catch { $rejected=$true }
    if (!$rejected) { throw 'Malformed new-character equipment was accepted' }
    $script:cases++
}
Reject { param($x) @($x[0]) }
Reject { param($x) $x[0].definitionName='GameItem.Equipment.Weapon.GreatSword';$x }
Reject { param($x) $x[1].definitionName=$x[1].definitionName.ToLowerInvariant();$x }
Reject { param($x) $x[1].state.instanceId=$x[0].state.instanceId;$x }
Reject { param($x) $x[1].state.instanceId='not-a-guid';$x }
Reject { param($x) $x[1].location.slotIndex=9;$x }
Reject { param($x) $x[1].location.containerType='Inventory';$x }
Reject { param($x) $x[1].state.equipmentProgression=$null;$x }
Reject { param($x) $x[1].state.equipmentProgression.level=1;$x }
Reject { param($x) $x[1].state.equipmentProgression.level=$false;$x }
Reject { param($x) $x[1].state.equipmentProgression.growthBandId='firstslice';$x }
Reject { param($x) $x[1].state.equipmentProgression.ruleVersion=2;$x }
Reject { param($x) $x[1].definitionVersion=2;$x }
Reject { param($x) $x[1].state.quantity=2;$x }
Reject { param($x) $x[1].state.quantity=$true;$x }
Reject { param($x) $x[1].revision='2';$x }
Reject { param($x) $x[1].owner.ownerId=[Guid]::NewGuid().ToString('D');$x }
Reject { param($x) $x[1].location.containerId=[Guid]::NewGuid().ToString('D');$x }
Reject { param($x) $x[1].metadata.bindState='Unbound';$x }
Reject { param($x) $x[1].metadata.isLocked=$true;$x }
Reject { param($x) $x[1].metadata.isLocked='False';$x }
Reject { param($x) $x[1].metadata.expiresAtUtc='2026-01-01T00:00:00Z';$x }
Reject { param($x) $x[1].metadata.creationSource='DungeonReward';$x }
[pscustomobject]@{passed=$true;cases=$cases;gameLaunched=$false;temporaryBytesCreated=0}|ConvertTo-Json -Compress
