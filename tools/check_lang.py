"""Checks the translations in src/Lang.cpp against the texts the code asks for.
usage: python tools/check_lang.py
- every entry has a text for all 9 languages, with the same {} placeholders as the English one
- every English text the menu / the hint pass to the text helpers has an entry
- no entry is left over that nothing asks for any more"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"
STRING = r'"(?:[^"\\]|\\.)*"'
HELPERS = r"\b(?:T|Lang::T|Label|Header|Checkbox|Slider|KeyRow|Help)\((.*?)\);"
NOT_TEXT = {"%.1f", "%.0f", "%.2f", "%.2f s", "%s", "store", "putback", "look", "switchview", "{}##{}", "{}_kb", "{}_pad", "[{}] {}", "[{}] {}    [{}] {}"}


def literals(text):
    """the C string literals in a piece of code, adjacent ones joined, split at commas"""
    out, current = [], None
    for match in re.finditer(STRING + r"|,|\?|:", text):
        token = match.group(0)
        if token in (",", "?", ":"):
            if current is not None:
                out.append(current)
            current = None
        else:
            current = (current or "") + token[1:-1]
    if current is not None:
        out.append(current)
    return out


def main():
    lang = (SRC / "Lang.cpp").read_text(encoding="utf-8")
    body = lang[lang.index("kTexts[] = {"):lang.index("const std::unordered_map")]
    entries = [literals(block) for block in re.findall(r"\{\s*(" + STRING + r"(?:\s*(?:" + STRING + r"|,))*)\s*\}", body)]
    keys = {entry[0] for entry in entries}

    used = set()
    for name in ("Menu.cpp", "Inspect.cpp"):
        code = (SRC / name).read_text(encoding="utf-8")
        for call in re.finditer(HELPERS, code, re.S):
            used.update(literals(call.group(1)))
    used = {text for text in used if text and text not in NOT_TEXT}

    problems = 0
    for entry in entries:
        if len(entry) != 9:
            print(f"not 9 texts ({len(entry)}): {entry[0][:60]}")
            problems += 1
        elif any(text.count("{}") != entry[0].count("{}") for text in entry):
            print(f"placeholder mismatch: {entry[0][:60]}")
            problems += 1
    for text in sorted(used - keys):
        print(f"missing translation: {text[:70]}")
        problems += 1
    for text in sorted(keys - used - {"Item Inspection", "Settings", "Languages"}):  # the menu entries are passed as variables
        print(f"unused translation: {text[:70]}")
        problems += 1
    print(f"{len(entries)} texts, {problems} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
