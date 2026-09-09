#!/usr/bin/env python3
"""Extract the documentation source-of-truth from Gotek firmware sketches.

The firmware already documents itself: FW_VERSION defines carry the version,
the CONFIG.TXT template table (`{"KEY", "\\n# comment\\nKEY=default\\n"}`) carries
every config key with its explanation and default, and the theme / cracktro /
language tables enumerate those features. This reads all of that out of the
.ino/.h source and emits ONE structured JSON blob, so the docs can be rebuilt
from facts instead of memory.

It parses text, never compiles anything, so it is safe to run anywhere and
fast enough to run on every build.

Usage:
    python extract_firmware_facts.py <firmware_root> [--out facts.json]

<firmware_root> is the tree that holds the sketch folders (e.g. a checkout's
`firmware/` for the mesarim tree, or the repo root for the OMEGAWARE tree —
the scanner finds sketches by locating every *.ino).
"""
import argparse, json, os, re, sys


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def find_sketches(root):
    """Every folder holding a same-named .ino is a sketch."""
    out = []
    for dirpath, _dirs, files in os.walk(root):
        # skip build artefacts and VCS
        if any(seg in dirpath for seg in (os.sep + "build", os.sep + ".git")):
            continue
        for fn in files:
            if fn.endswith(".ino"):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def grab_version(src):
    """FW_VERSION / DONGLE_VERSION / any *_VERSION define."""
    m = re.search(r'#define\s+(?:FW_VERSION|DONGLE_VERSION)\s+"([^"]+)"', src)
    if m:
        return m.group(1)
    m = re.search(r'#define\s+\w*VERSION\w*\s+"([^"]+)"', src)
    return m.group(1) if m else None


def grab_internal(src):
    m = re.search(r'#define\s+FW_INTERNAL\s+"([^"]+)"', src)
    return m.group(1) if m else None


def unescape_c(s):
    return (s.replace("\\n", "\n").replace("\\t", "\t")
             .replace('\\"', '"').replace("\\\\", "\\"))


def grab_config_keys(src):
    """The self-documenting template table:
        {"KEY", "\\n# human explanation ...\\nKEY=default\\n"}
    Returns [{key, default, doc}] preserving table order.
    """
    keys = []
    # match {"NAME", "....."} where the 2nd string is the template chunk.
    for m in re.finditer(r'\{\s*"([A-Z0-9_]+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', src):
        name, body = m.group(1), unescape_c(m.group(2))
        # doc = the '#'-comment lines; default = the KEY=value line
        doc_lines, default = [], None
        for line in body.splitlines():
            line = line.strip()
            if line.startswith("#"):
                doc_lines.append(line.lstrip("# ").rstrip())
            elif "=" in line and line.split("=", 1)[0].strip() == name:
                default = line.split("=", 1)[1].strip()
        # only keep entries that actually look like a config template
        if default is not None or doc_lines:
            keys.append({"key": name, "default": default,
                         "doc": " ".join(doc_lines).strip()})
    # de-dupe by key, last write wins (some sketches redefine)
    seen = {}
    for k in keys:
        seen[k["key"]] = k
    return list(seen.values())


def grab_parsed_keys(src):
    """Every key the firmware ACTUALLY reads: if(k=="KEY"). The gap between
    these and the documented template keys is exactly the doc drift we hunt."""
    return sorted(set(re.findall(r'k\s*==\s*"([A-Z0-9_]+)"', src)))


def grab_named_table(src, decl_regex):
    """Pull the quoted names out of a table like
        static const Theme THEMES[]={ {"NAVY",..}, {"EMBER",..}, ... };
    decl_regex must capture the { ... } body."""
    m = re.search(decl_regex, src, re.S)
    if not m:
        return []
    body = m.group(1)
    return re.findall(r'\{\s*"([^"]+)"', body)


def grab_languages(src):
    m = re.search(r'LANG_NAMES\[[^\]]*\]\s*=\s*\{([^}]*)\}', src)
    return re.findall(r'"([^"]+)"', m.group(1)) if m else []


def analyse_sketch(path):
    src = read(path)
    name = os.path.splitext(os.path.basename(path))[0]
    facts = {
        "sketch": name,
        "path": path,
        "version": grab_version(src),
        "internal": grab_internal(src),
        "config_keys": grab_config_keys(src),
        "parsed_keys": grab_parsed_keys(src),
        "themes": grab_named_table(src, r'THEMES\s*\[\]\s*=\s*\{(.*?)\};'),
        "languages": grab_languages(src),
    }
    # documented vs actually-parsed: names present in code but absent from docs
    doc_keys = {k["key"] for k in facts["config_keys"]}
    facts["undocumented_keys"] = [k for k in facts["parsed_keys"]
                                  if k not in doc_keys]
    return facts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware_root")
    ap.add_argument("--out", default="-")
    a = ap.parse_args()

    sketches = find_sketches(a.firmware_root)
    if not sketches:
        print("no .ino sketches found under " + a.firmware_root, file=sys.stderr)
        return 2
    result = {"root": os.path.abspath(a.firmware_root),
              "sketches": [analyse_sketch(p) for p in sketches]}
    text = json.dumps(result, indent=2, ensure_ascii=False)
    if a.out == "-":
        print(text)
    else:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(text)
        print("wrote " + a.out, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
