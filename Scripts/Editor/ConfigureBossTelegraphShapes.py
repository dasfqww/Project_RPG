"""Extend the existing raid decal with server-authored shape parameters.

Read-only by default. -RPGBossTelegraphShapesApply edits only the existing
M_RaidSwordManTelegraph material. Legacy users keep the original UV circle until
TelegraphUseShape is enabled by the native presentation component.
"""
import json
import unreal

PATH = '/Game/Blueprints/Character/NPC/SwordMan/Presentation/M_RaidSwordManTelegraph'
MARKER = 'BOSS_TELEGRAPH_SHAPES_V1'
SCALARS = {'TelegraphUseShape': 0.0, 'TelegraphShape': 0.0,
           'TelegraphRadius': 200.0, 'TelegraphInnerRadius': 0.0,
           'TelegraphHalfAngle': 1.57079632679, 'TelegraphHalfX': 100.0,
           'TelegraphHalfY': 100.0, 'TelegraphLength': 500.0}
VECTORS = {'TelegraphOrigin': (0.0, 0.0, 0.0),
           'TelegraphForward': (1.0, 0.0, 0.0), 'TelegraphRight': (0.0, 1.0, 0.0)}
INPUTS = ['UV', 'Progress', 'WorldPosition'] + list(SCALARS) + list(VECTORS)
CODE = r'''
// BOSS_TELEGRAPH_SHAPES_V1
if (TelegraphUseShape < 0.5)
{
    float d = length((UV - 0.5) * 2.0);
    float inside = 1.0 - smoothstep(0.98, 1.0, d);
    float ring = smoothstep(0.88, 0.92, d) * inside;
    float fill = (1.0 - smoothstep(saturate(Progress), saturate(Progress) + 0.02, d)) * inside;
    return saturate(ring * 0.85 + fill * 0.35 + inside * 0.08);
}
float3 delta = WorldPosition - TelegraphOrigin;
float2 p = float2(dot(delta, TelegraphForward), dot(delta, TelegraphRight));
float r = length(p);
float boundary = TelegraphRadius - r;
float fillDistance = r / max(TelegraphRadius, 0.001);
if (TelegraphShape > 1.5 && TelegraphShape < 2.5)
{
    boundary = min(TelegraphHalfX - abs(p.x), TelegraphHalfY - abs(p.y));
    fillDistance = max(abs(p.x) / max(TelegraphHalfX, 0.001), abs(p.y) / max(TelegraphHalfY, 0.001));
}
else if (TelegraphShape > 4.5)
{
    float2 closest = float2(clamp(p.x, 0.0, TelegraphLength), 0.0);
    boundary = TelegraphRadius - length(p - closest);
    fillDistance = saturate((p.x + TelegraphRadius) / max(TelegraphLength + 2.0 * TelegraphRadius, 0.001));
}
else if (TelegraphShape > 2.5)
{
    if (TelegraphShape > 3.5)
    {
        boundary = min(boundary, r - TelegraphInnerRadius);
        fillDistance = (r - TelegraphInnerRadius) / max(TelegraphRadius - TelegraphInnerRadius, 0.001);
    }
    if (TelegraphHalfAngle < 3.14158)
    {
        float angle = acos(clamp(p.x / max(r, 0.001), -1.0, 1.0));
        boundary = min(boundary, (TelegraphHalfAngle - angle) * r);
    }
}
float aa = max(fwidth(boundary), 0.75);
float inside = smoothstep(-aa, aa, boundary);
float border = 1.0 - smoothstep(2.0, 9.0, boundary);
float filled = 1.0 - smoothstep(saturate(Progress), saturate(Progress) + 0.025, fillDistance);
return inside * saturate(0.08 + border * 0.8 + filled * 0.35);
'''


def require(value, message):
    if not value:
        raise RuntimeError(message)


def configure(material):
    edit = unreal.MaterialEditingLibrary
    mask = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY)
    require(isinstance(mask, unreal.MaterialExpressionCustom), 'Preserving a different opacity graph')
    old = mask.get_editor_property('code')
    if MARKER in old:
        return False
    require('length((UV - 0.5) * 2.0)' in old and 'ring * 0.85' in old,
            'Preserving a customized telegraph material')
    material.modify()
    mask.modify()
    inputs = []
    for name in INPUTS:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    mask.set_editor_property('inputs', inputs)
    mask.set_editor_property('code', CODE)
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    # Keep the existing color/output graph and connect mask inputs explicitly.
    uv = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, 100)
    progress = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1000, 200)
    progress.set_editor_property('parameter_name', 'TelegraphProgress')
    progress.set_editor_property('default_value', 0.0)
    position = edit.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 300)
    sources = {'UV': (uv, ''), 'Progress': (progress, ''), 'WorldPosition': (position, '')}
    for index, (name, value) in enumerate(SCALARS.items()):
        node = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1000, 400 + index * 80)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', value)
        sources[name] = (node, '')
    for index, (name, value) in enumerate(VECTORS.items()):
        node = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1400, 400 + index * 80)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', unreal.LinearColor(*value, 0.0))
        sources[name] = (node, 'RGB')
    for name, (node, output) in sources.items():
        require(edit.connect_material_expressions(node, output, mask, name), 'Cannot connect ' + name)
    edit.recompile_material(material)
    return True


def verify(material):
    edit = unreal.MaterialEditingLibrary
    require(material.get_editor_property('material_domain') == unreal.MaterialDomain.MD_DEFERRED_DECAL,
            'Expected deferred decal domain')
    mask = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY)
    require(isinstance(mask, unreal.MaterialExpressionCustom) and mask.get_editor_property('code') == CODE,
            'Shape mask differs from the maintained shader')
    names = {str(n) for n in edit.get_scalar_parameter_names(material)}
    require(set(SCALARS).issubset(names), 'Missing shape scalar parameters')
    for name, expected in SCALARS.items():
        actual = edit.get_material_default_scalar_parameter_value(material, name)
        require(abs(actual - expected) < 0.0001, 'Changed default: ' + name)
    require(set(VECTORS).issubset({str(n) for n in edit.get_vector_parameter_names(material)}),
            'Missing world-space orientation parameters')


def main():
    material = unreal.load_asset(PATH)
    require(isinstance(material, unreal.Material), 'Missing raid decal material')
    apply = '-rpgbosstelegraphshapesapply' in unreal.SystemLibrary.get_command_line().lower()
    changed = configure(material) if apply else False
    verify(material)
    if changed:
        require(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=True), 'Cannot save material')
    unreal.log('BOSS_TELEGRAPH_SHAPES ' + json.dumps({'passed': True, 'changed': changed,
               'shapes': ['Sphere', 'Capsule', 'Box', 'Sector', 'RingSector', 'LineSweep'],
               'legacy_circle_default': True, 'material': material.get_path_name()}))


main()
