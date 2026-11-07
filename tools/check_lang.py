"""Checks the translation files res/lang/<language>.json against the texts the code asks for.
usage: python tools/check_lang.py
- every language file has every English text the menu / the hint pass to the text helpers, and nothing else
- every translation is filled in and keeps the English text's {} placeholders"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
LANG = ROOT / "res" / "lang"
LANGUAGES = ["french", "german", "italian", "spanish", "polish", "russian", "japanese", "chinese"]
STRING = r'"(?:[^"\\]|\\.)*"'
HELPERS = r"\b(?:T|Lang::T|Label|Header|Checkbox|Slider|KeyRow|Help)\((.*?)\);"
NOT_TEXT = {"%.1f", "%.0f", "%.2f", "%.2f s", "%s", "store", "putback", "look", "switchview", "inspect", "{}##{}", "{}_kb", "{}_pad", "[{}] {}", "[{}] {}    [{}] {}"}
MENU_ENTRIES = {"Item Inspection", "Settings", "Languages"}  # passed to the text helper as variables
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}


def unescape(text):
    """a C string literal's text as the program sees it"""
    return re.sub(r"\\(.)", lambda m: ESCAPES[m.group(1)], text)


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
    used = set(MENU_ENTRIES)
    for name in ("Menu.cpp", "Inspect.cpp"):
        code = (SRC / name).read_text(encoding="utf-8")
        for call in re.finditer(HELPERS, code, re.S):
            used.update(unescape(text) for text in literals(call.group(1)) if text and text not in NOT_TEXT)

    problems = 0
    for language in LANGUAGES:
        path = LANG / f"{language}.json"
        if not path.exists():
            print(f"missing file: {path.name}")
            problems += 1
            continue
        table = json.loads(path.read_text(encoding="utf-8"))
        for text in sorted(used - table.keys()):
            print(f"{language}: missing translation: {text[:70]!r}")
            problems += 1
        for text in sorted(table.keys() - used):
            print(f"{language}: unused translation: {text[:70]!r}")
            problems += 1
        for english, text in table.items():
            if not text.strip():
                print(f"{language}: empty translation: {english[:60]!r}")
                problems += 1
            elif text.count("{}") != english.count("{}"):
                print(f"{language}: placeholder mismatch: {english[:60]!r}")
                problems += 1
    print(f"{len(used)} texts, {len(LANGUAGES)} languages, {problems} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
