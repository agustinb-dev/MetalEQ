# MetalEQ V3.2 — Analyzer Research Record

This document is the provenance trail for the sixteen V3.2 analyzer targets. It
was written alongside `Source/dsp/MetalEqToneTargets.cpp`; the source indices in
that table refer to the same titles and URLs recorded here.

**Read this first.** No reference recordings were downloaded, ripped, separated
or measured for this build. No listening test of commercial records was
performed. The numeric target values are **engineering estimates** translated
from documented production descriptions using the rubric in §3. They are not
measured album spectra and must never be presented as the exact EQ used by an
artist, album or producer. Where a source describes a general character
("midrange authority", "smooth top") but not a frequency or gain, the model
records an interpretation, not a fact.

---

## 1. Why the old analyzer produced similar recommendations

The pre-V3.2 implementation had four entries:

| Old entry | How it was implemented |
|---|---|
| 2000s Metalcore: Tight Rhythm | pointed at the EQ curve of factory preset `metalcore-2000s-tight-rhythm` |
| Thrash: Technical Clarity | pointed at factory preset `thrash-technical-clarity` |
| Classic Thrash: Midrange Punch | pointed at factory preset `thrash-classic-midrange-punch` |
| Post-Amp: Mix Cleanup | pointed at factory preset `utility-postamp-mix-cleanup` |

The correction was:

```
target(f)  = response of that preset's EQ at f, normalised at 1 kHz
measured(f)= input magnitude at five points (120, 350, 1k, 2.8k, 7k)
suggestion = clamp(target(f) - measured(f), -6, +6)
```

Root causes:

1. **The "target" was another preset, not a research-backed destination.** Three
   of the four destinations were factory EQ curves for MetalEQ's own tone
   library; they were not independent tonal models. The fourth was a utility
   cleanup curve.
2. **The input measurement was too coarse to support a diagnosis.** Five narrow
   point measurements (±3 FFT bins) at fixed frequencies cannot distinguish a
   moving harmonic from a persistent tonal imbalance, and there was no smoothing,
   robust averaging, deadband, confidence or feature weighting.
3. **The comparison was a fixed shape match.** There was no concept of a
   tolerance ("this region already matches"), no per-feature importance, no
   input confidence and no evidence-based differentiation between destinations.
4. **The four curves were closely related.** They came from the same preset
   library and were all normalised at 1 kHz, so the resulting correction was
   dominated by the input and only weakly by the target.

V3.2 separates the three concepts explicitly: **input analysis → target model →
correction**, with the target model stored as structured, sourced data.

---

## 2. Evidence taxonomy

Each claim and each numeric feature is tagged:

| Tag | Meaning in this record |
|---|---|
| **DOC** | A direct interview, official credit list/liner notes, or a documented practice stated by the people involved. |
| **SEC** | A trade/education analysis, review or secondary synthesis. Useful, but not the artist/producer's own words. |
| **EST** | An engineering interpretation by this project: the direction and approximate magnitude we infer from the evidence. |
| **UNK** | Not established. No numeric claim is made. |

Important limits of the DOC tag:

- An album credit ("produced by X") proves who worked on the record, not what EQ
  they used.
- A producer saying "I like clear productions" does not prove a 750 Hz cut.
- A gear interview proves what amplifier or microphone was used, not its settings.
- Reviews describe an impression of the finished master; they are not production
  documentation.
- Wikipedia credit pages are used for credits only, and explicitly say when the
  credits are adapted from liner notes; the album credits themselves were not
  independently re-checked against physical liner notes in this environment.

---

## 3. Translation rubric (how qualitative evidence became numbers)

The analyzer works in eight broad bands, each expressed in dB **relative to the
same capture's 500–2000 Hz region**:

| # | Centre | Range | Dimension |
|---|---|---|---|
| 1 | 90 Hz | 70–115 Hz | sub weight / rumble |
| 2 | 175 Hz | 140–220 Hz | bass body / palm-mute weight |
| 3 | 350 Hz | 280–440 Hz | low-mid body / mud / boxiness |
| 4 | 700 Hz | 560–880 Hz | midrange body |
| 5 | 1.4 kHz | 1.12–1.76 kHz | upper-mid bite / aggression |
| 6 | 2.8 kHz | 2.24–3.52 kHz | presence / pick attack |
| 7 | 5.6 kHz | 4.48–7.04 kHz | brightness / harshness |
| 8 | 9.5 kHz | 7.6–12 kHz | fizz / air |

Translation rules, fixed before the numeric targets were written:

1. Start from the documented qualitative direction for that target (tight vs
   thick low end, scooped vs present low-mids, aggressive vs smooth top, etc.).
2. Assign a signed deviation to the relevant broad band:
   - strong, repeated direct description: **1.5–2.5 dB**;
   - one direct description: **1.0–1.5 dB**;
   - secondary production analysis or a single inference: **0.5–1.0 dB**;
   - no support: **0 dB**, with an explicit unknown.
3. Round to **0.5 dB**. Values are relative *shape objectives*, not absolute
   measured levels.
4. Set a tolerance per band. The key differentiators (90, 175, 350, 1400 and
   5600 Hz) use 0.5 dB; the remaining bands use 0.75–1.5 dB. Inside the
   tolerance, no correction is justified. This is why an input that already
   matches a target is left alone.
5. Set an importance weight (0.4–1.0). A band with weak evidence or a narrow
   effect gets a lower weight, so it cannot dominate the five-band fit.
6. Set a per-band correction cap (2.0–3.5 dB). Even if the input is far away, no
   single feature may demand more than a musical amount; the overall fitted
   gain is separately clamped to ±12 dB.
7. Set overall confidence (0.40–0.72) from the source mix: direct interviews and
   repeated claims raise it; credit-only or single-inference targets stay low.
8. Reject the temptation to make every target maximally different. Where the
   evidence overlaps (notably the Black Album genre target and the Bob Rock
   producer target), the profiles are allowed to be close and the limitation is
   recorded instead of inventing a difference.

The resulting target vectors are listed in §5. They were sanity-checked for
musical plausibility (a "thick" target must not have less low end than a "tight"
target; a lead target must have more midrange presence than its rhythm
counterpart) and then locked into the test suite.

### Why there are no measured observations

The prompt asks for measured reference observations *where possible*. This
environment could not legitimately obtain isolated guitar stems for the selected
commercial records. Downloading and separating copyrighted full mixes would (a)
not give an isolated guitar response and (b) make the result dominated by bass,
drums, vocals and mastering. No measurement was fabricated. The research record
therefore marks the numeric layer as EST throughout. If isolated, provenance-clear
references become available later, they should be measured with the documented
method (same eight bands, same 500–2000 Hz centring, long-term average over
varied playing) and used to refine the EST values, not to overwrite the source
record.

---

## 4. Target-by-target research

### 4.1 2000s Metalcore — Tight Rhythm

**Selected references:** Killswitch Engage, *The End of Heartache* (2004); Bullet
for My Valentine, *The Poison* (2005); Trivium, *Ascendancy* (2005) as adjacent
context.

**Documented**

- Killswitch Engage's Adam Dutkiewicz and Joel Stroetzel describe their sound as
  "mid-range, but with a little extra gain and a little extra low-end"; they use
  a Maxon 808 to "get rid of the flubbiness", and Adam complains that many modern
  masters have a painful 2.7–3 kHz upper-mid spike. (MusicRadar, 2014-02-14)
- Adam D on recording: "If you want more mids, turn your mids up. Turn your
  treble down. People put too much treble in their guitar tone and way too much
  bass. Everybody uses too much low end in their guitar tones." He also describes
  the 1990s/early-2000s metal approach as "scoop it out and get some clicky ass
  drums and make it precision tight" — from a producer who is critical of
  over-production, but it documents the era's reference aesthetic. (URM Academy,
  2025-08-21)
- Credits: *The End of Heartache* produced by Adam Dutkiewicz, mixed by Andy
  Sneap; *The Poison* produced and mixed by Colin Richardson. (Wikipedia credit
  pages, adapted from liner notes)

**Secondary**

- Nail The Mix's profile of Colin Richardson describes midrange authority and
  attack, less gain, HPF around 80–100 Hz, taming 5–10 kHz fizz and a modest
  1.5–4 kHz attack lift. This is an educational synthesis, not a Richardson
  interview, and it incorrectly attributes *Ascendancy* production to Richardson
  (the credits say Jason Suecof and Matt Heafy produced, Sneap mixed/mastered).
  Only the general technique is used, tagged SEC.

**Interpretation (EST)**

- Low end is controlled but not absent (90 Hz −3.0, 175 Hz −1.0 relative to the
  mid reference).
- Low-mids are deliberately scooped (350 Hz −2.5).
- Upper-mids/presence carry the aggression (1.4 kHz +3.0, 2.8 kHz +2.0).
- The top is kept from fizzing (5.6 kHz −2.0, 9.5 kHz −5.0).

**Unknown / limitation:** no isolated reference audio; the exact low-mid/Q of
these records is undocumented. The three records do not share one curve. The
model is a representative intersection, not a genre average.

**Confidence: 0.72.**

---

### 4.2 Modern Metalcore — Dense & Defined

**Selected references:** Architects, *Holy Hell* (2018); Spiritbox, *Eternal
Blue* (2021).

**Documented**

- Spiritbox's Mike Stringer: the debut was recorded entirely with plugins
  (Neural DSP), guitars double-tracked; "We barely use any gain, like at all";
  because they are in drop F#, "we roll off a lot of lows, but you have to be
  careful because at the same time you can lose a lot of weight"; the instruments
  are chosen for a snappy high-end register. (MusicRadar, 2021-09-17)
- Architects' Josh Middleton: four rhythm tracks, two with a 5150 and two with a
  Victory Kraken through the same Mesa cabinet and a Maxon TS9; low B tuning;
  "stop overdoing the gain… underplay the gain, so everything is clear."
  (MusicRadar, 2019-02-18)

**Secondary**

- Nail The Mix's analysis of Henrik Udd's Architects mix: multiband compression
  tames high-end fizz and low-end boom; subtractive EQ first, then broad additive
  body and upper-mid bite. (2025-05-29)
- Adam D's complaint about 2.7–3 kHz spikes in modern masters supports keeping
  the upper-mid region controlled rather than maximized.

**Interpretation (EST)**

- Lows remain tight but there is more low-mid body than the 2000s scoop
  (175 Hz −1.5, 350 Hz −0.5).
- Upper-mids are present but not pushed as hard as the 2000s target (1.4 kHz
  +1.0, 2.8 kHz +0.5).
- The top is smoother (5.6 kHz −4.0, 9.5 kHz −8.0), reflecting both the fizz
  control described by Udd and the darker/weightier character of the selected
  modern records.

**Unknown / limitation:** the two selected records differ substantially; this is
a conservative intersection. No isolated-guitar measurement was made.

**Confidence: 0.65.**

---

### 4.3 Lead Metalcore — Cut Through

**Selected references:** Bullet for My Valentine, *The Poison* (2005); Trivium,
*Ascendancy* (2005). These are selected for their prominent harmonised lead work,
not because a verified isolated lead-tone reference was available.

**Documented**

- Joey Sturgis: heavily distorted guitars lose most of their dynamics and become
  effectively linear; low lead notes need EQ/automation so they do not fight the
  bass and kick. (URM Academy, 2017-07-24)
- Matt Tuck recalls that a 5150 profile used on *The Poison* was later reused;
  the BFMV arrangements include prominent lead/solo parts. (MusicRadar,
  2018-06-14)
- Credits: *The Poison* produced and mixed by Colin Richardson; *Ascendancy*
  produced by Jason Suecof and Matt Heafy, mixed and mastered by Andy Sneap.
  (Wikipedia credit pages)

**Secondary**

- The Richardson analysis describes a 1.5–4 kHz attack lift and controlled
  5–10 kHz fizz, which is consistent with a lead that must cut through a dense
  mix. Tagged SEC.

**Interpretation (EST)**

This is the weakest-evidence target. No lead-specific production interview or
isolated lead recording was found. The model is built from arrangement logic
(a lead needs more midrange body and upper-mid presence than a rhythm layer, and
must not be dulled by excess fizz), not from a measured lead curve:

- more low-mid and mid body than the rhythm target (350 Hz +1.5, 700 Hz +2.0);
- strong upper-mid/presence (1.4 kHz +3.5, 2.8 kHz +2.0);
- controlled top (5.6 kHz −2.0, 9.5 kHz −7.0).

**Unknown / limitation:** lead-specific evidence is low. The target should be
treated as an arrangement-space starting point, not a claim about either album's
solo tone.

**Confidence: 0.45.**

---

### 4.4 Classic Thrash — Metallica, *The Black Album* (1991)

**Selected reference:** Metallica, *Metallica* (1991).

**Documented**

- Randy Staub: Hetfield's setup was "this Mesa-Boogie head and then all these
  other amps… It would generally have a scooped kind of tone." 9–11 cabinets,
  some baffled, room mics for ambience; the guitars were left uncompressed in the
  mix because James did not like what the SSL bus compressor did to them.
  (Mix, 2010-10-25)
- Bob Rock: the record gave the band "a rich, low-end punch that had been lacking
  in their previous recordings"; the band tracked live in the room; "Sad But
  True" was tuned down to D. (MusicRadar Black Album track-by-track, 2021-08;
  Blabbermouth summary, 2011-07-31)
- The band wanted the bottom end of *Dr. Feelgood*, which Rock had just produced.
  (Blabbermouth summary)
- Credits: produced by Bob Rock, engineered by Randy Staub. (Wikipedia credit
  page)

**Interpretation (EST)**

- Thick low end: 90 Hz 0.0, 175 Hz +2.0.
- Present low-mids despite the "scooped" setup: 350 Hz +0.5; mid body +1.0 at
  700 Hz, +0.5 at 1.4 kHz.
- Smooth top: 5.6 kHz −3.5, 9.5 kHz −8.0.

**Unknown / limitation:** "scooped" describes a guitar setup, not a measured
curve; the multi-cabinet room sound is not reproducible with EQ. The profile is
one interpretation of the album, not the album's actual EQ.

**Confidence: 0.70.**

---

### 4.5 Classic Thrash — Megadeth, *Rust in Peace* (1990)

**Selected reference:** Megadeth, *Rust in Peace* (1990).

**Documented**

- Produced by Dave Mustaine and Mike Clink; recorded at Rumbo Recorders; mixed
  by Max Norman. The album is widely classified as technical thrash; the
  Wikipedia page cites contemporary/retrospective genre sources. (Wikipedia
  credit page)
- *Countdown to Extinction* (1992) continued with Max Norman and a more
  groove/melody-oriented production. (Wikipedia credit page; context only)

**Interpretation (EST)**

The direct tonal evidence for *Rust in Peace* is thin. The model is deliberately
conservative:

- leaner lows than the Black Album (90 Hz −3.5, 175 Hz −1.0);
- controlled low-mids (350 Hz −2.0);
- strong upper-mid/presence for technical articulation (1.4 kHz +2.5, 2.8 kHz
  +1.5);
- a bright but not fizzy top (5.6 kHz −1.0, 9.5 kHz −6.0).

**Unknown / limitation:** no direct tonal interview or isolated reference was
obtained. This is the lowest-confidence genre target. It must not be presented
as the record's actual EQ. The mix-credit and technical-thrash context are the
only documented anchors.

**Confidence: 0.40.**

---

### 4.6 Classic Thrash — Metallica, *…And Justice for All* (1988)

**Selected reference:** Metallica, *…And Justice for All* (1988).

**Documented**

- James Hetfield: "I wanted it as in-your-face and dry as could be. Reverb was,
  you know, 'Get that outta here'… After a while, that got to be a little
  lifeless and flat." (MusicRadar quote, reproduced by Far Out, 2025-04-03)
- Steve Thompson (mixer): the band was meticulous and tight; mixed on an SSL G
  series; everything on 48-track analog tape, no Pro Tools. (Steve Thompson
  Productions, 2018-11-08)
- Flemming Rasmussen: the band ordered the bass down during mixing; Hetfield
  later said fried ears while touring led them to turn the high end up more and
  more. (Blabbermouth, 2025-10-01)
- Kirk Hammett: the bass frequencies interfered with Hetfield's rhythm-guitar
  tone; the band was going for a dry, in-your-face sound. (Decibel 2008 quote
  reproduced by Blabbermouth/Loudwire)
- Credits: produced by Flemming Rasmussen; mixed by Steve Thompson and Michael
  Barbiero. (Wikipedia credit page)

**Interpretation (EST)**

- Lean, dry low end (90 Hz −4.0, 175 Hz −2.0).
- A pronounced low-mid scoop (350 Hz −3.0) and little mid body (700 Hz 0.0).
- Strong upper-mid/treble attack (1.4 kHz +3.0, 2.8 kHz +2.0, 5.6 kHz 0.0):
  the documented "high end turned up" and "dry" descriptions support a
  brighter, harder top than the Black Album.
- The 9.5 kHz region is still controlled (−6.0); "treble-forward" is not the same
  as "fizzy".

**Unknown / limitation:** no isolated measurement; specific frequencies were not
documented. The bass was deliberately buried, so a full-mix capture is a poor
reference for this target. This profile is intentionally distinct from the Black
Album in low-end weight and low-mid scoop, not merely by a small presence boost.

**Confidence: 0.60.**

---

### 4.7 Bullet for My Valentine — *The Poison* (2005)

**Selected reference:** Bullet for My Valentine, *The Poison* (2005).

**Documented**

- Produced and mixed by Colin Richardson at The Chapel, Lincolnshire; engineered
  by Dan Turner; recorded at The Chapel, Backstage Studios, Notting Pill and SUSI.
  (Wikipedia credit page, adapted from liner notes)
- Matt Tuck: the main tones on later records came from a Kemper profile of "the
  5150 that was actually used for recording [2005 debut] The Poison"; the band
  values simple, pedal-free tracking. (MusicRadar, 2018-06-14)

**Secondary**

- The Richardson analysis (SEC) describes midrange authority/attack, less gain,
  an HPF, controlled fizz and an attack boost. The specific moves are not
  documented as Richardson's own settings.

**Interpretation (EST)**

- Tight lows (90 Hz −3.0, 175 Hz −1.0), a controlled low-mid scoop (350 Hz −2.0).
- Strong upper-mid and presence (1.4 kHz +2.5, 2.8 kHz +2.5) — the band's
  British metalcore bite.
- Controlled top (5.6 kHz −1.5, 9.5 kHz −6.0).

**Unknown / limitation:** a retrospective gear comment is not a tonal measurement
of the 2005 record; only the 5150 context is documented. No isolated reference
was measured.

**Confidence: 0.55.**

---

### 4.8 Trivium — *Ascendancy* (2005)

**Selected reference:** Trivium, *Ascendancy* (2005).

**Documented**

- Produced by Jason Suecof and Matt Heafy; mixed and mastered by Andy Sneap.
  (Wikipedia credit page, adapted from liner notes)
- Tape Op profiles Sneap's mixes as tight, snarling and clear, with the bands'
  playing fully audible. (Tape Op #144, 2021-07; the publicly available portion
  of the interview.)

**Secondary**

- Nail The Mix's Sneap analysis: tight controlled lows, a 300–500 Hz low-mid
  scoop, an 8–10 kHz low-pass and high clarity. SEC; the exact settings are not
  documented by Sneap in the sources read.

**Interpretation (EST)**

- Tight lows and a modest low-mid scoop (90 Hz −3.0, 175 Hz −1.0, 350 Hz −1.5).
- Strong upper-mid attack for the thrash-influenced twin-guitar work (1.4 kHz
  +3.0, 2.8 kHz +2.0).
- A darker top than the 2000s metalcore target (5.6 kHz −1.5, 9.5 kHz −7.0).

**Unknown / limitation:** no direct *Ascendancy* guitar-tone interview was
obtained. The model is based on credits, Sneap's documented mix approach and the
album's thrash/metalcore context. It is not a measured curve.

**Confidence: 0.45.**

---

### 4.9 As I Lay Dying — *An Ocean Between Us* (2007)

**Selected reference:** As I Lay Dying, *An Ocean Between Us* (2007).

**Documented**

- Produced by Adam Dutkiewicz and As I Lay Dying; mixed by Colin Richardson;
  guitar re-amping by Andy Sneap. (Wikipedia credit page, adapted from liner
  notes)
- A contemporary review describes the guitars as tight and aggressive ("a saw
  burning through bone"). (Exclaim!, 2007-08-13; SEC as a review)
- Adam D's documented low-end/mid views and Richardson's/Sneap's mix approaches
  are relevant context, but this album is explicitly a collaboration.

**Interpretation (EST)**

- Tight lows (90 Hz −3.0, 175 Hz −1.0), controlled low-mid scoop (350 Hz −1.0).
- Strong upper-mid/presence (1.4 kHz +2.5, 2.8 kHz +2.0).
- A darker top than brighter 2000s records (5.6 kHz −3.5, 9.5 kHz −6.0).

**Unknown / limitation:** no direct tone interview for this album was obtained;
the model describes the record, not one producer's signature. No isolated
reference was measured.

**Confidence: 0.50.**

---

### 4.10 Miss May I — *Monument* (2010)

**Selected reference:** Miss May I, *Monument* (2010).

**Documented**

- Produced, engineered, mixed and mastered by Joey Sturgis. (Wikipedia credit
  page, adapted from liner notes)
- Ryan Neff: the band's live rig was Peavey 6505 Plus heads into Mesa 4x12
  cabinets with Ibanez Tube Screamers and Boss EQ pedals; Sturgis handled the
  studio and worked "100% in-the-box"; the band learned to value clarity over
  raw gain instead of a mushy tone. (UberProAudio, 2010-10-04)
- Joey Sturgis: heavily distorted guitars lose their dynamics and become
  effectively linear; weight/density, editing and arrangement matter more than a
  plugin chain. (URM Academy, 2017-07-24)

**Interpretation (EST)**

- Tight, scooped lows (90 Hz −3.0, 175 Hz −1.5, 350 Hz −2.5).
- Bright upper-mid/presence (1.4 kHz +2.0, 2.8 kHz +2.5) and a modern top
  (5.6 kHz −1.0, 9.5 kHz −6.0).
- The profile is deliberately brighter/more scooped than the modern-metalcore
  target, reflecting the 2010 Sturgis era.

**Unknown / limitation:** no direct tonal interview about this album was found;
the profile relies on rig reports and Sturgis's general production philosophy.
No isolated reference was measured.

**Confidence: 0.50.**

---

### 4.11 Andy Sneap — Tight & Transparent

**Selected credits/context:** Killswitch Engage mix work; Testament, *The
Formation of Damnation*; Megadeth, *Endgame*.

**Documented**

- Tape Op describes Sneap's work as "tight, snarling mixes where the bands'
  virtuosity and chops can be heard with full force and clarity." (Tape Op #144,
  2021-07)
- Sneap: "I like clear, transparent productions" and the performance must remain
  audible; he is happy to mix other people's recordings and to work from DIs.
  (GITARRE & BASS, 2023-03-08)
- Killswitch Engage's *The End of Heartache* was mixed by Sneap. (Wikipedia
  credit page)

**Secondary**

- Nail The Mix's Sneap profile: 5150/EVH 6505 and JCM800, SM57+MD421, HPF
  80–120 Hz, LPF 8–10 kHz, a 300–500 Hz low-mid scoop and multiband control at
  150–400 Hz on the guitar bus. SEC; not a direct statement of exact settings.

**Interpretation (EST)**

- Tight lows (90 Hz −3.5, 175 Hz −1.5) and a clear low-mid scoop (350 Hz −2.5).
- Articulate upper-mid/presence (1.4 kHz +2.5, 2.8 kHz +1.5).
- Smooth top (5.6 kHz −2.0, 9.5 kHz −7.0).

**Unknown / limitation:** a producer profile is not one curve; the specific EQ
moves are secondary analysis. No isolated Sneap-produced guitar was measured.

**Confidence: 0.60.**

---

### 4.12 Colin Richardson — Midrange Authority

**Selected credits/context:** Bullet for My Valentine, *The Poison* (2005); As I
Lay Dying, *An Ocean Between Us* (2007, mixer).

**Documented**

- *The Poison* produced and mixed by Richardson; *An Ocean Between Us* mixed by
  Richardson. (Wikipedia credit pages)
- Matt Tuck ties *The Poison* to a 5150. (MusicRadar, 2018-06-14)

**Secondary**

- Nail The Mix's Richardson profile: midrange authority and attack, less gain,
  HPF 80–100 Hz, tame 5–10 kHz fizz, a 1.5–4 kHz attack lift, SM57+MD421 blend.
  SEC, and the article contains an incorrect claim about *Ascendancy* (which was
  produced by Jason Suecof and Matt Heafy, mixed by Sneap). Only the general
  technique is used.

**Interpretation (EST)**

- Tight lows (90 Hz −3.0, 175 Hz −1.0) and controlled low-mids (350 Hz −1.5).
- Strong 700 Hz body and upper-mid attack (700 Hz +2.0, 1.4 kHz +3.0,
  2.8 kHz +2.5) — more midrange-forward and less scooped than the Sneap target.
- Controlled top (5.6 kHz −1.5, 9.5 kHz −7.0).

**Unknown / limitation:** the specific EQ moves are secondary analysis; the
artist's records vary. This is an interpretation across records, not a claim of
one signature curve.

**Confidence: 0.55.**

---

### 4.13 Bob Rock — Weight & Smooth Top

**Selected references:** Metallica, *The Black Album* (1991); Mötley Crüe,
*Dr. Feelgood* (1989) as documented low-end context.

**Documented**

- The Black Album guitar setup was "scooped", with 9–11 cabinets, room mics and
  guitars left uncompressed. (Mix, 2010-10-25)
- Bob Rock: the record added a rich low-end punch; live-in-room tracking; D
  tuning on "Sad But True". (MusicRadar, 2021-08; Blabbermouth, 2011-07-31)
- The band wanted *Dr. Feelgood*'s bottom end; Rock had just produced it.
  (Blabbermouth summary)
- Credits: produced by Bob Rock, engineered by Randy Staub. (Wikipedia credit
  page)

**Interpretation (EST)**

The producer profile is deliberately broader than the Black Album genre target,
but the evidence overlaps heavily. The model reads Rock's documented work as a
weight-and-smoothness destination:

- fuller low end and low-mids than the genre target (90 Hz +1.0, 175 Hz +3.0,
  350 Hz +2.0);
- a warmer midrange (700 Hz +1.5) and less aggressive upper-mid (1.4 kHz −0.5,
  2.8 kHz −1.0);
- a smooth top (5.6 kHz −4.5, 9.5 kHz −9.0).

**Unknown / limitation:** this is not a universal "Bob Rock formula". The
*Black Album* is also the genre target, so the two profiles are intentionally
close in places; the producer profile differs by emphasizing weight and a
smoother top and by incorporating the documented *Dr. Feelgood* low-end context.
No isolated guitar measurement exists.

**Confidence: 0.65.**

---

### 4.14 Flemming Rasmussen — Mid-Focused Thrash

**Selected reference:** Metallica, *Master of Puppets* (1986).

**Documented**

- Rasmussen on *Master of Puppets*: Hetfield used a Mesa/Boogie Mark II C+
  rewired as a preamp into a 100-watt Marshall power amp and 4x12 cabs; the
  Boogies had a five-band EQ that the band "really messed around a lot with".
- Mics: two Shure SM57s close to the cone, two DPA/BMK room mics (one per
  cabinet) and AKG Gold tube mics at a 45° angle, three or four feet away; three
  mics per cabinet.
- He inserted "a studio parametric EQ" at the back of the Boogies so he could
  sculpt the tone from the control room. The source does **not** document the
  frequencies used.
- Rhythm guitars were double- and sometimes quadruple-tracked; the album was
  recorded to 24-track tape with no computer editing. (MusicRadar, 2025-05-01)
- Credits: produced by Metallica and Flemming Rasmussen. (Wikipedia credit page)
- AJFA context: the band ordered the bass down and Hetfield later linked the
  treble-heavy result to fried ears. (Blabbermouth, 2025-10-01)

**Interpretation (EST)**

- Tight low end (90 Hz −3.0, 175 Hz −1.0).
- Solid midrange body (350 Hz +1.0, 700 Hz +2.0, 1.4 kHz +2.5).
- Moderate presence (2.8 kHz +1.0) and a smooth top (5.6 kHz −2.0, 9.5 kHz
  −7.5).

**Unknown / limitation:** the studio EQ's frequencies are undocumented; "mid-
focused" is an interpretation of the gear/mic choices and the album's context,
not a measured curve. *Master of Puppets* is the reference, not AJFA.

**Confidence: 0.60.**

---

### 4.15 Mike Clink — Raw Hard-Rock Midrange

**Selected reference:** Guns N' Roses, *Appetite for Destruction* (1987).

**Documented**

- Produced by Mike Clink; Slash used a Les Paul copy with Seymour Duncan Alnico
  II pickups into a Marshall; final overdubs and mixing were done at Mediasound.
  (Wikipedia credit page, adapted from liner notes)
- Slash: "I went through about three or four Marshall heads and found the one
  that sounded right with that guitar, and that was it." The sound came together
  at the last minute. (Far Out, 2024-04-06; secondary quote)
- *Rust in Peace* was co-produced by Clink, a documented thrash credit alongside
  *Appetite*. (Wikipedia credit page)

**Interpretation (EST)**

- Raw, tight lows (90 Hz −3.5, 175 Hz −1.5).
- Strong midrange body and upper-mid cut (350 Hz +1.0, 700 Hz +2.0, 1.4 kHz
  +2.0), consistent with a Les Paul/Marshall hard-rock setup.
- Moderate presence (2.8 kHz +1.0) and a smooth top (5.6 kHz −1.5, 9.5 kHz
  −8.0); the approach is explicitly *not* a modern metalcore scoop/fizz.

**Unknown / limitation:** no direct Clink EQ interview was found; the model is
based on gear/performance accounts and the record's raw production context. It is
a hard-rock target, not a metalcore one.

**Confidence: 0.45.**

---

### 4.16 Max Norman — Polished Thrash Weight

**Selected references:** Megadeth, *Countdown to Extinction* (1992); Ozzy
Osbourne, *Diary of a Madman* (1981) as guitar-layering context.

**Documented**

- *Countdown to Extinction* produced and mixed by Max Norman and Dave Mustaine;
  the record moved toward more groove and melody, with meticulous pre-production.
  (Wikipedia credit page)
- Norman mixed *Rust in Peace* (1990). (Wikipedia credit page)
- Max Norman: Randy Rhoads triple-tracked his solos on *Blizzard of Ozz* and
  *Diary of a Madman*; Norman describes the layering in detail. (Gibson,
  2017-07-18; interview quote)

**Interpretation (EST)**

- Fuller lows than *Rust in Peace* (90 Hz −2.5, 175 Hz +1.0) and present
  low-mids (350 Hz +1.0).
- Midrange body and upper-mid clarity (700 Hz +1.5, 1.4 kHz +2.0, 2.8 kHz
  +1.0).
- Smooth top (5.6 kHz −2.5, 9.5 kHz −7.5).

**Unknown / limitation:** evidence is thin and spans two very different
productions. The numeric model is a low-confidence interpretation of Norman's
credits and documented layering practice, not a measured *Countdown* guitar
curve.

**Confidence: 0.40.**

---

## 5. Target vectors at a glance

Feature order: 90, 175, 350, 700, 1400, 2800, 5600, 9500 Hz. Values are dB
relative to the 500–2000 Hz region; EST.

| Target | 90 | 175 | 350 | 700 | 1.4k | 2.8k | 5.6k | 9.5k | Conf. |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2000s Metalcore | −3.0 | −1.0 | −2.5 | +1.0 | +3.0 | +2.0 | −2.0 | −5.0 | 0.72 |
| Modern Metalcore | −3.0 | −1.5 | −0.5 | +1.0 | +1.0 | +0.5 | −4.0 | −8.0 | 0.65 |
| Lead Metalcore | −2.5 | −0.5 | +1.5 | +2.0 | +3.5 | +2.0 | −2.0 | −7.0 | 0.45 |
| Black Album | 0.0 | +2.0 | +0.5 | +1.0 | +0.5 | 0.0 | −3.5 | −8.0 | 0.70 |
| Megadeth | −3.5 | −1.0 | −2.0 | +1.0 | +2.5 | +1.5 | −1.0 | −6.0 | 0.40 |
| …And Justice for All | −4.0 | −2.0 | −3.0 | 0.0 | +3.0 | +2.0 | 0.0 | −6.0 | 0.60 |
| Bullet for My Valentine | −3.0 | −1.0 | −2.0 | +1.0 | +2.5 | +2.5 | −1.5 | −6.0 | 0.55 |
| Trivium | −3.0 | −1.0 | −1.5 | +1.0 | +3.0 | +2.0 | −1.5 | −7.0 | 0.45 |
| As I Lay Dying | −3.0 | −1.0 | −1.5 | +1.0 | +2.5 | +2.0 | −3.5 | −6.0 | 0.50 |
| Miss May I | −3.0 | −1.5 | −2.5 | +1.0 | +2.0 | +2.5 | −1.0 | −6.0 | 0.50 |
| Andy Sneap | −3.5 | −1.5 | −2.5 | +1.0 | +2.5 | +1.5 | −2.0 | −7.0 | 0.60 |
| Colin Richardson | −3.0 | −1.0 | −1.5 | +2.0 | +3.0 | +2.5 | −1.5 | −7.0 | 0.55 |
| Bob Rock | +1.0 | +3.0 | +2.0 | +1.5 | −0.5 | −1.0 | −4.5 | −9.0 | 0.65 |
| Flemming Rasmussen | −3.0 | −1.0 | +1.0 | +2.0 | +2.5 | +1.0 | −2.0 | −7.5 | 0.60 |
| Mike Clink | −3.5 | −1.5 | +1.0 | +2.0 | +2.0 | +1.0 | −1.5 | −8.0 | 0.45 |
| Max Norman | −2.5 | +1.0 | +1.0 | +1.5 | +2.0 | +1.0 | −2.5 | −7.5 | 0.40 |

### Sanity checks used in the tests

- An input equal to a target receives essentially zero correction (deadband).
- A muddy input needs a larger 350 Hz cut than a thin input under the same target.
- A bright/fizzy input receives a high-frequency cut.
- AJFA vs the Black Album differ by at least 0.5 dB in their fitted corrections
  on the same input (the actual model difference is much larger in the low end
  and low-mids).
- Lead vs rhythm metalcore, and modern vs 2000s metalcore, likewise differ.
- All fitted gains are finite, within ±12 dB, and produce a valid five-band EQ.
- All sixteen target shapes are distinct; all have three to five source records.

---

## 6. Correction algorithm (and why it is not the target curve)

1. **Capture** eight seconds of mono input on the audio thread (preallocated).
2. **Worker analysis**: 8192-point Hann FFT, 50% overlap, skip frames below
   ~−80 dBFS; average the power spectrum; compute the eight band averages; the
   reference is the average 500–2000 Hz level. The result is eight relative dB
   values plus a confidence from frame count and input level.
3. **Target**: read the selected `ToneTarget` (eight relative levels, tolerances,
   weights, per-feature caps, confidence, sources).
4. **Desired change** per feature: `target − input`; if inside the target's
   tolerance, zero; otherwise remove the tolerance (soft deadband); scale by
   the 0.8–1.0 confidence factor; clamp to the feature's cap. Key
   differentiators use a 0.5 dB deadband.
5. **Fit**: solve a weighted, ridge-regularized least-squares problem that
   approximates those eight desired changes with five peaking sections at
   120, 350, 1000, 2800 and 7000 Hz (Q = 0.8). This avoids treating five
   overlapping filters as independent bands.
6. **Limit**: clamp every fitted gain to ±12 dB; report `alreadyClose` when the
   fit is below 0.25 dB.
7. **Apply**: write the five correction gains (scaled by the Amount control) into
   MetalEQ's existing five musical band parameters. The target model is never
   written as the EQ curve.
8. **Target switch**: if a capture is already analysed, the correction is
   recomputed from the stored input profile without recording again. If the
   target changes during capture, the capture finishes against the current
   target. Applying is rejected if the report's target is not the selected one.

Threading: the audio thread only copies samples to the existing preallocated
capture buffer. FFT, feature extraction, the least-squares solve and all
allocations happen on the analyzer worker or the message thread. The report is
published under a spin lock with atomics; no locks or allocations occur on the
audio thread.

---

## 7. Known limitations of the whole system

- **No measured reference spectra.** Every numeric target value is EST.
- **No isolated stems.** Full mixes cannot isolate the guitar; no such analysis
  is claimed.
- **EQ is not an amp/cab/mic.** The target models describe broad balance only.
  They cannot recreate a different amplifier, cabinet, microphone, room,
  performance, arrangement or mastering chain.
- **Broad-band only.** Eight bands and five broad peaking filters cannot fix a
  narrow resonance; the separate surgical/dynamic resonance tools handle that.
- **Input dependence.** A DI, a miked cabinet, a full mix or a very bright
  interface will produce different measured balances. The deadband and caps
  reduce damage but do not make the diagnosis source-independent.
- **Some targets are close by design.** The Black Album genre target and the Bob
  Rock producer target share the album's evidence; Megadeth and Trivium are
  lower-confidence and may recommend similar moves on some inputs. This is
  recorded rather than hidden.
- **Secondary sources can be wrong.** The Nail The Mix Colin Richardson article
  misattributes *Ascendancy* production; the model uses only its general
  technique and relies on the credit pages for facts. The same caution applies
  to any production-education article.
- **Credit pages vs liner notes.** Wikipedia's album pages state that credits are
  adapted from liner notes, but the physical liner notes were not re-checked in
  this environment.
- **No listening validation in this environment.** The tonal direction was
  sanity-checked through the model ordering and deterministic tests, not through
  a monitored listening session with the plugin.

If better evidence arrives, update the source record and the target vector
together. Do not add a numeric value without a documented rationale or a
documented measurement method.
