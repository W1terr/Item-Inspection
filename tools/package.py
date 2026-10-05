"""Builds the mod archive: SKSE\\Plugins\\ItemInspection.dll (+ pdb), ItemInspection.ini, and the license and credits
in SKSE\\Plugins\\ItemInspection.
usage: python package.py [build dir] [output dir]"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"


def version() -> str:
    match = re.search(r'set_version\("([^"]+)"\)', (ROOT / "xmake.lua").read_text(encoding="utf-8"))
    return match.group(1) if match else "0.0.0"


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    files = {
        "SKSE/Plugins/ItemInspection.dll": plugin / "ItemInspection.dll",
        "SKSE/Plugins/ItemInspection.pdb": plugin / "ItemInspection.pdb",
        "SKSE/Plugins/ItemInspection.ini": ROOT / "res" / "ItemInspection.ini",
        "SKSE/Plugins/ItemInspection/LICENSE.txt": ROOT / "LICENSE",
        "SKSE/Plugins/ItemInspection/CREDITS.txt": ROOT / "res" / "CREDITS.txt",
    }
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Item Inspection {version()}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, path in files.items():
            zf.write(path, name)
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
