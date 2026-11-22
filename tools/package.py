"""Builds the mod archive: the plugin (dll + pdb), its INI, the translations, license and credits, in the Data folder
layout (SKSE\\Plugins\\...). In 3rd person the hand always puts items into the pocket (bStowOverShoulder=0 in the INI).
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


def pocket_ini() -> bytes:
    ini = (ROOT / "res" / "ItemInspection.ini").read_bytes()
    key = re.compile(rb"^bStowOverShoulder=\d", re.MULTILINE)
    if len(key.findall(ini)) != 1:
        raise SystemExit("res/ItemInspection.ini: bStowOverShoulder not found exactly once")
    return key.sub(b"bStowOverShoulder=0", ini)


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    files = {
        "SKSE/Plugins/ItemInspection.dll": plugin / "ItemInspection.dll",
        "SKSE/Plugins/ItemInspection.pdb": plugin / "ItemInspection.pdb",
        "SKSE/Plugins/ItemInspection/LICENSE.txt": ROOT / "LICENSE",
        "SKSE/Plugins/ItemInspection/CREDITS.txt": ROOT / "res" / "CREDITS.txt",
    }
    # the translations: one file per language, read by the DLL at the start (English is built in)
    for lang in sorted((ROOT / "res" / "lang").glob("*.json")):
        files[f"SKSE/Plugins/ItemInspection/lang/{lang.name}"] = lang
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Item Inspection {version()}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, path in files.items():
            zf.write(path, name)
        zf.writestr("SKSE/Plugins/ItemInspection.ini", pocket_ini())
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
