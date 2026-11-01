"""Apply the approved initial SwordMan tuning to only its definition and two attack BPs.

Back up those three assets first. Without -RPGApplyRaidBossCombatTuning, verify only.
Custom tuning is rejected instead of overwritten; repeating the same application is a no-op.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
APPLY = "-rpgapplyraidbosscombattuning" in unreal.SystemLibrary.get_command_line().lower()


def require(path, kind):
    asset = unreal.load_asset(path)
    if not isinstance(asset, kind):
        raise RuntimeError("Missing/incompatible raid tuning asset: " + path)
    return asset


def tag(name):
    result = unreal.GameplayTag()
    if not result.import_text('(TagName="{}")'.format(name)):
        raise RuntimeError("Missing raid tuning tag: " + name)
    return result


def near(value, expected):
    return abs(value - expected) < 0.0001


def tag_name(value):
    return str(value.get_editor_property("tag_name"))


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not save scoped raid tuning asset")


definition = require(ROOT + "/DA_RaidSwordMan", unreal.RPGBossDefinition)
phases = list(definition.get_editor_property("phases"))
attacks = [require(ROOT + "/GameplayAbility/GA_RaidSwordMan_" + name, unreal.Blueprint)
           for name in ("Strike", "HeavyStrike")]
classes = [asset.generated_class() for asset in attacks]
defaults = [unreal.get_default_object(kind) for kind in classes]
cooldown_tags = [tag("Boss.Cooldown.SwordMan." + name) for name in ("Strike", "HeavyStrike")]
if len(phases) != 2 or any(not near(phase.get_editor_property("health_threshold"), expected)
                           for phase, expected in zip(phases, (1.0, 0.7))):
    raise RuntimeError("Custom phase layout; refusing to change tuning")
expected_patterns = [[classes[0]], [classes[1], classes[0]]]
for phase, original, expected in zip(phases, classes, expected_patterns):
    if list(phase.get_editor_property("ability_patterns")) not in ([original], expected):
        raise RuntimeError("Custom phase patterns; refusing overwrite")
enrage = (definition.get_editor_property("enrage_seconds"),
          definition.get_editor_property("enraged_attack_multiplier"))
if not any(near(enrage[0], seconds) and near(enrage[1], multiplier)
           for seconds, multiplier in ((0.0, 1.0), (180.0, 1.5))):
    raise RuntimeError("Custom enrage tuning; refusing overwrite")
for default, seconds, cooldown_tag in zip(defaults, (4.0, 8.0), cooldown_tags):
    if not isinstance(default, unreal.RPGBossTelegraphedStrikeAbility):
        raise RuntimeError("Raid attack does not use the native strike parent")
    current_seconds = default.get_editor_property("PatternCooldownSeconds")
    current_tag = default.get_editor_property("PatternCooldownTag")
    if not ((near(current_seconds, 0.0) and tag_name(current_tag) in ("None", "")) or
            (near(current_seconds, seconds) and tag_name(current_tag) == tag_name(cooldown_tag))):
        raise RuntimeError("Custom attack cooldown; refusing overwrite: seconds={} tag={}".format(current_seconds, tag_name(current_tag)))
    if default.get_editor_property("cooldown_gameplay_effect_class") is not None:
        raise RuntimeError("Existing authored cooldown GE; refusing replacement")

saved = 0
if APPLY:
    for asset, default, seconds, cooldown_tag in zip(attacks, defaults, (4.0, 8.0), cooldown_tags):
        if near(default.get_editor_property("PatternCooldownSeconds"), seconds) and tag_name(default.get_editor_property("PatternCooldownTag")) == tag_name(cooldown_tag):
            continue
        asset.modify()
        default.set_editor_property("PatternCooldownSeconds", seconds)
        default.set_editor_property("PatternCooldownTag", cooldown_tag)
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        if "ERROR" in str(asset.get_editor_property("status")).upper():
            raise RuntimeError("Raid tuning BP compile failed")
        save(asset)
        saved += 1
    # Blueprint compilation may reinstance generated classes/CDOs. Store only current class references.
    classes = [asset.generated_class() for asset in attacks]
    expected_patterns = [[classes[0]], [classes[1], classes[0]]]
    phases = list(definition.get_editor_property("phases"))
    if (not near(enrage[0], 180.0) or not near(enrage[1], 1.5) or
            any(list(phase.get_editor_property("ability_patterns")) != expected
                for phase, expected in zip(phases, expected_patterns))):
        definition.modify()
        definition.set_editor_property("enrage_seconds", 180.0)
        definition.set_editor_property("enraged_attack_multiplier", 1.5)
        for phase, expected in zip(phases, expected_patterns):
            phase.set_editor_property("ability_patterns", expected)
        definition.set_editor_property("phases", phases)
        save(definition)
        saved += 1

for asset, seconds, cooldown_tag in zip(attacks, (4.0, 8.0), cooldown_tags):
    default = unreal.get_default_object(asset.generated_class())
    if not near(default.get_editor_property("PatternCooldownSeconds"), seconds) or tag_name(default.get_editor_property("PatternCooldownTag")) != tag_name(cooldown_tag):
        raise RuntimeError("Raid cooldown tuning is not configured")
if not near(definition.get_editor_property("enrage_seconds"), 180.0) or not near(definition.get_editor_property("enraged_attack_multiplier"), 1.5):
    raise RuntimeError("Raid enrage tuning is not configured")
for phase, expected in zip(definition.get_editor_property("phases"), expected_patterns):
    if list(phase.get_editor_property("ability_patterns")) != expected:
        raise RuntimeError("Raid cooldown fallback is not configured")
unreal.log("RAID_COMBAT_TUNING COMPLETE StrikeCooldown=4 HeavyCooldown=8 HeavyFallback=Strike EnrageSeconds=180 EnrageAttack=1.5 SavedAssets=" + str(saved) + " Applied=" + str(int(APPLY)))
