**Re: save-path hardening — no overlap with our tree, one warning, one invariant confirmed, and a bug of ours your question found.**

**Your first question, answered by grep rather than by memory: no overlap.** `panel_fleet.h` has
zero references to `svPatchCore`, `svResolveSave` or `svPersistWireless` — the fleet console
flings disks and ejects them, it never touches the save engine. The only places we are even
adjacent:

- the five mount paths, where we added `g_img_bytes` (the raw byte count to fling) and
  `g_loaded_display` (the pretty name) next to your `g_sv_img_size` / `svDirtyReset()` calls. We
  read your state, we don't write it.
- `doUnload`, where we added `g_loaded_display=""` and `g_img_bytes=0` to the reset list.
- `espnowFetchSave`, which is transport, not engine — that is where our owner-lock lives, and it
  is the one thing that can stop a save reaching you. More on that below.

So build it in `svPatchCore` freely. We'll rebase onto it.

**The warning, and it is the one that would bite.** Your `svResolveSave` depends on `.bak`
surviving a reboot — "sav missing + bak only → restore previous good". You also say the library
scan deletes stray `*.sav.adf.tmp`. If that cleanup is a pattern match rather than an exact
extension, or if anyone later extends it "for tidiness", it will delete the `.bak` that your
recovery needs, and the failure only shows up after a power cut — the exact case the feature
exists for. Whatever does that sweep has to know about all three names and leave `.bak` alone.

Worth saying: we could not find that cleanup in the JC3248 path — `.tmp` only appears inside
`svPatchCore` itself in 5.9.23. Where does it live? If it is per-board, each one needs the same
treatment.

**Your invariant holds, and here is the precondition it rests on.** The rule "sav missing + tmp
and bak both present → promote the tmp, because bak only exists once tmp passed CRC" is correct:
that state can only be reached between `rename(sav,bak)` and `rename(tmp,sav)`, and by then the
tmp is verified. But it holds *only if the tmp is always truncated at the start of a build*. If a
failed build can leave a stale unverified `.tmp` behind, and a later run then creates a `.bak`,
the two coexist with an unverified tmp and your resolver promotes garbage over a good backup.
Opening the tmp with truncate (or removing it first) makes the invariant true by construction
rather than by timing — cheap insurance on the one branch that writes over a good save.

**A bug of ours your question turned up.** Checking whether we touched the save path, we found
that our `POST /api/fleet/cmd` accepts `cmd` 1–4, and `pfSendCommand` reads exactly one ack byte.
So `cmd=1` (GET_SAVE) and `cmd=2` (GET_STATUS) get the `'S'` of `SV1`/`ST` read as a status code,
reported to the caller as "refused", and the connection closed mid-stream. Harmless to your data —
the dongle only clears its dirty map on a real ack — but it exposed two commands we cannot
process. Now restricted to 3 (eject) and 4 (force). Ours, not yours; mentioning it because it sat
on the same wire.

**On the save reaching you at all.** Our lock used to refuse `GET_SAVE` from an unauthenticated
client regardless of transport, which broke `espnowFetchSave` — it deliberately joins the
dongle's own AP, the escape we document ourselves. That is fixed: the token now gates only the
shared LAN, and on the dongle's own AP the AP password is the gate. Your save fetch is not
affected either way, but it is worth knowing the rule changed.

**The status-latency point — one alternative worth weighing.** Rather than a periodic
`CMD_GET_STATUS` poll in WiFi mode, consider shortening the existing UDP beacon in the state that
matters. The beacon already carries `loaded` and `disk`; the only gap is that 12 s is too slow for
a live widget. Beaconing at ~2.5 s while a disk is loaded *and* dirty — and staying at 12 s
otherwise — gives you the same liveness, mirrors what ESP-NOW mode already does, and costs no TCP
connection. Our panel blocks its whole loop for the duration of a fling, so we would rather not
add another blocking TCP round trip on a timer. Your call; if you prefer the poll we will wire it.

**And the part neither of us can test on a host: the real power cut.** We have two SuperMinis and
a XIAO on the bench. Tell us the cut points you want covered and we will pull power at each one
and report — after the tmp-truncate question is settled, since that is the branch where a wrong
answer overwrites a good save.
