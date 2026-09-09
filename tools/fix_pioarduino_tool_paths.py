#!/usr/bin/env python3
"""Explicit, reversible workaround for pioarduino 55.03.311 tool paths.

Default: preview only. --apply: backup and patch the installed platform.
Not a PlatformIO extra_script. Does not download or execute platform code.
"""
import argparse
import ast
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import sys

SUPPORTED = {"55.3.311", "55.03.311"}
MARKER = "# LCD5B_TOOL_PATHS_PDS203"
HELPER_NAME = "_lcd5b_resolve_tool_path"

# Kept as literal source for injection into the installed SCons script.
# Imports are local, independent of the platform script's imports.
HELPER = '''
# LCD5B_TOOL_PATHS_PDS203
def _lcd5b_resolve_tool_path(toolchain_dir, executable):
    """Resolve only within this selected toolchain, never a random PATH entry."""
    import os as _os
    from pathlib import Path as _Path
    _name = str(executable).strip()
    if not _name or any(_c in _name for _c in ('"', "'", '\\n', '\\r')):
        raise RuntimeError("PDS203: unexpected compiler executable: " + repr(_name))
    _tool = _Path(_name)
    _names = [_tool.name]
    if _os.name == "nt" and not _tool.name.lower().endswith(".exe"):
        _names.insert(0, _tool.name + ".exe")
    _root = _Path(toolchain_dir)
    if _tool.is_absolute():
        _candidates = [_tool.with_name(_n) for _n in _names]
    elif _tool.parent != _Path("."):
        raise RuntimeError("PDS203: relative executable subpath not supported: " + _name)
    else:
        _candidates = [_folder / _n
                       for _folder in (_root / "bin", _root / "xtensa-esp-elf" / "bin")
                       for _n in _names]
    for _candidate in _candidates:
        if _candidate.is_file():
            return _candidate.resolve().as_posix()
    raise RuntimeError("PDS203: tool missing; checked: " +
                       ", ".join(str(_p) for _p in _candidates))

'''
# Newline escapes above must remain escapes inside the injected Python source.
HELPER = HELPER.replace("'\n'", "'\\n'").replace("'\r'", "'\\r'")

OLD_CC = '''f'-DCC="{str(Path(TOOLCHAIN_DIR) / "bin" / "$CC")}"','''
MANUAL_CC = '''f'-DCC="{str(Path(TOOLCHAIN_DIR) / "xtensa-esp-elf" / "bin" / (env.subst("$CC") + ".exe"))}"','''
NEW_CC = '''f'-DCC="{_lcd5b_resolve_tool_path(TOOLCHAIN_DIR, env.subst("$CC"))}"','''
OLD_OD = '''"objdump": str(Path(TOOLCHAIN_DIR) / "bin" / env.subst("$CC").replace("-gcc", "-objdump")),'''
MANUAL_OD = '''"objdump": str(Path(TOOLCHAIN_DIR) / "xtensa-esp-elf" / "bin" / (env.subst("$CC").replace("-gcc", "-objdump") + ".exe")),'''
NEW_OD = '''"objdump": _lcd5b_resolve_tool_path(TOOLCHAIN_DIR, env.subst("$CC").replace("-gcc", "-objdump")),'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def version_of(directory):
    try:
        manifest = json.loads((directory / "platform.json").read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    if manifest.get("name") != "espressif32":
        return None
    return manifest.get("version")


def discover(core, explicit=None, platforms=None):
    if explicit is not None:
        candidate = explicit.expanduser().resolve()
        if version_of(candidate) not in SUPPORTED:
            raise ValueError("Selected platform is not espressif32 55.03.311; nothing changed.")
        return candidate
    directory = (platforms or core / "platforms").expanduser()
    found = sorted(p for p in directory.glob("*")
                   if p.is_dir() and version_of(p) in SUPPORTED
                   and (p / "builder/frameworks/espidf.py").is_file())
    if len(found) != 1:
        listing = "\n".join("  " + str(p) for p in found) or "  (none)"
        raise ValueError("Expected ONE installed 55.03.311 platform, found "
                         + str(len(found)) + ":\n" + listing
                         + "\nUse --platform-dir with the exact platform directory from the build log.")
    return found[0].resolve()


def prepared_patch(data):
    text = data.decode("utf-8-sig")
    newline = "\r\n" if "\r\n" in text else "\n"
    normalized = text.replace("\r\n", "\n")
    tree = ast.parse(normalized)
    if MARKER in normalized:
        if HELPER.strip() not in normalized or normalized.count(NEW_CC) != 1 or normalized.count(NEW_OD) != 1:
            raise ValueError("An altered/incomplete PDS203 patch is present; nothing changed.")
        return data, "already patched"
    if HELPER_NAME in normalized:
        raise ValueError("Resolver name already exists without the expected marker; nothing changed.")
    lines = normalized.splitlines(keepends=True)
    replacements = [
        ({OLD_CC, MANUAL_CC}, NEW_CC, "IDF5 compiler path"),
        ({OLD_OD, MANUAL_OD}, NEW_OD, "objdump path"),
    ]
    for old_variants, new, label in replacements:
        matches = [i for i, line in enumerate(lines) if line.strip() in old_variants]
        if len(matches) != 1:
            raise ValueError("Expected exactly one known " + label
                             + " expression; found " + str(len(matches))
                             + ". Platform code differs; nothing changed.")
        i = matches[0]
        indent = re.match(r"\s*", lines[i]).group(0)
        lines[i] = indent + new + ("\n" if lines[i].endswith("\n") else "")
    # Define helper before any executable platform setup, but after module
    # docstring / future imports, so Python's future-import rules remain valid.
    index = 0
    for node in tree.body:
        is_doc = (index == 0 and isinstance(node, ast.Expr)
                  and isinstance(node.value, ast.Constant)
                  and isinstance(node.value.value, str))
        is_future = isinstance(node, ast.ImportFrom) and node.module == "__future__"
        if is_doc or is_future:
            index = node.end_lineno
        else:
            break
    lines.insert(index, HELPER)
    result = "".join(lines)
    ast.parse(result)
    bom = b"\xef\xbb\xbf" if data.startswith(b"\xef\xbb\xbf") else b""
    return bom + result.replace("\n", newline).encode("utf-8"), "two path expressions + resolver"


def apply_patch_file(target, original, updated):
    if target.read_bytes() != original:
        raise ValueError("File changed since inspection; stopped without modification.")
    timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup = target.with_name(target.name + ".pds203-backup-" + timestamp)
    # Exclusive creation, never overwrite a backup.
    with backup.open("xb") as stream:
        stream.write(original)
    try:
        with target.open("wb") as stream:
            stream.write(updated)
        if target.read_bytes() != updated:
            raise OSError("Post-write verification failed")
    except OSError:
        target.write_bytes(original)
        raise
    return backup


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Apply after validation; otherwise preview only.")
    parser.add_argument("--core-dir", type=Path,
                        default=Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio"))))
    parser.add_argument("--platforms-dir", type=Path,
                        default=Path(os.environ["PLATFORMIO_PLATFORMS_DIR"]) if os.environ.get("PLATFORMIO_PLATFORMS_DIR") else None)
    parser.add_argument("--platform-dir", type=Path, help="Exact installed platform directory, if discovery is ambiguous.")
    args = parser.parse_args(argv)
    try:
        platform = discover(args.core_dir.expanduser(), args.platform_dir, args.platforms_dir)
        target = platform / "builder/frameworks/espidf.py"
        original = target.read_bytes()
        updated, status = prepared_patch(original)
        print("Platform:", platform)
        print("File:", target)
        print("Current SHA256:", digest(original))
        print("Result:", status)
        if updated == original:
            print("No changes required.")
            return 0
        print("Proposed SHA256:", digest(updated))
        if not args.apply:
            print("PREVIEW ONLY. Run again with --apply to create a backup and apply.")
            return 0
        backup = apply_patch_file(target, original, updated)
        print("Backup:", backup)
        print("Applied and verified. Now build normally; no firmware was compiled/uploaded by this script.")
        return 0
    except (OSError, ValueError, SyntaxError, UnicodeError) as error:
        print("STOP:", error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
