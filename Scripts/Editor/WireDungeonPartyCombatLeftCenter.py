"""MCP programmatic authoring: wire only the owned left-centre WBP variant.

Explicit pins avoid the graph DSL's English-only execution-pin detection in a
Korean editor. Existing controller/base WBP and editor preferences are preserved.
"""

import json

BP_PATH = "/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter.WBP_DungeonPartyCombat_LeftCenter"
GRAPH = {"refPath": BP_PATH + ":RefreshPartyLayout"}
VARIABLE = "Variables|WBP_DungeonPartyCombat_LeftCenter|Get"


def create_node(graph, type_id, position):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.create_node", json.dumps({"graph": graph, "type_id": type_id, "pos": position}))["returnValue"]


def infos(nodes):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.get_node_infos", json.dumps({"nodes": nodes}))["returnValue"]


def find_nodes(graph):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.find_nodes", json.dumps({"graph": graph, "title": ""}))["returnValue"]


def delete_node(node):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.delete_node", json.dumps({"node": node}))


def connect(source, target):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.connect_pins", json.dumps({"output_pin": source, "input_pin": target}))


def literal(pin, value):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.set_pin_value", json.dumps({"pin": pin, "value": value}))


def add_pin(node):
    return execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools.add_node_pin", json.dumps({"node": node}))["returnValue"]


def is_pie():
    return execute_tool("EditorToolset.EditorAppToolset.IsPIERunning", "{}")["returnValue"]


def get_widgets():
    return execute_tool("UMGToolSet.UMGToolSet.GetWidgets", json.dumps({"widgetBlueprint": {"refPath": BP_PATH}}))["returnValue"]


def compile_widget():
    return execute_tool("UMGToolSet.UMGToolSet.CompileWidgetBlueprint", json.dumps({"widgetBlueprint": {"refPath": BP_PATH}}))["returnValue"]


def node(type_id, x=0, y=0, graph=None):
    ref = create_node(graph or GRAPH, type_id, {"x": x, "y": y})
    return infos([ref])[0]


def pin(data, name, output=False):
    candidates = data["output_pins"] if output else data["input_pins"]
    return next(p["pin_id"] for p in candidates if p["name"] == name)


def output(data, name="ReturnValue"):
    return pin(data, name, True)


def get_variable(name, x, y):
    data = node(VARIABLE + name, x, y)
    return data["output_pins"][0]["pin_id"]


def call_setter(type_id, target, previous, x, y):
    data = node(type_id, x, y)
    connect(target, pin(data, "self"))
    connect(previous, pin(data, "execute"))
    return data, output(data, "then")


def set_visibility(target, previous, value, x, y):
    data, tail = call_setter("위젯|SetVisibility", target, previous, x, y)
    literal(pin(data, "InVisibility"), value)
    return tail


def select_into(condition, target, false_value, true_value, x, y):
    data = node("Utilities|선택", x, y)
    connect(condition, pin(data, "Index"))
    connect(output(data), target)
    for name, value in [("Option 0", false_value), ("Option 1", true_value)]:
        if isinstance(value, dict):
            connect(value, pin(data, name))
        else:
            literal(pin(data, name), value)
    return data


def enum_equals(source, name, x, y):
    data = node("Utilities|Enum|Equal(Enum)", x, y)
    connect(source, pin(data, "A"))
    literal(pin(data, "B"), name)
    return output(data)


def valid_index(array, index, x, y):
    data = node("유틸리티|배열|IsValidIndex", x, y)
    connect(array, pin(data, "TargetArray"))
    literal(pin(data, "IndexToTest"), str(index))
    return output(data)


def array_get(array, index, x, y):
    data = node("Utilities|Array|Get(사본)", x, y)
    connect(array, pin(data, "Array"))
    literal(pin(data, "Dimension 1"), str(index))
    return output(data, "Output")


def tint(r, g, b):
    return "(SpecifiedColor=(R=" + str(r) + ",G=" + str(g) + ",B=" + str(b) + ",A=1),ColorUseRule=UseColor_Specified)"


def build_row(index, start, members):
    y = 500 + index * 950
    card = get_variable("PartyRow" + str(index), 0, y)
    label = get_variable("PartyMember" + str(index), 0, y + 100)
    bar = get_variable("PartyHealth" + str(index), 0, y + 200)
    wrapper = get_variable("PartyHealthHeight" + str(index), 0, y + 300)
    branch = node("Utilities|FlowControl|Branch", 350, y)
    connect(start, pin(branch, "execute"))
    connect(valid_index(members, index, 0, y + 400), pin(branch, "Condition"))
    missing = set_visibility(card, output(branch, "else"), "Collapsed", 650, y + 500)
    clear, missing = call_setter("위젯|SetText(Text)", label, missing, 950, y + 500)
    literal(pin(clear, "InText"), "")
    clear, missing = call_setter("프로그레스|SetPercent", bar, missing, 1250, y + 500)
    literal(pin(clear, "InPercent"), "0")
    set_visibility(wrapper, missing, "Collapsed", 1550, y + 500)
    member = array_get(members, index, 350, y + 150)
    split = node("Utilities|Struct|BreakRPGDungeonPartyCombatMember", 650, y + 150)
    connect(member, pin(split, "RPGDungeonPartyCombatMember"))
    reconnect = enum_equals(output(split, "Presence"), "ReconnectGrace", 950, y + 200)
    dead = enum_equals(output(split, "State"), "Incapacitated", 950, y + 300)
    neg = node("수학|부울|NOTBoolean", 1250, y + 200)
    connect(reconnect, pin(neg, "A"))
    both = node("수학|부울|ANDBoolean", 1550, y + 200)
    connect(output(split, "bHasHealth"), pin(both, "A"))
    connect(output(neg), pin(both, "B"))
    tail = set_visibility(card, output(branch, "then"), "HitTestInvisible", 650, y)
    setter, tail = call_setter("위젯|SetText(Text)", label, tail, 950, y)
    formatter = node("RPG|Dungeon|Party|GetPartyMemberText", 650, y + 400)
    connect(member, pin(formatter, "Member"))
    connect(output(formatter), pin(setter, "InText"))
    setter, tail = call_setter("프로그레스|SetPercent", bar, tail, 1250, y)
    fraction = node("RPG|Dungeon|Party|GetPartyHealthFraction", 950, y + 400)
    connect(member, pin(fraction, "Member"))
    connect(output(fraction), pin(setter, "InPercent"))
    setter, tail = call_setter("위젯|SetVisibility", wrapper, tail, 1550, y)
    select_into(output(both), pin(setter, "InVisibility"), "Collapsed", "HitTestInvisible", 1550, y + 350)
    setter, tail = call_setter("프로그레스|SetFillColorandOpacity", bar, tail, 1850, y)
    select_into(dead, pin(setter, "InColor"), "(R=0.13,G=0.65,B=0.36,A=1)", "(R=0.65,G=0.15,B=0.12,A=1)", 1850, y + 200)
    setter, tail = call_setter("Class|텍스트|SetColorandOpacity", label, tail, 2150, y)
    reconnect_tint = node("Utilities|선택", 2150, y + 200)
    connect(reconnect, pin(reconnect_tint, "Index"))
    connect(output(reconnect_tint), pin(setter, "ColorAndOpacity"))
    literal(pin(reconnect_tint, "Option 1"), tint(1, .84, .48))
    select_into(dead, pin(reconnect_tint, "Option 0"), tint(1, 1, 1), tint(1, .63, .56), 2150, y + 400)


def build_notice(index, start, notices):
    y = 4500 + index * 650
    label = get_variable("PartyNotice" + str(index), 0, y)
    branch = node("Utilities|FlowControl|Branch", 350, y)
    connect(start, pin(branch, "execute"))
    connect(valid_index(notices, index, 0, y + 100), pin(branch, "Condition"))
    setter, tail = call_setter("위젯|SetText(Text)", label, output(branch, "then"), 650, y)
    value = array_get(notices, index, 350, y + 200)
    formatter = node("RPG|Dungeon|Party|GetPartyNoticeText", 650, y + 200)
    connect(value, pin(formatter, "Notice"))
    connect(output(formatter), pin(setter, "InText"))
    set_visibility(label, tail, "HitTestInvisible", 950, y)
    setter, tail = call_setter("위젯|SetText(Text)", label, output(branch, "else"), 650, y + 350)
    literal(pin(setter, "InText"), "")
    set_visibility(label, tail, "Collapsed", 950, y + 350)


def run():
    if is_pie():
        raise RuntimeError("Preserve the existing PIE session")
    if get_widgets()["info"]["widgetCount"] != 29:
        raise RuntimeError("Unexpected layout; do not overwrite")
    current = infos(find_nodes(GRAPH))
    entry = next(n for n in current if n["type_id"] == "|RefreshPartyLayout")
    if any(p["connected_pins"] for p in entry["output_pins"]):
        raise RuntimeError("Already wired: do not overwrite a working layout")
    for old in current:
        if old["node"] != entry["node"]:
            delete_node(old["node"])
    sequence = node("Utilities|FlowControl|시퀀스", 0, 0)
    for unused in range(7):
        add_pin(sequence["node"])
    sequence = infos([sequence["node"]])[0]
    connect(output(entry, "then"), pin(sequence, "execute"))
    members = output(node("RPG|Dungeon|Party|GetPartyMembers", -700, 0))
    notices = output(node("RPG|Dungeon|Party|GetActivePartyNotices", -700, 200))
    header = get_variable("PartyHeader", 350, 0)
    setter, unused = call_setter("위젯|SetText(Text)", header, output(sequence, "then_0"), 650, 0)
    connect(output(node("RPG|Dungeon|Party|GetPartyHeaderText", 350, 180)), pin(setter, "InText"))
    for i in range(4):
        build_row(i, output(sequence, "then_" + str(i + 1)), members)
    for i in range(3):
        build_notice(i, output(sequence, "then_" + str(i + 5)), notices)
    overflow = get_variable("PartyOverflow", 0, 6500)
    setter, unused = call_setter("위젯|SetVisibility", overflow, output(sequence, "then_8"), 650, 6500)
    select_into(valid_index(members, 4, 0, 6700), pin(setter, "InVisibility"), "Collapsed", "HitTestInvisible", 350, 6700)
    event_graph = {"refPath": BP_PATH + ":EventGraph"}
    event = next(n for n in infos(find_nodes(event_graph)) if "OnPartyPresentationUpdated" in n["type_id"])
    call = node("함수호출|RefreshPartyLayout", 350, 650, event_graph)
    connect(output(event, "then"), pin(call, "execute"))
    if not compile_widget():
        raise RuntimeError("Do not save failed compilation")
    return {"compiled": True, "functionNodes": len(find_nodes(GRAPH)), "eventConnected": True, "saved": False, "controllerChanged": False}
