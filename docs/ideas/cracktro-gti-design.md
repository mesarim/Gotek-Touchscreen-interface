# Custom cracktro's (`.gti`) + cracktro-screensaver — ontwerp

**Datum:** 2026-09-16
**Branch:** `cracktro-gti` (vers vanaf `origin/main` @ `8f289b4`)
**Status:** ontwerp goedgekeurd door Dimmy, nog niet geïmplementeerd

Alle regelverwijzingen in dit document gaan over
`firmware/Gotek_JC3248/Gotek_JC3248.ino` op `origin/main` @ `8f289b4`.
Let op: de fleet-branch `panel-fleet-5923` heeft andere regelnummers.

---

## 1. Aanleiding

Discord **#your-builds**, 16 september 2026, 10:33, Mesarim:

> "nice nice. Oh, need to see if dimmy wants to make a 'custom cracktro'
> generator. So we can load a cracktro file, and you can have your own custom
> one on… and… need to add a 'turn off cracktro' option."

En om 10:39, `katzazakis`:

> "cracktro screensaver 🙂"

Twee features dus: **(A)** eigen cracktro's die de gebruiker zelf maakt en
laadt, en **(B)** een cracktro als screensaver-stand.

---

## 2. Wat er al staat (geverifieerd, niet aangenomen)

### 2.1 De engine

`drawCracktro(int style)` (**:1905**) is één `while(true)` met een vaste vorm:
`gfx_fillScreen` → effect-functie → `crk_scrollerT()` onderaan → `gfx_flush()`
→ `delay(6)`.

De tien stijlen zijn **code, geen data**. Elke `crkXxx(float t)` tekent zijn
eigen tekst op hardgecodeerde posities en kleuren, bijvoorbeeld
`crk_txtC(gW/2,34,"OMEGAWARE",4,...)`. Er bestaat geen struct of tabel die een
cracktro beschrijft. **Er is dus geen datamodel om in te vullen — dat maken we
in dit ontwerp.**

Wat wél al data-achtig is, en niet toevallig precies wat gebruikers willen
veranderen:

- de scrollteksten (`const char*`, o.a. `CRK_SCROLL`);
- de wordmark-bitmaps (**:1643**), **1bpp, MSB-first, row-major**.

### 2.2 `drawCracktro` als screensaver — de hoofdvraag, beantwoord uit de code

| vraag | antwoord |
| --- | --- |
| returnt het? | **ja** — eindigt met `gfx_fillScreen(TFT_BLACK); gfx_flush();` |
| touch-onderbreekbaar? | **ja** — `Touch_ReadFrame()` bovenaan de lus, 500 ms drain, `break` |
| blokkeert het? | ja — stopt na 6000 ms, **tenzij `g_loop_cracktro`** |

`LOOP=1` (CONFIG.TXT) zet `g_loop_cracktro` en laat de cracktro **oneindig
doorlopen tot een tik**. Dat is al screensaver-semantiek; het is alleen nooit
op de screensaver aangesloten.

Er is ook precedent dat een cracktro-thema de saver voedt: `g_cracktro==8/9/10`
zetten `g_ss_have=true`, en `runScreensaver()` kijkt naar `g_cracktro` bij het
kiezen van zijn tak. "De cracktro voedt de saver" is dus een bestaand patroon in
dit bestand, geen nieuw idee.

### 2.3 Drie gaten voor hergebruik als saver

1. **Servicing.** `runScreensaver`, `runMatrixRain` en `runSlideshow` roepen elk
   `webPanelService()` aan in hun lus; `drawCracktro` doet dat **nul** keer
   (geteld). Als saver zou hij de web-UI bevriezen zolang hij draait.
   *Op `panel-fleet-5923` staat daar ook `pfService()`/`pfWorker()`; op
   `origin/main` bestaan die niet. Wij richten ons op `webPanelService()`.*
2. **Restore.** De savers eindigen met `g_touch_active=false;
   g_touch_release=0; g_last_touch_ms=millis();` gevolgd door `drawCarousel()`
   of `drawFullUI()`. `drawCracktro` laat een zwart scherm achter.
3. **Tijdbasis.** `float t = (float)(millis()-startMs)`. Prima voor zes
   seconden. Een saver die uren draait loopt tegen de 24-bits mantisse van
   `float` (~16,7 miljoen); voorbij ±4,6 uur registreren stappen van 6 ms niet
   meer en **bevriest de animatie**. De boot-splash raakt dit nooit.

### 2.4 `CRACKTRO=OFF` — half af

- **De parser werkt echt.** De numerieke fallback zit binnen een `else{}`, dus
  `OFF` → `-1` wordt níet overschreven door `"OFF".toInt()==0`. Dit had stuk
  kunnen zijn; dat is het niet.
- **De zelf-documenterende CONFIG.TXT noemt `OFF` nergens** — alleen
  `0=random` en `1..7`.
- **`CRACKTRO` komt 0 keer voor in `firmware/shared/webui.h`** (3721 regels).
  Er is geen bedieningselement. De API accepteert de sleutel wel via
  `passThrough[]` in `web_panel.h`, dus een POST werkt — maar klikkend vindt
  niemand het. `SSMODE` staat er net zo bij; het is geen cracktro-specifieke
  omissie.

**Conclusie:** het mechanisme is er, de vindbaarheid niet. Mez' "turn off
cracktro" is één regel documentatie plus een knopje, geen feature.

### 2.5 Randvoorwaarden op de hardware

- SD is gemount op **:4529/:4539** en config geladen vóór `drawCracktro` op
  **:4556**. **Een cracktro-bestand van SD lezen bij boot kan dus.**
- `retroLogoLoad()` leest uit **PROGMEM**, niet van SD — er is dus nog géén
  precedent voor SD-assets in een cracktro. Het bewijst wél dat alloceren en
  decoden *tijdens* de cracktro werkt, inclusief `ps_malloc` (PSRAM
  beschikbaar).

---

## 3. Historisch precedent: RSI Demomaker

De Red Sector Demo Maker (Red Sector Inc. + The Clumsy Creepers, uitgegeven door
Data Becker vanaf 1991) loste exact dit probleem op voor niet-programmeurs.

Het structurele begrip is **"pattern"**: een segment. Een demo is patterns die
aan elkaar geknoopt worden — de handleiding tekent letterlijk
`LOGO → VECTOR GRAPHIC → GREETS`. Per pattern kies je één effect uit een
**vaste lijst** (Biglogo, Littlelogo, Vectorgfx, Vectorballs, SinusScroller,
Zoomtext, Plasma, Textscr32/16/8, Lineffect, Bobeffect, Pixeleffect …) en stel je
**"parameters"** in — de handleiding gebruikt dat woord. Voor een logo:
`Logonumber`, `Begineffect`, `Endeffect`, `Mode`.

De gebruiker levert **logo's, fonts, muziek en scrolltekst**. De gebruiker kan
**geen nieuwe effecten schrijven**. Logo's hadden **harde maximale afmetingen**
(littlelogo max 54×320). Ingebouwd zaten precies twee editors: een
**palette-editor** en een **teksteditor**.

Drie gevolgen voor dit ontwerp:

1. "Ingebouwde effecten + je eigen logo, tekst en kleuren" is geen compromis om
   ESP32-werk te vermijden — het is het model dat duizenden échte demo's heeft
   opgeleverd.
2. Vaste maxima zijn geen armoede maar wat een vaste engine nodig heeft.
3. Een demo *beweegt door delen heen*. Onze `drawCracktro` is één scherm, één
   effect, zes seconden — dat is één pattern. Daarom nemen we de patternlijst
   over.

Bronnen: [manual (Goldstar PD)](http://www.classicamiga.com/images/stories/jreviews/software/R/manuals/rsi_demo_maker%5Bmanual%5D%5Btext%5D.txt),
[pouët](https://www.pouet.net/prod.php?which=12558),
[Demozoo](https://demozoo.org/groups/4737/).

---

## 4. Besluiten

| # | Besluit | Reden |
| --- | --- | --- |
| 1 | Eén formaat, drie consumenten (web-tool, firmware, generator-skill) | Het formaat is de ruggengraat; één spec, drie implementaties |
| 2 | Reskin + eigen 1bpp logo, geen scriptbare DSL | RSI-model; precedent bestaat al in de wordmarks |
| 3 | Map `/cracktro/` met meerdere bestanden | Sluit aan op het bestaande `/screensaver`-patroon; `CRACKTRO=0` (random) pikt ze mee; saver kan doorcyclen |
| 4 | Patternlijst van 2–4 delen per bestand | Wat een cracktro als cracktro laat voelen; saver valt er gratis uit |
| 5 | Eén zelfstandig tekstbestand, logo als hex | Deelbaar in één Discord-bericht; hand-bewerkbaar; te versionen in git |
| 6 | Preview in de web-builder draait de **echte** engine via WASM | Eén implementatie in plaats van twee; kan niet driften. Zie §8.1 |
| 7 | Effecten via een **registry**, niet via een `switch` | Een nieuw effect is één regel + één functie, inplugbaar zonder de rest te raken. Zie §5.4 |

---

## 5. Het formaat

### 5.1 Voorbeeld

```ini
; /cracktro/omega.gti
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

### 5.2 Regels

- **Magic + versie:** eerste niet-lege, niet-commentaarregel moet
  `GTICRACK 1` zijn. Anders: afkeuren. Zo kunnen we het formaat laten groeien
  en rommel meteen weigeren.
- **Secties** tussen `[ ]`, sleutels als `K=V`, commentaar met `;`. Dezelfde
  smaak als CONFIG.TXT, dus herkenbaar voor gebruikers én dezelfde parseerstijl
  als wat er al staat.
- **`FX=`** kiest een bestaand effect bij naam: `COPPER`, `STARFIELD`,
  `RASTER`, `PLASMA`, `BOING`, `SYNTH`, `LOGO`. Namen, geen nummers, want een
  bestand moet leesbaar zijn.
  `LOGO` is géén nieuw effect: het is wat `crkDenise()` en `crkWrangler()` nu al
  doen — een 1bpp wordmark met een scroller eronder — alleen met de bitmap uit
  het bestand in plaats van uit PROGMEM.
- **`[LOGO n]`** draagt `W=`/`H=` plus hex in het **bestaande 1bpp MSB-first
  row-major formaat** van :1643. Daardoor werkt de blit-code die er al staat
  ongewijzigd, en kunnen we DENISE en WRANGLER als voorbeeld-`.gti`'s
  exporteren — gebruikers beginnen met echte cracktro's in plaats van een leeg
  vel.
- **`[SCROLL] TEXT=`** is de scrolltekst voor het hele bestand; per pattern zet
  `SCROLL=ON|OFF` hem aan of uit. Dat is precies RSI's "Screen Mode", en het
  past op onze engine omdat `crk_scrollerT()` nu al door elk effect apart
  onderaan getekend wordt.

### 5.3 Harde grenzen

Om boot eerlijk te houden op een ESP32-S3:

| grens | waarde |
| --- | --- |
| bestandsgrootte | 32 KB |
| patterns per bestand | 4 |
| logo's per bestand | 2 |
| logo-afmeting | 128 × 48 |
| scrolltekst | 512 tekens |

Over de grens → afkeuren en terugvallen (zie §6).

### 5.4 Effecten worden pluggable (registry in plaats van `switch`)

Een effect is nu een `crkXxx(float t)` achter een `switch`, met hardgecodeerde
teksten en kleuren. Daardoor kost een nieuw effect wijzigingen op meerdere
plekken, en is een effect principieel niet vanuit een bestand te sturen.

De `switch` gaat eruit, er komt een **registry** voor in de plaats:

```c
struct CrkPattern {                 // één geparsed [PATTERN]-blok
  uint8_t  fx;
  uint32_t timeMs;
  const char *title, *sub;
  uint16_t col;
  int8_t   logo;                    // index in de logo's van het bestand, -1 = geen
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

`FX=` zoekt op naam in die tabel. **Een nieuw effect is één regel plus één
functie** — geen `switch` om te wijzigen, geen nummering die synchroon moet
blijven, en geen wijziging aan het bestandsformaat.

Twee gevolgen die we expliciet willen:

**De bestaande effecten krijgen parameters.** `fxCopper(t, p)` leest `p.title`,
`p.sub` en `p.col` in plaats van `"OMEGAWARE"` hard te coderen. Dat is precies
dezelfde ingreep die `.gti` überhaupt mogelijk maakt — pluggable en
parametriseerbaar zijn één klus, geen twee.

**De ingebouwde cracktro's worden zélf patternlijsten.** Niet één codepad voor
ingebouwd en één voor bestanden, maar de ingebouwde stijlen als `CrkPattern`-
arrays door dezelfde engine. Eén pad in plaats van twee — waardoor het
bestandspad bij elke boot gebruikt wordt, niet alleen door mensen met een
bestand. Dezelfde redenering als bij de WASM-preview (§8.1).

**Eerlijke uitzondering:** `RETRONAUT` decodeert een PROGMEM-JPEG in plaats van
een 1bpp wordmark. Die past niet in het patternmodel zonder het formaat ook een
kleurafbeelding-begrip te geven. Die laten we voorlopig een speciaal geval, in
plaats van het formaat op te blazen voor één thema.

**Gevolg voor de tooling:** een nieuw effect krijgt zijn preview gratis — rij
toevoegen aan `CRK_FX`, WASM herbouwen, klaar (§8.1).

---

## 6. Vindplaats en faalgedrag

- Bestanden staan in **`/cracktro/*.gti`**.
- **`CRACKTRO=CUSTOM`** → kies willekeurig een geldig bestand uit de map.
- **`CRACKTRO=<naam>`** → gebruik `/cracktro/<naam>.gti`.
- Alle bestaande waarden (`OFF`, `NONE`, `0..7`, `OMEGA`, `DENISE`, `WRANGLER`,
  `RETRONAUT`) blijven ongewijzigd werken.

**Naamsbotsing, expliciet geregeld:** de ingebouwde namen winnen altijd. Pas als
een waarde géén ingebouwde naam en géén getal is, zoeken we
`/cracktro/<naam>.gti`. Een gebruiker die `/cracktro/denise.gti` maakt, krijgt
dus nog steeds het ingebouwde DENISE-thema bij `CRACKTRO=DENISE`; zijn eigen
bestand bereikt hij via een andere naam of via `CRACKTRO=CUSTOM`. Dit voorkomt
dat een bestand op de SD-kaart stilletjes bestaand gedrag kaapt.

**Faalregel, zonder uitzondering:** geen map, geen bestand, verkeerde magic,
parsefout, of over een grens → **terugvallen op de ingebouwde cracktro**. Nooit
een zwart scherm, nooit een hang. Reden naar serial loggen zodat we het op
afstand kunnen navragen.

---

## 7. Screensaver (feature B)

Nieuwe stand `SSMODE=CRACKTRO`; de cyclus wordt vierledig:
**SLIDES → BOUNCE → MATRIX → CRACKTRO**.

Te raken plekken:

| plek | wijziging |
| --- | --- |
| `SSMODE`-parse (**:1542**) | herken `CRACKTRO`; derde vlag `g_ss_cracktro` |
| cyclus-handler `IA_SSMODE` (**:4780**) | 3-weg → 4-weg |
| vertaaltabel | nieuwe `L_CRACKTRO` in zes talen (`L_MATRIX` als model) |
| `runScreensaver()` (**:3613**) | tak `if(g_ss_cracktro){ runCracktroSaver(); return; }` |

### 7.1 De ingreep in de engine

`drawCracktro` splitsen in **`drawCracktroFrame(pattern, t)`** plus twee
aanroepers: de boot-splash en de saver. Klein, want de effect-`switch` staat al
geïsoleerd; alleen de lus eromheen verschilt.

- **Boot-splash:** gedrag ongewijzigd (6000 ms of tik, `LOOP=1` respecteren).
- **Saver:** dezelfde frames, plus de drie fixes uit §2.3 —
  `webPanelService()` in de lus, de restore-staart zoals `runMatrixRain` die
  heeft, en een tijdbasis die elke ~10 minuten omklapt.

De omklap leggen we op een **veelvoud van de scrollperiode**; anders springt de
scroller zichtbaar één positie.

### 7.2 Waarom dit goedkoop is

De patternlijst uit §5 lóópt van nature rond, dus de saver hoeft geen eigen
inhoud te bedenken: hij draait dezelfde `/cracktro/`-bestanden, oneindig.
Feature A en feature B delen dus vrijwel alle code.

---

## 8. De keten

| schakel | wat het doet |
| --- | --- |
| **Web-tool** (GitHub Pages, in de vorm van de bestaande [6x8-bitmap-font-generator](https://dimitrihilverda.github.io/6x8-bitmap-font-generator/)) | pattern-editor + palette-editor + pixel-editor voor het logo; uitvoer `.gti`, met downloadknop én kopieerknop voor Discord |
| **Firmware** | leest en draait `/cracktro/*.gti` |
| **generator-skill** | genereert hetzelfde `.gti` uit een beschrijving, voor wie liever typt dan klikt |

RSI had palette-editor + teksteditor; de font-tool die er al staat is feitelijk
de font-editor van diezelfde gereedschapskist.

### 8.1 De preview is de echte engine (WASM)

De web-builder krijgt een preview die **exact** toont wat het paneel toont. Niet
"een goede benadering": we compileren **`drawCracktroFrame(pattern, t)` naar
WebAssembly** en draaien die in de browser met een `gfx_*`-shim naar een canvas.

Reden: een preview in JavaScript zou een **tweede implementatie van de engine**
zijn. Elk effect zou dan twee keer bestaan — C++ op het paneel, JS in de browser
— en die twee lopen uit elkaar zodra iemand een effect tweakt. Met WASM is de
preview definitiegewijs identiek, want het ís dezelfde code.

De seam die dit nodig heeft is **dezelfde opsplitsing die §7.1 toch al vraagt**
voor de screensaver. Eén refactor, twee opbrengsten.

Drie dingen die stil falen als we ze missen:

| | waarom het misgaat |
| --- | --- |
| **Resolutie** | `gW=480, gH=320` (**:54**), portrait 320×480. Vast; canvas exact zo groot. |
| **Font** | De engine tekent alles met `font6x8[95][6]` (**:131**). Dat is precies het formaat van de bestaande 6x8-bitmap-font-generator — beide tools delen dus dezelfde letterdata. |
| **Kleur** | `CRK_RGB` is **RGB565** (**:1658**): 5 bits rood, 6 groen, 5 blauw. Een canvas is 8 bits per kanaal. Zonder dezelfde quantisatie kies je in de tool een kleur die op het paneel nét anders uitkomt. |

En: de preview haalt `t` uit de **wandklok**, net als `millis()-startMs` in de
engine — niet uit een frameteller. Anders loopt de animatie in de browser op
60 fps en op het paneel op iets anders, en klopt de snelheid niet.

**Daarnaast:** het ontbrekende `CRACKTRO`-knopje in de web-UI (§2.4), inclusief
een zichtbare `OFF`-optie. Dat is Mez' tweede helft.

> **Coördinatie:** dat knopje raakt `firmware/shared/web_panel.h`, en die file
> wordt bewerkt door de zustersessie (dongle owner-lock / STANDALONE). Niet
> aanraken vóór afstemming.

---

## 9. Buiten scope (YAGNI)

- Geen scriptbare DSL, geen tijdlijn, geen eigen effectvolgorde per frame.
- Geen muziek.
- Geen nieuwe effecten; `.gti` herschikt en herkleurt wat er is.
- Geen kleurdiepte boven 1bpp voor logo's.
- **De 4827-twin** (`firmware/Gotek_JC4827W543/`) blijft buiten v1, tenzij de
  splitsing daar letterlijk identiek is. Pas bekijken als de 3248 werkt.

---

## 10. Testplan (hardware, niet "het compileert")

Geen enkele regel gaat naar Mez op basis van een geslaagde compile. Te testen op
het echte paneel:

1. Geldig `.gti` met twee patterns → draait bij boot.
2. `CRACKTRO=CUSTOM` met drie bestanden in de map → kiest er willekeurig één.
3. **Map afwezig** → ingebouwde cracktro, geen zwart scherm.
4. **Bestand met kapotte magic** → ingebouwde cracktro.
5. **Logo boven de maat** → afgekeurd, ingebouwde cracktro.
6. `CRACKTRO=OFF` → nog steeds geen cracktro (regressie).
7. **`SSMODE=CRACKTRO`, saver aan, web-UI openen → UI reageert nog.** Dit is de
   observeerbare toets op fix 1 uit §2.3.
8. Saver-cyclus blijft rondgaan; instelling overleeft een herstart.

De tijdbasis-omklap (fix 3) is niet in een avond uit te zitten. Voor de testbuild
zetten we de omklap tijdelijk kort zodat het gedrag wél waarneembaar is.

---

## 11. Open punten

- Exacte `L_CRACKTRO`-vertalingen in zes talen.
- Of de web-tool bestaande `.gti`'s kan inlezen om te bewerken (waarschijnlijk
  ja, maar niet nodig voor v1).

**Volgorde van bouwen.** De firmware-kant is v1: zonder lezende firmware heeft
een generator niets om voor te genereren. Web-tool en generator-skill volgen zodra
het formaat op echte hardware bewezen is. Dat voorkomt dat we een tool bouwen
voor een formaat dat nog kan schuiven.
