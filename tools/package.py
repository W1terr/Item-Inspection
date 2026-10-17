"""Builds the mod archive as a FOMOD installer: the plugin (dll + pdb, license and credits in SKSE\\Plugins\\ItemInspection)
is always installed, the installer then asks how the hand puts items in the backpack in 3rd person and installs the
matching ItemInspection.ini (bStowOverShoulder 0 = into a pocket, 1 = over the shoulder).
usage: python package.py [build dir] [output dir]"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"

AUTHOR = "W1ter"
WEBSITE = "https://github.com/W1terr/Item-Inspection"
SHOULDER_IMAGE = "fomod/images/over_shoulder.gif"

# (folder in the archive, bStowOverShoulder, option name, type, description, image)
STOW_OPTIONS = [
    ("Options/Pocket", "0", "Into a pocket", "Recommended",
     "The hand lifts the item a little, then puts it into a pocket at your right hip, closing into a fist; "
     "the item slips in and the arm hangs back into its animation.", None),
    ("Options/Over the shoulder", "1", "Over the shoulder", "Optional",
     "The hand closes around the item, takes it over your right shoulder onto your back and comes back down, "
     "like the vanilla bow sheath.", SHOULDER_IMAGE),
]


def version() -> str:
    match = re.search(r'set_version\("([^"]+)"\)', (ROOT / "xmake.lua").read_text(encoding="utf-8"))
    return match.group(1) if match else "0.0.0"


def ini_with_stow(a_value: str) -> bytes:
    ini = (ROOT / "res" / "ItemInspection.ini").read_bytes()
    key = re.compile(rb"^bStowOverShoulder=\d", re.MULTILINE)
    if len(key.findall(ini)) != 1:
        raise SystemExit("res/ItemInspection.ini: bStowOverShoulder not found exactly once")
    return key.sub(b"bStowOverShoulder=" + a_value.encode(), ini)


def info_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<fomod>
	<Name>Item Inspection</Name>
	<Author>{AUTHOR}</Author>
	<Version>{version()}</Version>
	<Website>{WEBSITE}</Website>
	<Description>Pick up an item and hold it in your hand: turn it with the mouse, then put it in your backpack or back where it was.</Description>
</fomod>
"""


def windows_path(a_path: str) -> str:
    return a_path.replace("/", "\\")


def module_config() -> str:
    plugins = []
    for folder, _, name, kind, description, image in STOW_OPTIONS:
        image_line = f'\n\t\t\t\t\t\t\t<image path="{windows_path(image)}" />' if image else ""
        plugins.append(f"""						<plugin name="{name}">
							<description>{description}</description>{image_line}
							<files>
								<folder source="{windows_path(folder)}" destination="" priority="0" />
							</files>
							<typeDescriptor>
								<type name="{kind}" />
							</typeDescriptor>
						</plugin>""")
    plugin_xml = "\n".join(plugins)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
	<moduleName>Item Inspection</moduleName>
	<requiredInstallFiles>
		<folder source="Core" destination="" />
	</requiredInstallFiles>
	<installSteps order="Explicit">
		<installStep name="Putting items away">
			<optionalFileGroups order="Explicit">
				<group name="In 3rd person, the hand puts items in the backpack" type="SelectExactlyOne">
					<plugins order="Explicit">
{plugin_xml}
					</plugins>
				</group>
			</optionalFileGroups>
		</installStep>
	</installSteps>
</config>
"""


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    files = {
        "Core/SKSE/Plugins/ItemInspection.dll": plugin / "ItemInspection.dll",
        "Core/SKSE/Plugins/ItemInspection.pdb": plugin / "ItemInspection.pdb",
        "Core/SKSE/Plugins/ItemInspection/LICENSE.txt": ROOT / "LICENSE",
        "Core/SKSE/Plugins/ItemInspection/CREDITS.txt": ROOT / "res" / "CREDITS.txt",
        SHOULDER_IMAGE: ROOT / "res" / "fomod" / "over_shoulder.gif",
    }
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Item Inspection {version()}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, path in files.items():
            zf.write(path, name)
        for folder, value, *_ in STOW_OPTIONS:
            zf.writestr(f"{folder}/SKSE/Plugins/ItemInspection.ini", ini_with_stow(value))
        zf.writestr("fomod/info.xml", info_xml())
        zf.writestr("fomod/ModuleConfig.xml", module_config())
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
