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

## Webby dongles — the home-WiFi lock

A **Webby** dongle (Super Mini `Webby-1.6.x`, XIAO `Webby-1.5.x`) can leave its own
access point behind and join **your home WiFi**, so any screen on the network can fling a
disk to it. That is lovely at home and alarming at a club day: without a lock, *every*
screen on that network can drive *every* dongle. So Webby has a second lock, alongside the
ESP-NOW one above — same idea, different road.

The two locks are independent and can both be set:

| | ESP-NOW lock (above) | WiFi lock (this section) |
|---|---|---|
| Protects | the dongle's own AP + ESP-NOW link | flinging over your home network (TCP 3333) |
| Remembers | up to 4 GTi **MAC addresses** | up to 4 screen **tokens** |
| Stored in | `/XIAO_CONFIG.TXT` | `/WOWNERS.TXT` |
| Opened by | BOOT held ~5 s (30 s window) | BOOT **tapped** (60 s window) |

### Claim a dongle for your screen
1. **On the dongle:** a **short tap** of BOOT (under 3 seconds) opens a **60-second**
   claim window. If the dongle has an OLED it says *PAIRING OPEN*; on a bare board, trust
   the clock.
2. **On the screen:** **INFO → FLEET**, find the dongle's row, tap **CLAIM**. It answers
   *Claimed*, and the window closes immediately — one tap, one claim.

From then on the dongle **only accepts work from screens it knows**. A fling, an eject, a
save-read or a rename from anyone else is refused, and the dongle **hides itself** from
their fleet list entirely — so a stranger doesn't even see a target to aim at.

### What a locked dongle refuses
Everything that changes it or reads your data, on both roads in:

- over the network: the disk itself, `GET_SAVE`, `EJECT`, `EJECT_FORCE`, `SET_NAME`
- over its **web page**, while it is on your home WiFi: uploading a disk, ejecting,
  changing the WiFi or AP settings, saving config, rebooting — **and firmware updates**

Reading stays open: its status page still answers, so a screen can still see what is in the
drive. And that firmware-update line is deliberate rather than an oversight — an OTA is the
most complete write there is, so a locked dongle cannot be re-flashed over the shared
network at all. Unclaim it first, or use its own access point.

### Release a dongle
- **From the screen that owns it:** **INFO → FLEET →** the dongle's row → **UNCLAIM**.
- **From a screen that has forgotten it** (you reset the card, or claimed it from the web
  page on an older build): **INFO → RELEASE LOCKS**. The screen offers its token to every
  locked dongle it can see; a dongle only lets go if that token really is one of its owners,
  so this can never open somebody else's lock.
- **On the dongle, with nothing else to hand:** hold BOOT for **10 seconds**. That wipes the
  saved WiFi *and* every owner, and the dongle comes back as a fresh one on its own AP.

Both screen releases prove themselves the way a fling does: the screen shows its token first
and only then asks to be forgotten. A dongle that does not know that token answers *locked to
another screen*, and RELEASE LOCKS counts it as *Not ours*.

### Give the setup AP its own name and password
While the dongle is on its own access point, open its page and find **AP security**: set a
name (up to 24 characters) and a WPA2 password (8–32). After the reboot, only somebody who
knows both can join the dongle's AP to reconfigure it. The default stays `GotekOMEGA` /
`gotek1234` with the `-<mac>` suffix, so two dongles in one room are still tellable apart.

---

## BOOT-button cheat-sheet (beta dongles)

| Hold BOOT for… | Result |
|---|---|
| ~5–15 s, then release | Opens the 30-second **pairing** window (then SCAN → USE on the GTi) |
| ~15 s (auto) | **Wipes all owners** — fires by itself, no release needed |
| under 5 s | Nothing (safe — ignores accidental taps) |

Changed your mind mid-hold before 15 s? Unplug or tap reset — nothing is wiped until the
15-second mark.

### …and on a Webby dongle that has joined your home WiFi

| Hold BOOT for… | Result |
|---|---|
| a **short tap** (under 3 s) | Opens the 60-second **claim** window (then CLAIM on the screen) |
| ~3–10 s | Back to **ESP-NOW** + its own AP, saved WiFi kept |
| ~10 s or more | Wipes the saved WiFi **and every owner** — the full reset |

On a Webby dongle still in ESP-NOW mode, the table above this one applies instead: the
dongle only uses the WiFi timings once it has actually joined a network.

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

**A Webby dongle vanished from my screen's fleet list.**
Almost always the lock doing its job: it is claimed by a different screen. Either claim it
yourself (short BOOT tap, then **CLAIM**), or — if it is *your* dongle and your screen has
simply forgotten it — **INFO → RELEASE LOCKS**.

**My screen says "locked to another screen".**
That is the dongle refusing a fling from a screen it doesn't know. Nothing is broken.

**UNCLAIM says *Failed*, or RELEASE LOCKS says *Not ours*, for a dongle that is mine.**
Fixed in current screen firmware (24 September 2026). Older fleet builds asked a claimed
dongle on your home WiFi to forget them without showing their token first, and the dongle
refuses that. Update the screen and try again. With no update to hand, hold BOOT on the
dongle for 10 seconds — that also forgets its WiFi.

**I can't update a Webby dongle's firmware over WiFi any more.**
Because it is claimed — a locked dongle refuses every write over the network, firmware
included. UNCLAIM it, update, then claim it again.

**Can a stranger pair with or steal my dongle?**
On a beta (owner-lock) dongle, no — nothing enrols without the physical BOOT-hold, and a
locked dongle ignores any GTi that isn't already an owner.

---

_WiFi owner-lock (Webby): Super Mini `Webby-1.6.3-lock` / XIAO `Webby-1.5.3-xiao-lock`, screen side JC 5.9.x `gti-web r22` — on branches `webby-lock-161` and `panel-fleet-alwaysweb`, not yet in a release build._

_Owner-lock: Super Mini v3.5.2 / XIAO v3.6.1 · paired-dongle menu (USE/RENAME/DEL):
JC 5.7.x, 7-inch v4.13.0 (DEL only)._
