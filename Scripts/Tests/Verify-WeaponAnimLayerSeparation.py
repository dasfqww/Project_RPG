"""Read-only Unreal regression check for shared/GreatSword layer separation.

Compiles five dependent Blueprints in memory without saving any package and
checks graph connections, class ancestry, default assets, both tag combinations
and 0.1-second blends. This does not replace GPU/network animation observation.
"""
import importlib.util
import sys
from pathlib import Path


def main():
    sys.dont_write_bytecode = True
    script = Path(__file__).resolve().parents[1] / "Editor" / "SeparateGreatSwordAnimLayer.py"
    spec = importlib.util.spec_from_file_location("weapon_anim_layer", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.report({"saved": [], "validation": module.validate()})


if __name__ == "__main__":
    main()
