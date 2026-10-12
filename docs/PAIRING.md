# Wireless Pairing (Super Mini / XIAO dongle)

The GTi loads disks to a **wireless dongle** (Super Mini or XIAO) over WiFi. Before it
can, the touchscreen and the dongle have to be **paired**. How that works depends on
which firmware the dongle is running.

> **About the lights:** most Super Mini / XIAO boards have **no status LEDs fitted and
> no screen**, so don't rely on colours — go by the timings below. Every step works blind.

---

## Which firmware am I on?

- **Release / stable dongle** (Super Mini ≤ v3.4.x, XIAO ≤ v3.6.0): the dongle
  **auto-pairs** — it accepts the first GTi that enrolls it. No button needed.
- **Beta dongle — "owner-lock"** (Super Mini v3.5.0+, XIAO v3.6.1+): the dongle only
  obeys GTis you **deliberately enrol** with a BOOT-button hold. It ignores everyone
  else and doesn't advertise itself.
- **Webby dongle** (Super Mini / S3-Zero Webby 1.6.5+): open until its first pairing, then
  locked to its owners like the beta dongle. From **Webby 1.6.13** the lock also covers the
  disk itself — see [Webby: the lock and the dongle's own Wi-Fi](#webby-the-lock-and-the-dongles-own-wi-fi).

The GTi side is the same **SCAN DONGLES** menu on both; only the dongle's willingness to
be enrolled differs. (On the trial-UI JC beta the menu lives under **CONFIG**; on stable
builds it's under **INFO**. Same thing, renamed.)

---

## Release / stable firmware — auto-pair

1. Flash the dongle and plug it into the Gotek's USB port; power the Gotek on.
2. On the GTi, open **INFO/CONFIG** and set **MODE = WIRELESS**.
3. Tap **SCAN DONGLES**. After a ~4-second scan, tap your dongle, then **USE**.
   *(Very old builds show a single **PAIR NOW** button instead — it grabs the first
   dongle it hears.)*
4. Done — the GTi remembers it. To switch dongles later, just **SCAN** and **USE** another.

That's it: no button-holding on the dongle.

---

## Beta firmware — owner-lock

The dongle only listens to GTis it has been enrolled to (up to **4**), so nobody at an
event can pair with or steal it.

### Pair a dongle (first time)
1. **On the dongle:** press and **hold BOOT for about 5 seconds**, then let go. This
   opens a 30-second pairing window.
2. **On the GTi:** **SCAN DONGLES**, tap your dongle, tap **USE**. It shows *PAIRED*.

A brand-new dongle will **not** pair on its own — the 5-second BOOT hold is always
required for the first pairing. That's the lock doing its job.

### Add a second GTi to the same dongle
Repeat the pairing steps from the other GTi: BOOT-hold ~5 s on the dongle, then
**SCAN → USE** on the second screen. Either GTi can then drive the dongle (one at a time).

### Forget / unpair a dongle
- **Easy way (from the GTi):** **SCAN DONGLES**, tap the dongle, tap **DEL**.
  This tells the dongle over the air to drop this GTi **and** forgets it on the GTi side.
- **On the dongle:** **hold BOOT for a full 15 seconds** — at 15 s it wipes *all* owners
  automatically (no release needed). Verify from the GTi that it dropped you.

### Rename a dongle
On the JC (3.5"/4.3") screens: **SCAN DONGLES → select → RENAME**, type a friendly name.
*(Name editing isn't on the 7-inch screens yet.)*

---

## Webby: the lock and the dongle's own Wi-Fi

A Webby ships **open**: the first GTi that scans it becomes its owner, no button needed
(1.6.5). After that it behaves like a beta dongle: a second screen needs the BOOT-hold, or
**SHARE** from an owner screen (2 minutes, one more screen). Up to 4 owners.

Until 1.6.12 that lock only guarded *pairing* and *eject*. The disk itself travels over the
dongle's own Wi-Fi (`GotekOMEGA-XXXX`, password `gotek1234` on every dongle) and nothing
there checked who was sending, so at a club day any screen or phone could put a disk in
any dongle. **Webby 1.6.13** closes that, in ESP-NOW mode only (on your home Wi-Fi the
Wi-Fi access is the lock):

- **While the dongle has an owner and still uses the default password**, only owner
  screens get onto its Wi-Fi, onto port 3333 and onto the parts of its page that change
  something. Anyone else is dropped from the Wi-Fi, refused on 3333, or told *locked* by the
  page. A BOOT-hold of ~5 s (30 s window) or SHARE from an owner screen (2 min) opens the dongle
  to anyone for that long — join, page, even a disk — which is how you get a phone onto a dongle
  a screen already owns.
- **Give the dongle its own password** (its page → *Wi-Fi* tab → *This dongle's own Wi-Fi*,
  or from an owner screen). From then on the password is the lock: phones and PCs that know
  it are welcome, nobody else gets on, and an owner screen learns the password by itself when
  it pairs. You can rename the Wi-Fi there too (screens find the dongle by its radio address,
  so a rename breaks nothing).
- **Away from home** (home Wi-Fi saved, but no router in sight, so the dongle fell back to
  ESP-NOW): a dongle with no owner does **not** take the first screen that scans. Hold BOOT
  ~5 s to pair it, like a beta dongle.
- **BOOT 15 s** wipes the owners *and* puts the name and password back to
  `GotekOMEGA-XXXX` / `gotek1234`.

---

## BOOT-button cheat-sheet (beta dongles)

| Hold BOOT for… | Result |
|---|---|
| ~5–15 s, then release | Opens the 30-second **pairing** window (then SCAN → USE on the GTi) |
| ~15 s (auto) | **Wipes all owners** — fires by itself, no release needed (Webby 1.6.13: also restores the default Wi-Fi name and password) |
| under 5 s | Nothing (safe — ignores accidental taps) |

Changed your mind mid-hold before 15 s? Unplug or tap reset — nothing is wiped until the
15-second mark.

---

## Troubleshooting

**My dongle doesn't show up in SCAN DONGLES.**
On a *beta* dongle that means it's locked to a different GTi, or never enrolled.
BOOT-hold it ~5 s to open its pairing window, then scan again. On a *release* dongle,
check it's powered from the Gotek's USB and that both ends share SSID `GotekOMEGA`.

**The BOOT-hold wipe didn't do anything.**
Hold the **full 15 seconds continuously** — it fires on its own at 15 s and there's no
light to tell you. If unsure, hold longer, or just use **DEL** on the GTi instead.

**The LEDs do nothing / show the wrong colour.**
Expected — most boards have no status LEDs and no screen. Ignore the lights; use the timings.

**I paired but the screen "half went back" to the game list.**
Fixed in current firmware (JC 5.6.9+). Update to the latest build.

**Can a stranger pair with or steal my dongle?**
On a beta (owner-lock) dongle, no — nothing enrols without the physical BOOT-hold, and a
locked dongle ignores any GTi that isn't already an owner. On a Webby, from 1.6.13, the same
goes for the disk path: a stranger's screen or phone cannot put a disk in a dongle that has
an owner, and once the dongle has its own Wi-Fi password only devices that know it can.

**My phone can't join the dongle's Wi-Fi any more (Webby 1.6.13).**
The dongle has an owner screen and still the default password, so it only lets owners on.
Hold BOOT ~5 s, join within 30 s, and give the dongle its own password on the *Wi-Fi* tab;
after that the password is what counts.

---

_Owner-lock: Super Mini v3.5.2 / XIAO v3.6.1 · paired-dongle menu (USE/RENAME/DEL):
JC 5.7.x, 7-inch v4.13.0 (DEL only)._
