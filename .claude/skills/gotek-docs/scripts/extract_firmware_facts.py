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


# ── Endpoint surfaces: HTTP routes, the TCP escape opcodes, the UDP beacon ──
#
# These need more than the .ino. The dongles register their routes in the sketch
# itself, but the panel's routes are ALL in shared/web_panel.h and none are in
# the .ino — so harvesting endpoints has to follow #include or the panel comes
# out with an empty table. The older facts above deliberately keep reading the
# .ino alone, so this addition cannot change what CONFIG-REFERENCE.md says.


def grab_git_ref(start):
    """Which checkout this run read, as branch + SHA, by reading .git as TEXT —
    no subprocess, so the extractor keeps its promise of parsing only.

    Provenance matters more here than anywhere else in this tool: both of our
    worktrees hold ALL the sketches, and the two branches disagree about the
    same sketch (one carries the ported dongle, the other the ported panel). A
    reader who cannot see which checkout a table came from cannot tell which of
    the two is in front of them."""
    d = os.path.abspath(start)
    while True:
        cand = os.path.join(d, ".git")
        if os.path.exists(cand):
            gitdir = cand
            if os.path.isfile(cand):                 # linked worktree
                m = re.match(r'gitdir:\s*(.+)', read(cand).strip())
                if not m:
                    return None
                gitdir = m.group(1).strip()
            head = read(os.path.join(gitdir, "HEAD")).strip()
            m = re.match(r'ref:\s*(.+)', head)
            if not m:
                return {"branch": None, "sha": head[:7] or None}
            ref = m.group(1).strip()
            sha = read(os.path.join(gitdir, ref)).strip()
            if not sha:                              # commondir / packed refs
                common = read(os.path.join(gitdir, "commondir")).strip()
                if common:
                    base = os.path.normpath(os.path.join(gitdir, common))
                    sha = read(os.path.join(base, ref)).strip()
                    if not sha:
                        for line in read(os.path.join(base, "packed-refs")).splitlines():
                            parts = line.split()
                            if len(parts) == 2 and parts[1] == ref:
                                sha = parts[0]
                                break
            return {"branch": ref.rsplit("/", 1)[-1], "sha": (sha[:7] or None)}
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def sketch_sources(ino_path, depth=3):
    """The .ino plus every quoted #include it reaches, resolved relative to the
    including file. Angle-bracket includes are libraries and are skipped.
    Returns [(label, text)], .ino first, each real file once."""
    root = os.path.dirname(ino_path)
    out, seen = [], set()

    def add(path, budget):
        real = os.path.normpath(path)
        if real in seen or not os.path.isfile(real):
            return
        seen.add(real)
        text = read(real)
        out.append((os.path.relpath(real, root).replace(os.sep, "/"), text))
        if budget > 0:
            for inc in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.M):
                add(os.path.join(os.path.dirname(real), inc), budget - 1)

    add(ino_path, depth)
    return out


def _match_span(text, pos, opener, closer):
    """The balanced opener..closer block starting at pos. Returns '' when it
    does not balance, rather than guessing at a truncated body."""
    depth, i, n = 0, pos, len(text)
    while i < n:
        c = text[i]
        if c == opener:
            depth += 1
        elif c == closer:
            depth -= 1
            if depth == 0:
                return text[pos:i + 1]
        i += 1
    return ""


def function_body(text, name):
    """Body of `... name(...) { ... }`. A trailing `;` in the arg list is excluded
    so a forward declaration never matches. Handles a body that sits on one line
    (apiReboot does)."""
    m = re.search(r'\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{', text)
    return _match_span(text, m.end() - 1, "{", "}") if m else ""


def _split_args(s):
    """Top-level commas only — a lambda argument carries its own commas, parens
    and braces."""
    args, depth, cur, i, n = [], 0, [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':                      # skip a string literal wholesale
            cur.append(c); i += 1
            while i < n:
                cur.append(s[i])
                if s[i] == '\\':
                    i += 1
                    if i < n:
                        cur.append(s[i])
                elif s[i] == '"':
                    break
                i += 1
        elif c in "([{":
            depth += 1; cur.append(c)
        elif c in ")]}":
            depth -= 1; cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    if cur:
        args.append("".join(cur).strip())
    return args


# The lock gates, most specific first. Which routes a claim closes is the fact
# that rots fastest, so it is derived from the handler body every run.
GATE_CALLS = ("webDenyLockedCfg", "webDenyLocked", "webWriteAllowed")

# Guards that refuse a route for a reason OTHER than an owner claim. Kept as a
# separate, named list so the two never get conflated: a client has to handle
# both, but they answer different questions. `wpFleetOff` is the panel's own
# check that this screen is in the MODE that drives dongles at all.
MODE_GATES = ("wpFleetOff",)


def _gate_of(body):
    for call in GATE_CALLS:
        if call + "()" in body:
            return call
    return None


def _mode_gate_of(body):
    for call in MODE_GATES:
        if call + "()" in body:
            return call
    return None


def grab_http_routes(sources):
    """Every `<server>.on("path", HTTP_x, handler[, uploadHandler])` plus the
    onNotFound fallback, in registration order, with the lock gate each handler
    actually calls."""
    bodies = {}                                    # named handler -> body text
    routes = []
    for label, text in sources:
        for m in re.finditer(r'(\w+)\.on\s*\(', text):
            call = _match_span(text, m.end() - 1, "(", ")")
            if not call:
                continue
            args = _split_args(call[1:-1])
            if len(args) < 3 or not args[0].startswith('"'):
                continue
            path = args[0].strip('"')
            method = args[1].strip()
            if not method.startswith("HTTP_"):
                continue
            handlers, gate, mgate = [], None, None
            for a in args[2:]:
                if re.fullmatch(r'\w+', a):        # a named handler
                    if a not in bodies:
                        for _lbl, t in sources:
                            b = function_body(t, a)
                            if b:
                                bodies[a] = b
                                break
                    body = bodies.get(a, "")
                    handlers.append(a)
                else:                              # an inline lambda
                    body = a
                    handlers.append("<lambda>")
                gate = gate or _gate_of(body)
                mgate = mgate or _mode_gate_of(body)
            routes.append({"path": path, "method": method, "handlers": handlers,
                           "server": m.group(1), "file": label, "gate": gate,
                           "mode_gate": mgate})
        for m in re.finditer(r'(\w+)\.onNotFound\s*\(', text):
            call = _match_span(text, m.end() - 1, "(", ")")
            if not call:
                continue
            routes.append({
                "path": "(onNotFound fallback)", "method": "ANY",
                "handlers": ["<lambda>"], "server": m.group(1), "file": label,
                "gate": _gate_of(call), "mode_gate": _mode_gate_of(call),
                # onNotFound is not method-scoped, so whatever it matches on the
                # URL it serves for any verb. Record those literals as found.
                "matches": re.findall(r'(?:startsWith|endsWith)\(\s*"([^"]*)"', call)})
    return routes


def grab_tcp_opcodes(sources):
    """`#define CMD_*` plus, per opcode, whether it is dispatched above or below
    the `wtokGateActive() && !authed` refusal inside handleTCPClient. Above =
    answerable without AUTH; below = refused while the dongle is locked."""
    out = []
    for label, text in sources:
        defs = re.findall(
            r'#define\s+(CMD_\w+)\s+(0x[0-9A-Fa-f]+|\d+)[ \t]*(?://[ \t]*(.*))?',
            text)
        if not defs:
            continue
        body = function_body(text, "handleTCPClient")
        gate = body.find("wtokGateActive() && !authed") if body else -1
        for name, val, doc in defs:
            pos = body.find("cmd == " + name) if body else -1
            out.append({
                "name": name, "value": val, "doc": (doc or "").strip(),
                "file": label, "dispatched": pos >= 0,
                "refused_while_locked": (pos > gate) if (gate >= 0 and pos >= 0) else None})
        break
    return out


def _cadence(text, fn, body):
    """How often the builder runs, taken from the source and labelled with where
    it was found. Two styles exist and both are real: the panel schedules itself
    inside the builder, the dongle schedules at the call site in loop(). A
    builder whose cadence lives at the call site would report "unknown" if we
    only read its body — and the cadence is the half of a beacon contract that
    tells a consumer how stale a flag can be, so it is worth chasing."""
    m = re.search(r'millis\(\)\s*\+\s*(\w+)', body)
    where = "builder"
    if not m:
        # the scheduling statement: mentions the call AND advances a deadline
        for stmt in re.findall(r'[^;{}\n]*\b' + re.escape(fn) + r'\s*\(\s*\)[^;]*;'
                               r'[^;{}]*millis\(\)\s*\+\s*\w+', text):
            m = re.search(r'millis\(\)\s*\+\s*(\w+)', stmt)
            if m:
                where = "call site"
                break
    if not m:
        return None
    token = m.group(1)
    if token.isdigit():
        return {"literal_ms": int(token), "found_at": where}
    d = re.search(r'#define\s+' + re.escape(token) + r'\s+(\d+)', text)
    return {"define": token, "ms": int(d.group(1)) if d else None,
            "found_at": where}


def _first_arg(text, call_name):
    """First argument of call_name(...), paren-aware — `IPAddress(255,255,255,255)`
    has commas of its own and a naive split truncates it to `IPAddress(255`."""
    out = []
    for m in re.finditer(r'\b' + re.escape(call_name) + r'\s*\(', text):
        inner = _match_span(text, m.end() - 1, "(", ")")
        if inner:
            args = _split_args(inner[1:-1])
            if args:
                out.append(args[0].strip())
    return out


def grab_beacon(sources):
    """The UDP discovery beacons — the ones that broadcast a JSON document.

    A builder is only included when it actually emits JSON fields. That is what
    makes it a discovery beacon: `sendStatusBeacon` on the dongles sends a packed
    binary PktStatus struct to one ESP-NOW peer, so listing it here as a UDP
    beacon with empty columns would describe something that does not exist."""
    out = []
    for label, text in sources:
        for fn in ("sendAliveBeacon", "sendStatusBeacon", "sendDirtyBeacon",
                   "pfSendBeacon"):
            body = function_body(text, fn)
            if not body:
                continue
            fields = re.findall(r'\\"([A-Za-z_]\w*)\\":', body)
            if not fields:
                continue
            out.append({
                "function": fn, "file": label,
                "fields": fields,
                "cadence": _cadence(text, fn, body),
                "targets": _first_arg(body, "beginPacket")})
    return out


# Disclosed filter: numeric #defines whose names name a port or a protocol
# timing, limit or size. Deliberately narrower than "every _MS define" — a
# screensaver timeout is a real define but it is not part of any API.
CONST_RE = re.compile(
    r'#define\s+(\w*(?:PORT|BEACON|ENROLL|TOKEN|OWNER|STALE|HELLO|CHANNEL'
    r'|SETTLE|PKT|PEERS)\w*)\s+(\d+)(?:UL|L|U)?\b')


def grab_constants(sources):
    out = {}
    for label, text in sources:
        for name, val in CONST_RE.findall(text):
            out.setdefault(name, {"value": int(val), "file": label})
    return out


def analyse_sketch(path):
    src = read(path)
    sources = sketch_sources(path)
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
        "sources": [lbl for lbl, _ in sources],
        "endpoints": {
            "http_routes": grab_http_routes(sources),
            "tcp_opcodes": grab_tcp_opcodes(sources),
            "beacons": grab_beacon(sources),
            "constants": grab_constants(sources),
        },
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
              "checkout": grab_git_ref(a.firmware_root),
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
