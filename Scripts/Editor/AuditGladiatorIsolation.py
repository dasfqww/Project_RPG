"""Read-only package-reference audit before/after isolating GladiatorCore.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file> -nullrhi.
Uses registry metadata, never loads/saves Gladiator assets or modifies packages.
"""
from collections import deque
import json
import unreal


LEGACY_ROOT = "/GladiatorCore/"
PLAY_MAP = "/Game/Maps/testmap"
CURRENT_ROOTS = (
    "/Game/Blueprints/GameData/Skill",
    "/Game/Blueprints/UI/Skill",
    "/Game/Blueprints/GameData/Item",
    "/Game/System/Experiences",
)


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    options = unreal.AssetRegistryDependencyOptions()
    dependency_cache = {}

    def dependencies(package):
        if package not in dependency_cache:
            dependency_cache[package] = sorted(
                str(dep) for dep in registry.get_dependencies(package, options)
            )
        return dependency_cache[package]

    def legacy_chains(roots):
        queue = deque((root, [root]) for root in sorted(set(roots)))
        visited = set()
        chains = []
        while queue:
            package, chain = queue.popleft()
            if package in visited:
                continue
            visited.add(package)
            for dependency in dependencies(package):
                if dependency.startswith(LEGACY_ROOT):
                    chains.append(chain + [dependency])
                elif not dependency.startswith(("/Script/", "/Engine/")):
                    queue.append((dependency, chain + [dependency]))
        return {"visited_packages": len(visited), "legacy_chains": chains}

    game_assets = list(registry.get_assets_by_path("/Game", recursive=True))
    direct_references = []
    for asset in game_assets:
        package = str(asset.package_name)
        for dependency in dependencies(package):
            if dependency.startswith(LEGACY_ROOT):
                direct_references.append({"source": package, "dependency": dependency})

    catalog_roots = [
        str(asset.package_name)
        for asset in game_assets
        if any(str(asset.package_path).startswith(root) for root in CURRENT_ROOTS)
    ]
    map_assets = registry.get_assets_by_package_name(PLAY_MAP)
    if not map_assets:
        raise RuntimeError("Current play map is missing from the registry: " + PLAY_MAP)

    report = {
        "map": PLAY_MAP,
        "gladiator_enabled": "GladiatorCore" in unreal.PluginBlueprintLibrary.get_enabled_plugin_names(),
        "gladiator_mounted": unreal.PluginBlueprintLibrary.is_plugin_mounted("GladiatorCore"),
        "game_asset_count": len(game_assets),
        "direct_game_references": direct_references,
        "play_map": legacy_chains([PLAY_MAP]),
        "current_catalogs": legacy_chains(catalog_roots),
    }
    unreal.log("[RPG_GLADIATOR_AUDIT] " + json.dumps(report, ensure_ascii=False, sort_keys=True))


main()
