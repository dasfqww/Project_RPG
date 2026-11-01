"""MCP programmatic authoring for the owned left-centre party HUD variant.

Execute with ProgrammaticToolset.execute_tool_script, not as general Python.
Only this variant is edited; existing dirty controller/base WBP are not saved.
Graph wiring and final compile/save are separate explicit authoring steps.
"""

import copy
import json

BP = {"refPath": "/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter.WBP_DungeonPartyCombat_LeftCenter"}
ROW_COUNT = 4


def get_widgets():
    return execute_tool("UMGToolSet.UMGToolSet.GetWidgets", json.dumps({"widgetBlueprint": BP}))["returnValue"]


def add_widget(cls, name, parent):
    return execute_tool("UMGToolSet.UMGToolSet.AddWidget", json.dumps({
        "widgetBlueprint": BP, "widgetClass": {"refPath": "/Script/UMG." + cls},
        "widgetDisplayName": name, "parentWidget": parent,
    }))["returnValue"]


def list_properties(obj):
    return json.loads(execute_tool("editor_toolset.toolsets.object.ObjectTools.list_properties", json.dumps({"instance": obj}))["returnValue"])


def get_properties(obj, names):
    return json.loads(execute_tool("editor_toolset.toolsets.object.ObjectTools.get_properties", json.dumps({"instance": obj, "properties": names}))["returnValue"])


def set_properties(obj, values):
    return execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({"instance": obj, "values": json.dumps(values)}))["returnValue"]


def expose_widget(widget):
    return execute_tool("UMGToolSet.UMGToolSet.ToggleWidgetAsVariable", json.dumps({"widgetBlueprint": BP, "widget": widget, "bIsVariable": True}))


def is_pie():
    return execute_tool("EditorToolset.EditorAppToolset.IsPIERunning", "{}")["returnValue"]


def merge(old, patch):
    value = copy.deepcopy(old)
    for key, replacement in patch.items():
        if isinstance(replacement, dict) and isinstance(value.get(key), dict):
            value[key] = merge(value[key], replacement)
        else:
            value[key] = replacement
    return value


def configure(obj, patch):
    # UMG tools require discovery -> read -> write for every widget and slot.
    schema = list_properties(obj)
    missing = set(patch) - set(schema)
    if missing:
        raise RuntimeError("Unknown properties: " + str(sorted(missing)))
    old = get_properties(obj, list(patch))
    if not set_properties(obj, merge(old, patch)):
        raise RuntimeError("Property write failed: " + str(obj))


def margin(left=0, top=0, right=0, bottom=0):
    return dict(left=left, top=top, right=right, bottom=bottom)


def colour(r, g, b, a=1):
    return dict(r=r, g=g, b=b, a=a)


def slate_colour(r, g, b, a=1):
    return dict(specifiedColor=colour(r, g, b, a), colorUseRule="UseColor_Specified")


def text_style(info, text, size, tint=None):
    values = {"text": text, "font": {"size": size}, "autoWrapText": True, "visibility": "HitTestInvisible"}
    if tint:
        values["colorAndOpacity"] = tint
    configure(info["widget"], values)


def run():
    if is_pie():
        raise RuntimeError("Preserve the existing PIE session; stop authoring.")
    tree = get_widgets()
    expected = {"PartyRoot", "PartyPanel", "PartyContent", "PartyHeader"}
    widgets = {w["widgetName"]: w for w in tree["widgets"]}
    if set(widgets) != expected:
        raise RuntimeError("Only the owned four-widget skeleton may be expanded; do not overwrite an authored variant.")
    if tree["info"]["parentClass"]["refPath"] != "/Script/Project_RPG.RPGDungeonPartyCombatWidget":
        raise RuntimeError("Unexpected HUD parent")

    configure(widgets["PartyRoot"]["widget"], {"visibility": "HitTestInvisible"})
    configure(widgets["PartyPanel"]["widget"], {"widthOverride": 280, "bOverride_WidthOverride": True})
    configure(widgets["PartyPanel"]["slot"], {
        "layoutData": {"offsets": margin(left=24),
                       "anchors": {"minimum": {"x": 0, "y": 0.5}, "maximum": {"x": 0, "y": 0.5}},
                       "alignment": {"x": 0, "y": 0.5}},
        "bAutoSize": True,
    })
    text_style(widgets["PartyHeader"], "파티 전투 정보 · 미리보기", 12, slate_colour(.75, .82, .9))
    configure(widgets["PartyHeader"]["slot"], {"padding": margin(bottom=8)})
    expose_widget(widgets["PartyHeader"]["widget"])

    for i in range(ROW_COUNT):
        card = add_widget("Border", "PartyRow" + str(i), widgets["PartyContent"]["widget"])
        configure(card["widget"], {"brushColor": colour(.025, .04, .06, .82), "padding": margin(10, 7, 10, 9), "visibility": "HitTestInvisible"})
        configure(card["slot"], {"padding": margin(bottom=5)})
        expose_widget(card["widget"])
        body = add_widget("VerticalBox", "PartyRowContent" + str(i), card["widget"])
        label = add_widget("TextBlock", "PartyMember" + str(i), body["widget"])
        text_style(label, "파티원 " + str(i + 1) + " · 미리보기", 14)
        configure(label["slot"], {"padding": margin(bottom=5)})
        expose_widget(label["widget"])
        height = add_widget("SizeBox", "PartyHealthHeight" + str(i), body["widget"])
        configure(height["widget"], {"heightOverride": 8, "bOverride_HeightOverride": True})
        expose_widget(height["widget"])
        bar = add_widget("ProgressBar", "PartyHealth" + str(i), height["widget"])
        configure(bar["widget"], {"percent": [1, .7, .35, 0][i], "fillColorAndOpacity": colour(.13, .65, .36), "visibility": "HitTestInvisible"})
        expose_widget(bar["widget"])

    for i in range(3):
        notice = add_widget("TextBlock", "PartyNotice" + str(i), widgets["PartyContent"]["widget"])
        text_style(notice, "", 12, slate_colour(1, .84, .48))
        configure(notice["widget"], {"visibility": "Collapsed"})
        configure(notice["slot"], {"padding": margin(top=3)})
        expose_widget(notice["widget"])
    note = add_widget("TextBlock", "PartyScopeNote", widgets["PartyContent"]["widget"])
    text_style(note, "접속·탈퇴 상태는 서버 확인 기준", 11, slate_colour(.65, .71, .78))
    configure(note["slot"], {"padding": margin(top=5)})
    # An exceptional larger roster is explicitly identified instead of silently
    # suggesting this four-slot presentation shows every member.
    overflow = add_widget("TextBlock", "PartyOverflow", widgets["PartyContent"]["widget"])
    text_style(overflow, "전체 인원은 상단에 표시 · 이 화면은 4인용", 11, slate_colour(1, .84, .48))
    configure(overflow["widget"], {"visibility": "Collapsed"})
    expose_widget(overflow["widget"])
    return {"blueprint": BP, "widgets": get_widgets()["info"]["widgetCount"], "rows": ROW_COUNT, "saved": False, "graphWiringPending": True}
