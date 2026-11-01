"""Opt-in creation of the raid telegraph decal and BP style mapping.

Use -RPGApplyRaidSwordManPresentation to save; default invocation only verifies.
No textures, external plugins, ordinary NPC assets or attack logic are changed.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
MATERIAL = ROOT + "/Presentation/M_RaidSwordManTelegraph"
BOSS = ROOT + "/BP_RaidSwordMan"
ATTACKS = [ROOT + "/GameplayAbility/GA_RaidSwordMan_Strike",
           ROOT + "/GameplayAbility/GA_RaidSwordMan_HeavyStrike"]


def require(path, kind):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, kind):
        raise RuntimeError("Missing or incompatible asset: " + path)
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + asset.get_path_name())


def make_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        return require(MATERIAL, unreal.Material)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_RaidSwordManTelegraph", ROOT + "/Presentation", unreal.Material, unreal.MaterialFactoryNew())
    if asset is None:
        raise RuntimeError("Could not create telegraph material")
    asset.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    asset.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    asset.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    edit = unreal.MaterialEditingLibrary
    uv = edit.create_material_expression(asset, unreal.MaterialExpressionTextureCoordinate, -600, 100)
    progress = edit.create_material_expression(asset, unreal.MaterialExpressionScalarParameter, -600, 300)
    progress.set_editor_property("parameter_name", "TelegraphProgress")
    progress.set_editor_property("default_value", 0.0)
    color = edit.create_material_expression(asset, unreal.MaterialExpressionVectorParameter, -600, -100)
    color.set_editor_property("parameter_name", "TelegraphColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.15, 0.02, 1.0))
    mask = edit.create_material_expression(asset, unreal.MaterialExpressionCustom, -300, 150)
    inputs = []
    for name in ("UV", "Progress"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    mask.set_editor_property("inputs", inputs)
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mask.set_editor_property("code", """
float d = length((UV - 0.5) * 2.0);
float inside = 1.0 - smoothstep(0.98, 1.0, d);
float ring = smoothstep(0.88, 0.92, d) * inside;
float fill = (1.0 - smoothstep(saturate(Progress), saturate(Progress) + 0.02, d)) * inside;
return saturate(ring * 0.85 + fill * 0.35 + inside * 0.08);
""")
    for source, output, destination, input_name in (
            (uv, "", mask, "UV"), (progress, "", mask, "Progress")):
        if not edit.connect_material_expressions(source, output, destination, input_name):
            raise RuntimeError("Could not connect material input " + input_name)
    for source, output, prop in ((color, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                                 (mask, "", unreal.MaterialProperty.MP_OPACITY)):
        if not edit.connect_material_property(source, output, prop):
            raise RuntimeError("Could not connect material output")
    edit.recompile_material(asset)
    save(asset)
    return asset


def get_presentation():
    boss = require(BOSS, unreal.Blueprint)
    default = unreal.get_default_object(boss.generated_class())
    component = default.get_editor_property("boss_telegraph_presentation")
    if component is None:
        raise RuntimeError("Rebuild Editor to expose the presentation component")
    return boss, component


def verify():
    material = require(MATERIAL, unreal.Material)
    if material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_DEFERRED_DECAL:
        raise RuntimeError("Telegraph material is not a decal")
    boss, component = get_presentation()
    styles = list(component.get_editor_property("styles"))
    if len(styles) != len(ATTACKS):
        raise RuntimeError("Expected one presentation style for each raid attack")
    for path, style in zip(ATTACKS, styles):
        attack = unreal.get_default_object(require(path, unreal.Blueprint).generated_class())
        # Python struct wrappers do not provide GameplayTag's native value equality.
        style_tag = style.get_editor_property("action_tag").export_text()
        attack_tag = attack.get_editor_property("action_tag").export_text()
        if (style_tag != attack_tag or
                style.get_editor_property("radius") != attack.get_editor_property("radius") or
                style.get_editor_property("material").get_path_name() != material.get_path_name()):
            raise RuntimeError("Presentation style differs from server attack {}: tag={} expected={}, "
                               "radius={} expected={}, material={} expected={}".format(
                                   path, style_tag, attack_tag,
                                   style.get_editor_property("radius"), attack.get_editor_property("radius"),
                                   style.get_editor_property("material").get_path_name(), material.get_path_name()))
    unreal.log_warning("RAIDPRESENTATION saved material and style verification passed")


def main():
    if "-rpgapplyraidswordmanpresentation" not in unreal.SystemLibrary.get_command_line().lower():
        verify()
        return
    material = make_material()
    boss, component = get_presentation()
    styles = []
    for index, path in enumerate(ATTACKS):
        attack = unreal.get_default_object(require(path, unreal.Blueprint).generated_class())
        style = unreal.RPGBossTelegraphStyle()
        style.set_editor_property("action_tag", attack.get_editor_property("action_tag"))
        style.set_editor_property("radius", attack.get_editor_property("radius"))
        style.set_editor_property("material", material)
        style.set_editor_property("color", unreal.LinearColor(1.0, 0.15 if index == 0 else 0.03, 0.02, 1.0))
        styles.append(style)
    component.set_editor_property("styles", styles)
    boss.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(boss)
    if "ERROR" in str(boss.get_editor_property("status")).upper():
        raise RuntimeError("Boss Blueprint compile failed")
    save(boss)
    verify()


main()
