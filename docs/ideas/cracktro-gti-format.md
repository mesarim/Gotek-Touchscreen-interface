# Proposal: custom cracktros as loadable `.gti` files, plus a cracktro screensaver

**Status:** proposal, for review before it is built.
**For:** Mez / upstream.
**From:** OMEGAWARE (Dimmy).

This is a draft for a GitHub issue. Nothing here has shipped, and nothing will be
sent as a PR until it has run on real hardware.

References below are by **symbol name**, not line number — this was written
against a slightly older base than 5.9.23 and the line numbers would not line up
in your tree.

---

## Where this came from

Discord **#your-builds**, 16 September 2026:

> "Oh, need to see if dimmy wants to make a 'custom cracktro' generator. So we
> can load a cracktro file, and you can have your own custom one on… and… need
> to add a 'turn off cracktro' option."

and, six minutes later, from `katzazakis`:

> "cracktro screensaver 🙂"

So: a way for users to make and load their own cracktros, and a cracktro as a
screensaver mode.

---

## 0. First — half of what you asked for already works

Before anything gets built, one finding worth having.

**`CRACKTRO=OFF` already works today.** It is in the config parser (`OFF` and
`NONE` both set the style to `-1`), and the boot call is guarded, so `-1` skips
the demo entirely. We checked the thing most likely to be broken: the numeric
fallback sits inside an `else{}`, so `"OFF".toInt() == 0` does **not** clobber
the `-1`. It is genuinely correct.

What is missing is not the feature — it is the ability to find it:

- the self-documenting `CONFIG.TXT` writer never mentions `OFF` or `NONE`. It
  documents `0=random` and `1..7` only, so a user reading their own config file
  has no way to learn the option exists;
- `CRACKTRO` does not appear anywhere in `webui.h`, so there is no control for
  it in the web UI. The key *is* in `passThrough[]` in `web_panel.h`, so the
  backend accepts it and a hand-made POST works — but nothing renders a widget.
  (`SSMODE` is in exactly the same position, so this is not specific to
  cracktro.)

So "turn off cracktro" is one line of documentation plus a button, not a
feature. We are happy to do both.

---

## 1. What a cracktro file is

The constraint that shapes everything: the ten styles are **code, not data**.
Each `crkXxx(float t)` draws its own text at hardcoded positions and colours.
There is no struct or table describing a cracktro, so a file format has to
invent the model.

We looked at how this problem was solved the first time round. The **RSI
Demomaker** (Red Sector Inc. + The Clumsy Creepers, Data Becker, 1991) let
non-programmers build demos, and its structural unit is the **"pattern"** — a
segment. A demo is patterns bolted together. Per pattern you pick one effect
from a fixed list and set what the manual calls **"parameters"**. You supply the
logos, fonts, music and scrolltext. You **cannot write new effects**, and logos
have hard maximum sizes.

That is the model we want, because it is the one that actually produced
thousands of real demos under exactly this kind of constraint.

### Proposed format

```ini
; /cracktro/dimmy.gti  (note: not "omega.gti" -- OMEGA is a built-in name and would win)
GTICRACK 1
NAME=Omegaware
AUTHOR=Dimmy

[PATTERN]
FX=COPPER
TIME=4000
TITLE=OMEGAWARE
SUB=* MEZ & DIMMY *
COL=FFE000

[PATTERN]
FX=LOGO
TIME=5000
LOGO=1
SCROLL=ON

[SCROLL]
TEXT=OMEGAWARE PRESENTS ... GOTEK TOUCHSCREEN INTERFACE ...

[LOGO 1]
W=96 H=24
FF00FF00C3A5...
```

Notes on the choices:

- **`GTICRACK 1`** magic and version on the first line, so the format can grow
  and junk is rejected immediately.
- **`[SECTION]` + `K=V` + `;` comments**, deliberately the same flavour as
  `CONFIG.TXT` so it is familiar and parses with the same style of code.
- **`FX=`** names an effect from the registry (see section 2). Names rather than
  numbers, because the file should be readable, and because a registry means the
  list can grow without renumbering anyone's files.
- **`[LOGO n]`** carries the bitmap as hex in the **existing 1bpp MSB-first,
  row-major wordmark layout**. That means the blit code already in the firmware
  works unchanged — and it means DENISE and WRANGLER can ship as example `.gti`
  files, so users start from real cracktros instead of a blank page.
- **One self-contained text file.** A cracktro fits in a single Discord message,
  can be pasted, mailed and diffed. That matters more than the few bytes a
  binary format would save: the Discord server is where these will actually be
  swapped.

### Proposed limits

To keep boot honest on an ESP32-S3:

| limit | value |
| --- | --- |
| file size | 32 KB |
| patterns per file | 4 |
| logos per file | 2 |
| logo size | 128 × 48 |
| scrolltext | 512 chars |

**These numbers are proposals, not measurements.** They are set to "boot must
stay fast" and we will confirm or move them on hardware. If your instinct says
they are wrong, say so — they are cheap to change now and annoying to change
after people have files.

---

## 2. Effects become pluggable

Right now an effect is a `crkXxx(float t)` function reached through a `switch`,
drawing hardcoded strings and colours. That makes adding an effect a change in
several places, and it makes an effect impossible to parameterise from a file.

We would replace the `switch` with a **registry**:

```c
struct CrkPattern {                 // one parsed [PATTERN] block
  uint8_t  fx;
  uint32_t timeMs;
  const char *title, *sub;
  uint16_t col;
  int8_t   logo;                    // index into the file's logos, -1 = none
  bool     scroll;
};

typedef void (*CrkFxFn)(float t, const CrkPattern& p);

struct CrkFx { const char *name; CrkFxFn fn; };

static const CrkFx CRK_FX[] = {
  { "COPPER",    fxCopper    },
  { "STARFIELD", fxStarfield },
  { "RASTER",    fxRaster    },
  { "PLASMA",    fxPlasma    },
  { "BOING",     fxBoing     },
  { "SYNTH",     fxSynth     },
  { "LOGO",      fxLogo      },
};
```

`FX=` resolves by name against this table. **Adding an effect becomes one row
and one function** — no switch to edit, no numbering to keep in sync, and no
change to the file format. Anyone who invents a new effect can plug it in.

Two things fall out of this that we think are worth having:

**The existing effects gain parameters.** `fxCopper(t, p)` reads `p.title`,
`p.sub` and `p.col` instead of hardcoding `"OMEGAWARE"`. That is the same
refactor that makes `.gti` possible at all — pluggable and parameterisable are
one job, not two.

**The built-in cracktros become pattern lists themselves.** Rather than having
one code path for built-ins and another for files, the built-in styles are
expressed as `CrkPattern` arrays and run through the same engine. One path, not
two — so the file path is exercised by every boot, not only by users who have a
file.

One honest exception: `RETRONAUT` decodes a PROGMEM JPEG rather than a 1bpp
wordmark, so it does not fit the pattern model without also giving the format a
colour-image concept. We would leave it as a special case for now rather than
inflate the format for one theme.

---

## 3. Where files live, and what happens when they are broken

- Files live in **`/cracktro/*.gti`**.
- **`CRACKTRO=CUSTOM`** picks a valid file from the folder at random.
- **`CRACKTRO=<name>`** uses `/cracktro/<name>.gti`.
- Every existing value (`OFF`, `NONE`, `0..7`, `OMEGA`, `DENISE`, `WRANGLER`,
  `RETRONAUT`) keeps working unchanged.

**Name collisions are resolved in favour of the built-ins.** Only a value that
is neither a built-in name nor a number is looked up as a file. So a user who
drops in `/cracktro/denise.gti` still gets the built-in DENISE theme from
`CRACKTRO=DENISE`; a file on the SD card can never silently hijack existing
behaviour.

**Failure rule, no exceptions:** no folder, no file, wrong magic, parse error, or
over a limit falls back to the **built-in cracktro**. Never a blank screen, never
a hang. The reason goes to serial so it can be diagnosed remotely.

---

## 4. The screensaver

New `SSMODE=CRACKTRO`, making the cycle four-way:
**SLIDES → BOUNCE → MATRIX → CRACKTRO**.

The good news is that most of this already exists. `LOOP=1` already makes
`drawCracktro` run indefinitely until a touch, which is screensaver semantics;
it has just never been wired to the saver. And cracktro themes already feed the
saver — `CRACKTRO=DENISE` / `WRANGLER` / `RETRONAUT` already arm it.

Three gaps we found by reading, which we would fix:

1. **Servicing.** `runScreensaver`, `runMatrixRain` and `runSlideshow` each call
   `webPanelService()` in their loop. `drawCracktro` does not call it at all. As
   a saver it would freeze the web UI for as long as it runs.
2. **Restore.** The savers end by resetting touch state and calling
   `drawFullUI()` / `drawCarousel()`. `drawCracktro` leaves a black screen.
3. **Time base.** `float t = millis() - startMs`. Fine for a six-second splash.
   A saver running for hours hits the 24-bit mantissa of `float` (~16.7M), and
   past roughly 4.6 hours the 6 ms steps stop registering and **the animation
   freezes**. The boot splash never reaches this.

The fix for all three is to split `drawCracktro` into
**`drawCracktroFrame(pattern, t)`** plus two callers — the boot splash
(unchanged behaviour) and the saver (servicing, restore tail, and a time base
that wraps every ~10 minutes on a multiple of the scroll period so the scroller
does not visibly jump).

---

## 5. The tooling

A browser-based builder, in the shape of the existing
[6x8 bitmap font generator](https://dimitrihilverda.github.io/6x8-bitmap-font-generator/):
pattern editor, palette editor, and a pixel editor for the logo, emitting a
`.gti` with a download button and a copy button for Discord. RSI shipped exactly
two editors — palette and text — so this is the same toolkit, rebuilt.

One decision worth flagging, because it affects the firmware: **the builder's
preview runs the real engine.** We compile `drawCracktroFrame()` to WebAssembly
and run it behind a `gfx_*` shim onto a canvas, rather than reimplementing the
effects in JavaScript. A JS preview would be a second engine that drifts from the
first the moment anyone tweaks an effect. This way the preview is identical by
construction, and the seam it needs is the same split the screensaver already
requires.

It also means a **new effect gets its preview for free**: add a row to `CRK_FX`,
rebuild the WASM, and the builder can show it. No second implementation to write.

Three things that would silently break that exactness, for the record: the fixed
`gW=480 / gH=320`, the shared `font6x8[95][6]` glyph data, and `CRK_RGB` being
RGB565 — a canvas is 8 bits per channel, so the preview has to quantise
identically or a colour picked in the tool lands slightly off on glass.

---

## What we would like from you

**Feedback on the format in section 1 and the registry in section 2**,
particularly the limits — you asked for this to be reviewable before it is built,
and the file format is the part that is expensive to change later.

Nothing else is blocking. We are building and testing this independently, on a
branch cut fresh from `main`, and nothing comes your way until it has run on a
real panel.
