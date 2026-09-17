#!/usr/bin/env python3
"""Render docs/API-REFERENCE.md from extract_firmware_facts.py output.

The endpoint surface is the fact that rots fastest and hurts most when it is
wrong: it is what somebody builds a client against. So this enumerates it
straight from the extractor's JSON and refuses to say anything the JSON does
not carry. Where a fact could not be derived it prints an em-dash rather than
a guess.

Each surface is named explicitly on the command line, because both of our
worktrees hold every sketch and the two branches disagree about the same
sketch — one carries the ported dongle, the other the ported panel. Guessing
which checkout is authoritative for which board is exactly the mistake this
document exists to stop, so the caller says it and the output stamps it.

Usage:
    python render_api_reference.py \\
        --surface "Webby dongle - Super Mini=dongle.json:Gotek_SuperMini_Webby" \\
        --surface "Touchscreen panel - JC3248=panel.json:Gotek_JC3248" \\
        --out docs/API-REFERENCE.md
"""
import argparse, json, os, sys

GATE_TEXT = {
    "webDenyLocked": "yes - disk",
    "webDenyLockedCfg": "yes - config",
    "webWriteAllowed": "yes",
    None: "no",
}


def load(spec):
    """"Label=file.json:SketchName" -> (label, run, sketch)."""
    label, _, rest = spec.partition("=")
    path, _, sketch = rest.rpartition(":")
    if not (label and path and sketch):
        raise SystemExit("bad --surface %r; want 'Label=facts.json:SketchName'" % spec)
    with open(path, encoding="utf-8") as f:
        run = json.load(f)
    for s in run["sketches"]:
        if s["sketch"] == sketch:
            return label, run, s
    raise SystemExit("no sketch %r in %s" % (sketch, path))


def table(rows, head):
    out = ["| " + " | ".join(head) + " |",
           "|" + "|".join("---" for _ in head) + "|"]
    for r in rows:
        out.append("| " + " | ".join(r) + " |")
    return out


def md_escape(s):
    return s.replace("|", "\\|")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--surface", action="append", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    surfaces = [load(s) for s in a.surface]
    L = []

    L.append("# GTi API reference")
    L.append("")
    L.append("_Generated — do not hand-edit._ Every table below is read out of the firmware by")
    L.append("`.claude/skills/gotek-docs/scripts/extract_firmware_facts.py` and rendered by")
    L.append("`render_api_reference.py`. When the firmware and this file disagree, the firmware is")
    L.append("right and this file is stale; re-run the pair rather than correcting it by hand.")
    L.append("")
    L.append("This file **enumerates**; it does not teach. For how pairing and the owner-lock")
    L.append("actually work, read `PAIRING.md`; for the CONFIG.TXT keys, `CONFIG-REFERENCE.md`.")
    L.append("")

    # ── provenance ───────────────────────────────────────────────────────────
    L.append("## Where these facts came from")
    L.append("")
    L.append("Both worktrees contain every sketch, and the two branches do not agree about all of")
    L.append("them, so each surface below names the checkout it was read from.")
    L.append("")
    rows = []
    for label, run, s in surfaces:
        ck = run.get("checkout") or {}
        rows.append([md_escape(label), "`%s`" % s["sketch"], "`%s`" % (s.get("version") or "—"),
                     "`%s`" % (ck.get("branch") or "—"), "`%s`" % (ck.get("sha") or "—")])
    L += table(rows, ["Surface", "Sketch", "Version", "Branch", "Commit"])
    L.append("")

    # ── HTTP ─────────────────────────────────────────────────────────────────
    L.append("## HTTP")
    L.append("")
    L.append("Routes in registration order. **Closed by a claim** is derived per run from the lock")
    L.append("gate the handler body actually calls — `webDenyLocked` for the routes that change")
    L.append("what a running Amiga sees, `webDenyLockedCfg` for the ones that could take the disk")
    L.append("or the gate away. Both gates only bite when the device has at least one enrolled")
    L.append("owner *and* is on the shared LAN; on its own AP they are inactive.")
    L.append("")
    for label, _run, s in surfaces:
        routes = s["endpoints"]["http_routes"]
        L.append("### %s" % label)
        L.append("")
        if not routes:
            L.append("No HTTP routes in this sketch.")
            L.append("")
            continue
        # A mode guard is a different refusal from an owner claim, so it only
        # earns a column on the surfaces that actually have one.
        has_mode = any(r.get("mode_gate") for r in routes)
        rows = []
        for r in routes:
            handlers = ", ".join("`%s`" % h for h in r["handlers"])
            extra = ""
            if r.get("matches"):
                extra = " <br>matches " + ", ".join("`%s`" % m for m in r["matches"])
            row = ["`%s`" % r["method"].replace("HTTP_", ""),
                   "`%s`" % md_escape(r["path"]) + extra,
                   handlers, GATE_TEXT.get(r["gate"], r["gate"] or "no")]
            if has_mode:
                row.append("`%s`" % r["mode_gate"] if r.get("mode_gate") else "no")
            rows.append(row)
        head = ["Method", "Path", "Handler(s)", "Closed by a claim"]
        if has_mode:
            head.append("Refused in the wrong MODE")
        L += table(rows, head)
        L.append("")
        if has_mode:
            L.append("`wpFleetOff` answers 409 unless this screen is in the mode that drives")
            L.append("dongles, which is a different refusal from an owner claim: it says *this")
            L.append("device is not doing that job right now*, not *this device belongs to")
            L.append("somebody else*. A client has to handle both.")
            L.append("")
        n_gated = sum(1 for r in routes if r["gate"])
        L.append("%d routes, %d of which a claim closes. Served from `%s`."
                 % (len(routes), n_gated,
                    ", ".join(sorted({r["file"] for r in routes}))))
        L.append("")

    # ── TCP ──────────────────────────────────────────────────────────────────
    tcp = [(lbl, s) for lbl, _r, s in surfaces if s["endpoints"]["tcp_opcodes"]]
    if tcp:
        L.append("## TCP control port")
        L.append("")
        L.append("A disk arrives as a 4-byte big-endian size followed by the image. The escape")
        L.append("`0xFFFFFFFF` in that size field means the next byte is a command instead.")
        L.append("**Needs AUTH first** is derived from whether the opcode is dispatched above or")
        L.append("below the refusal line inside `handleTCPClient`, so it follows the code.")
        L.append("")
        for label, s in tcp:
            const = s["endpoints"]["constants"]
            port = const.get("TCP_PORT", {}).get("value")
            L.append("### %s%s" % (label, " — port %d" % port if port else ""))
            L.append("")
            rows = []
            for o in s["endpoints"]["tcp_opcodes"]:
                need = o["refused_while_locked"]
                rows.append(["`%s`" % o["value"], "`%s`" % o["name"],
                             md_escape(o["doc"]) or "—",
                             "yes" if need else ("no" if need is False else "—"),
                             "yes" if o["dispatched"] else "no"])
            L += table(rows, ["Byte", "Name", "Note from the source",
                              "Needs AUTH first", "Dispatched here"])
            L.append("")

    # ── UDP beacons ──────────────────────────────────────────────────────────
    bea = [(lbl, s) for lbl, _r, s in surfaces if s["endpoints"]["beacons"]]
    if bea:
        L.append("## UDP discovery beacons")
        L.append("")
        L.append("Read the cadence as part of the contract: a consumer that reads a flag out of a")
        L.append("beacon is reading state up to one full interval old, because nothing re-sends the")
        L.append("beacon early when that flag changes. **Sent to** is the first argument of each")
        L.append("`beginPacket` call exactly as the source writes it, variable names included.")
        L.append("Builders that send a packed binary struct rather than JSON are not beacons and")
        L.append("are not listed.")
        L.append("")
        for label, s in bea:
            L.append("### %s" % label)
            L.append("")
            rows = []
            for b in s["endpoints"]["beacons"]:
                c = b.get("cadence") or {}
                where = " (%s)" % c["found_at"] if c.get("found_at") else ""
                if "literal_ms" in c:
                    cad = "%d ms — a literal, no define%s" % (c["literal_ms"], where)
                elif c.get("define"):
                    cad = "`%s`%s%s" % (c["define"],
                                        " = %d ms" % c["ms"] if c.get("ms") else "",
                                        where)
                else:
                    cad = "—"
                rows.append(["`%s`" % b["function"],
                             ", ".join("`%s`" % f for f in b["fields"]) or "—",
                             cad,
                             ", ".join("`%s`" % t.strip() for t in b["targets"]) or "—"])
            L += table(rows, ["Builder", "JSON fields", "Cadence", "Sent to"])
            L.append("")

    # ── constants ────────────────────────────────────────────────────────────
    L.append("## Ports, timings and limits")
    L.append("")
    L.append("Numeric `#define`s whose names mention a port, a beacon, enrolment, a token, an")
    L.append("owner, a peer, staleness, a hello, a channel, a settle time or a packet. That is a")
    L.append("name filter over the protocol-facing constants, not the full define list — a")
    L.append("screensaver timeout is a real define but it is not part of any interface.")
    L.append("")
    names = sorted({n for _l, _r, s in surfaces for n in s["endpoints"]["constants"]})
    rows = []
    for n in names:
        cells = [("`%s`" % n)]
        for _label, _run, s in surfaces:
            c = s["endpoints"]["constants"].get(n)
            cells.append(str(c["value"]) if c else "—")
        rows.append(cells)
    L += table(rows, ["Define"] + [md_escape(l) for l, _r, _s in surfaces])
    L.append("")

    text = "\n".join(L) + "\n"
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d lines)" % (a.out, len(L)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
