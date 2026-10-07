**Re: the lean access model — agreed, with one row missing from the table.**

Your instinct is right and we'd rather build your version than defend ours: put the defence where
the exposure is, and the exposure is the AP, not the frame. No HMAC.

**First, a concession.** We'd argued that the "neighbour's kid with a Chromebook" case needed our
lock on the home LAN. That was wrong — on a WPA2 home network he isn't on the LAN at all. He is
only ever reachable through an AP with a password everyone in the scene knows, which is exactly
what `AP_ENABLE=0` plus custom credentials closes. Your mechanism covers that case; ours was
answering a threat that doesn't exist there.

**Two of your three new items already exist on `webby-lock-161`.** Custom AP name and password
are in (`/APCFG.TXT`, `APNAME=` / `APPASS=`, clamped 24/32 chars, WPA2 with fallback, set from the
portal's "AP security" card, and deliberately *not* cleared by a WiFi-creds wipe so the AP stays
protected). What's missing is `AP_ENABLE` and the ESP-NOW commands to drive it from the GTi. Happy
to take those, and to use your key names.

**And we're withdrawing a proposal we sent an hour ago.** We were going to make a locked dongle
stop broadcasting its 51703 beacon and unicast it to its owners instead. With the AP off there is
no STA association, so there is no beacon to hide — your model solves it by removing the surface
rather than concealing it. Simpler. Scrap ours.

---

**The missing row: at a club, with internet.**

Our library lives on a remote WebDAV server, so at a club we want the venue's network *and* the
dongles. ESP-NOW has no internet, so the "club = ESP-NOW, AP off" row doesn't cover it. There are
only three ways to have both, and two of them are bad:

| | |
|---|---|
| **Venue WiFi, everything on it** | Works, but it's the shared-LAN case: ten dongles in your list, accidental flings. |
| **Panel on venue WiFi, dongles on ESP-NOW** | Not possible — ESP-NOW and STA are mutually exclusive on the panel's radio, which is why MODE went 3-way in 5.9.12 and why #26 is parked. |
| **Phone hotspot** | Internet *and* a network only your own devices are on. |

The third one is the answer, and it's already in your document — you mention pointing the dongle
at a phone hotspot, you just didn't line it up against the WebDAV case. It gives internet for the
remote library, a private LAN with no strangers on it, and the fleet console and mDNS behave
exactly as they do at home. That is the configuration we should be recommending for a club day,
not the venue's WiFi.

**One thing to test before anyone relies on it:** many phone hotspots isolate their clients — each
device reaches the internet but not the others. If your phone does that, the panel and the dongle
sit on the same hotspot and cannot see each other, and you'd find out in the room. We'll test it
here and report. Mobile data is a non-issue for the disks themselves (880 KB) but browsing a large
library with cover art is a different bill.

---

**Where that leaves our lock, honestly.** It is not security, and we'll stop presenting it as
such. Two things it does:

1. **Fleet hygiene.** With two or three screens and several dongles, "don't show me dongles that
   aren't mine" and "don't let another screen fling to my Amiga mid-game" is an accident
   preventer, not a threat mitigation. That value doesn't depend on there being an adversary.
2. **The fallback for the one row nobody can design away:** the venue's WiFi is your only
   internet — no signal, no data, or a place that only offers their own network. There you are on
   a shared LAN whether you like it or not, and the lock is what's between your disk and
   somebody's stray fling.

We also narrowed it today, because the wide version cost us more than it bought: a claimed dongle
now only refuses **upload, eject and unload** over the shared LAN. Config, OTA, `/espnow`, reboot
and `/setap` are open again — those are what you reach for when something is already wrong, and
locking them stranded us three times in one day, including a dongle we couldn't switch to ESP-NOW
over the network. And the whole thing stays opt-in: a dongle is completely open until someone
claims it with a physical BOOT tap, so a home user never meets it.

---

**Your open question — explicit control, with locking defaulting the AP off.** Your lean instinct
is the right one. One gesture doing two things is exactly what bit us today; keep the AP switch
its own control, and have a claim *default* it off rather than force it.

**And the real risk in your plan is the one you already named.** With no softAP and no STA
association, ESP-NOW has nothing pinning the radio — `esp_wifi_set_channel(6, …)` has to be
explicit, and if it's wrong the failure is silent and looks fine at boot. We have two SuperMinis
and a XIAO on the bench and can test that for you: AP off, panel over ESP-NOW, pair and fling,
then power-cycle both ends and do it again.

One small ask while we're in here: the #27 capability probe uses a bare `7` with no `#define`,
which collides with `CMD_ENROLL 0x07` from the contract you signed off on the 13th. `0x0A` and a
named constant clears it in both directions.
