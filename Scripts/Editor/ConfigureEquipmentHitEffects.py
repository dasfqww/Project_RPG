"""Connect data-authored first-slice hit effects. Read-only unless explicitly applied.

Run with -RPGEquipmentHitEffectsApply to save only the two items and two ability sets.
RPGEquipmentHitEffectsOutput selects a small JSON result; no item grants or gameplay probes.
"""
import json
import re
from pathlib import Path
import unreal

ROOT = "/Game/Blueprints/GameData/Item/Progression"
SPECS = (
    ("EnduranceGreatSword", "Shield", "Player.Ability.Skill.Charge", unreal.RPGEquipmentShieldOnHitAbility,
     unreal.RPGItemShieldOnHitDefinitionFragment, {"b_require_maximum_charge": True, "b_require_finisher": False, "maximum_health_fraction": 0.05, "duration_seconds": 3.0}),
    ("CirculationGreatSword", "Resolve", "Player.Ability.Skill.WhirlWind", unreal.RPGEquipmentResolveOnHitAbility,
     unreal.RPGItemResolveOnHitDefinitionFragment, {"b_require_maximum_charge": False, "b_require_finisher": True, "amount": 20.0}),
)


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def tag(name):
    value = unreal.GameplayTag()
    require(value.import_text('(TagName="' + name + '")'), "Unknown skill tag " + name)
    return value


def configure():
    command = unreal.SystemLibrary.get_command_line()
    apply = "-RPGEquipmentHitEffectsApply" in command
    match = re.search(r'-RPGEquipmentHitEffectsOutput=(?:"([^"]+)"|(\S+))', command)
    output = Path((match.group(1) or match.group(2)) if match else
                  str(Path(unreal.Paths.project_saved_dir()) / "EquipmentHitEffects.json"))
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    require(not dirty, "Start with clean packages; other users' changes must not be saved")
    rows = []
    for model, effect, skill, ability_class, fragment_class, values in SPECS:
        item = unreal.EditorAssetLibrary.load_asset(ROOT + "/DA_Item_Progression_" + model)
        require(item is not None, "Missing existing item " + model)
        set_path = ROOT + "/Effects/DA_AbilitySet_" + effect + "OnHit"
        ability_set = unreal.EditorAssetLibrary.load_asset(set_path) if unreal.EditorAssetLibrary.does_asset_exist(set_path) else None
        if not ability_set and apply:
            ability_set = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                set_path.rsplit("/", 1)[1], set_path.rsplit("/", 1)[0], unreal.RPGAbilitySet, unreal.DataAssetFactory())
        require(ability_set is not None, "Missing ability set; use explicit apply to author it")
        entries = list(ability_set.get_editor_property("granted_gameplay_abilities"))
        if apply and not entries:
            struct_type = getattr(unreal, "RPGAbilitySet_GameplayAbility", None) or getattr(unreal, "RPGAbilitySetGameplayAbility")
            # EditDefaultsOnly struct fields cannot be edited as standalone
            # Python instances. Initialize their values in the struct constructor.
            entry = struct_type(ability=ability_class.static_class(), ability_level=1)
            ability_set.set_editor_property("granted_gameplay_abilities", [entry])
            entries = list(ability_set.get_editor_property("granted_gameplay_abilities"))
        require(len(entries) == 1 and entries[0].get_editor_property("ability") == ability_class.static_class(), "Unexpected ability set contents")
        fragments = list(item.get_editor_property("fragments"))
        equipment = [f for f in fragments if isinstance(f, unreal.RPGItemEquipmentDefinitionFragment)]
        effect_fragments = [f for f in fragments if isinstance(f, fragment_class)]
        require(len(equipment) == 1 and len(effect_fragments) <= 1, "Unexpected fragment shape")
        if apply and not effect_fragments:
            fragment = unreal.new_object(fragment_class, outer=item, name=effect + "OnHit")
            fragments.append(fragment)
            item.set_editor_property("fragments", fragments)
            effect_fragments = [fragment]
        require(len(effect_fragments) == 1, "Missing effect settings")
        fragment = effect_fragments[0]
        configured = {"required_skill_tag": tag(skill), **values}
        for key, value in configured.items():
            # Python drops the native boolean b prefix in generated property names.
            property_name = key[2:] if key.startswith("b_") else key
            if apply:
                fragment.set_editor_property(property_name, value)
            actual = fragment.get_editor_property(property_name)
            matches = (actual.export_text() == value.export_text() if isinstance(value, unreal.GameplayTag)
                       else abs(actual - value) < 1e-5 if isinstance(value, float) else actual == value)
            require(matches, "Settings mismatch: " + property_name)
        grants = list(equipment[0].get_editor_property("granted_ability_sets"))
        if apply and ability_set not in grants:
            equipment[0].set_editor_property("granted_ability_sets", grants + [ability_set])
        require(ability_set in equipment[0].get_editor_property("granted_ability_sets"), "Missing equipment ability grant")
        if apply:
            description = ("완충 차지 적중 시 최대 체력의 5% 보호막을 3초간 얻습니다. 시전당 한 번 발동하며 재발동 시 새 보호막으로 교체합니다."
                           if effect == "Shield" else "일반 상태에서 시작한 회전의 마무리 타격 적중 시 투지를 추가로 20 얻습니다. 시전당 한 번 적용되며 초과 투지는 저장되지 않습니다.")
            item.set_editor_property("description", unreal.Text(description))
            require(unreal.EditorAssetLibrary.save_loaded_asset(ability_set, only_if_is_dirty=True), "Ability set save failed")
            require(unreal.EditorAssetLibrary.save_loaded_asset(item, only_if_is_dirty=True), "Item save failed")
        rows.append({"item": item.get_path_name(), "abilitySet": ability_set.get_path_name(), "skill": skill,
                     "effect": effect, "configured": True})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"passed": True, "applied": apply, "items": rows}, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("EQUIPMENT_HIT_EFFECTS passed apply=" + str(apply))


configure()
