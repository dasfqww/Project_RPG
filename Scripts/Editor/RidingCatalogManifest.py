"""Riding UE authoring wire compiler. Gameplay bytes must match RidingCatalog.cs.

No UE imports, deployment, asset mutation, activation or grants. ExportRidingCatalog
supplies validated UE observations; this compiler also runs in the offline tests.
"""
import hashlib
import json
import math
import re
import struct

POLICY = (
    ("rideSpeed", "RideSpeed", 1, 5000), ("acceleration", "Acceleration", 1, 20000),
    ("braking", "Braking", 1, 20000), ("yawRate", "YawRate", 1, 1080),
    ("dashDistance", "DashDistance", 1, 5000), ("dashDuration", "DashDuration", .05, 5),
    ("dashCooldown", "DashCooldown", .05, 10), ("combatLock", "CombatLock", .05, 60),
    ("emoteDuration", "EmoteDuration", .05, 3), ("emoteCooldown", "EmoteCooldown", .05, 2),
)


def require(value, message):
    if not value:
        raise ValueError(message)


def exact(value, keys):
    require(type(value) is dict and set(value) == set(keys), "Unexpected/missing authoring fields")


def integer(value):
    require(type(value) is int and 1 <= value <= 2147483647, "Positive int32 required")
    return value


def identifier(value, maximum=64, lower=False):
    chars = "a-z0-9" if lower else "A-Za-z0-9"
    require(type(value) is str and len(value) <= maximum and re.fullmatch("[" + chars + "][" + chars + "._-]*", value), "Invalid stable identity")
    return value


def single(value):
    require(type(value) in (int, float), "Finite number required")
    try:
        require(math.isfinite(value), "Finite number required")
        result = struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error) as error:
        raise ValueError("Float32 overflow") from error
    require(math.isfinite(result), "Finite float32 required")
    return result


def strict_json(text):
    def pairs(entries):
        result = {}
        for key, value in entries:
            require(key not in result, "Duplicate JSON key")
            result[key] = value
        return result
    def invalid(_):
        raise ValueError("Non-finite JSON number")
    return json.loads(text, object_pairs_hook=pairs, parse_constant=invalid)


def compile_catalog(authoring):
    exact(authoring, ("protocolVersion", "catalogVersion", "policy", "mounts", "registrationItems"))
    require(type(authoring["protocolVersion"]) is int and authoring["protocolVersion"] == 1, "Protocol 1 required")
    version = integer(authoring["catalogVersion"])
    exact(authoring["policy"], (entry[0] for entry in POLICY))
    policy = {}
    lines = ["ProjectRPG.Riding.Gameplay/v1", "protocol=1", "catalog=" + str(version)]
    for source, target, minimum, maximum in POLICY:
        value = single(authoring["policy"][source])
        require(single(minimum) <= value <= single(maximum), "Policy outside v1 bounds: " + source)
        policy[target] = value
        lines.append(source + "=" + format(struct.unpack("<I", struct.pack("<f", value))[0], "08x"))
    require(policy["DashDuration"] <= policy["DashCooldown"], "Dash duration exceeds cooldown")
    raw_mounts = authoring["mounts"]
    require(type(raw_mounts) is list and 1 <= len(raw_mounts) <= 512, "Mount count must be 1..512")
    mounts, seen = [], set()
    for mount in raw_mounts:
        exact(mount, ("mountId", "definitionVersion", "enabled", "regionTags"))
        identity = identifier(mount["mountId"], lower=True)
        require(identity not in seen, "Duplicate mount identity")
        seen.add(identity)
        require(type(mount["enabled"]) is bool, "Enabled must be boolean")
        tags = mount["regionTags"]
        require(type(tags) is list and len(tags) <= 64, "Region tag count exceeds limit")
        for tag in tags:
            require(type(tag) is str and 1 <= len(tag) <= 128 and re.fullmatch(r"[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)*", tag), "Invalid region tag")
        require(len(set(tags)) == len(tags), "Duplicate region tag")
        mounts.append(dict(MountId=identity, DefinitionVersion=integer(mount["definitionVersion"]), Enabled=mount["enabled"], RegionTags=sorted(tags)))
    mounts.sort(key=lambda value: value["MountId"])
    for mount in mounts:
        lines.append("mount={MountId}|{DefinitionVersion}|".format(**mount) + ("1" if mount["Enabled"] else "0") + "|" + ",".join(mount["RegionTags"]))
    raw_items = authoring["registrationItems"]
    require(type(raw_items) is list and len(raw_items) <= 512, "Registration definition count exceeds limit")
    items, keys = [], set()
    for item in raw_items:
        exact(item, ("definitionType", "definitionName", "definitionVersion", "mountId", "maximumStackSize", "uses"))
        key = (identifier(item["definitionType"]), identifier(item["definitionName"], 128), integer(item["definitionVersion"]))
        mount_id = identifier(item["mountId"], lower=True)
        require(key not in keys and mount_id in seen, "Duplicate registration or unknown mount")
        keys.add(key)
        require(type(item["uses"]) is int and item["uses"] == 1, "Registration must have RegisterMount use only")
        items.append(dict(DefinitionType=key[0], DefinitionName=key[1], DefinitionVersion=key[2], MountId=item["mountId"], MaximumStackSize=integer(item["maximumStackSize"]), Uses=1))
    items.sort(key=lambda value: (value["DefinitionType"], value["DefinitionName"], value["DefinitionVersion"]))
    for item in items:
        lines.append("register={DefinitionType}|{DefinitionName}|{DefinitionVersion}|{MountId}|{MaximumStackSize}|{Uses}".format(**item))
    canonical = "\n".join(lines) + "\n"
    digest = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    return dict(format="ProjectRPG.Riding.Export/v1", gameplayHash=digest, canonical=canonical,
                configuration={"Riding": dict(Enabled=False, CatalogVersion=version, GameplayHash=digest, Policy=policy, Mounts=mounts, RegistrationItems=items)})
