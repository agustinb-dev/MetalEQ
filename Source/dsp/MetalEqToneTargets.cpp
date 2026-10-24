/*
    MetalEQ - evidence-traced target models and adaptive correction (V3.2).

    The data below is intentionally explicit and reviewable. Read
    Docs/AnalyzerResearch.md alongside it: every target lists the recordings
    and sources used, what those sources actually support, and what remains an
    engineering estimate.

    Centring convention
    -------------------
    Feature levels are dB relative to the 500-2000 Hz region of the same
    capture. This removes overall level and makes the input and every target
    directly comparable. The absolute numbers are not claimed to be measured
    album spectra; they are estimates rounded to 0.5 dB, produced with the
    translation rubric in the research document.
*/

#include "MetalEqToneTargets.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include "MetalEqAnalysis.h"

namespace metaleq::tone
{

using metaleq::detail::clampDouble;

namespace
{

//------------------------------------------------------------------------------
// Source records. Keep URLs stable: these are the provenance trail the UI and
// the research document expose.
//------------------------------------------------------------------------------
constexpr std::array<Source, 35> kSources {{
    { "Classic Tracks: Metallica's \"Enter Sandman\" (Mix, Sarah Benzuly)",
      "https://www.mixonline.com/recording/classic-tracks-metallicas-enter-sandman-366232",
      "2010-10-25" },
    { "Metallica The Black Album track-by-track interview with Bob Rock (MusicRadar, Joe Bosso)",
      "https://www.musicradar.com/news/bob-rock-metallica-black-album-interview",
      "2021-08" },
    { "Producer Bob Rock Talks About Making Of Metallica's 'Black' Album (Blabbermouth)",
      "https://blabbermouth.net/news/producer-bob-rock-talks-about-making-of-metallica-s-black-album",
      "2011-07-31" },
    { "'As dry as could be': The Metallica album James Hetfield thought tried too hard (Far Out, Kelly Murphy)",
      "https://faroutmagazine.co.uk/the-record-james-hetfield-said-tried-too-hard/",
      "2025-04-03" },
    { "Metallica's 'And Justice For All' (Steve Thompson Productions)",
      "https://stevethompsonproductions.com/news/metallicas-and-justice-for-all",
      "2018-11-08" },
    { "Metallica's Former Producer On Lack Of Bass In '...And Justice For All' Mix (Blabbermouth)",
      "https://blabbermouth.net/news/metallicas-former-producer-on-lack-of-bass-in-and-justice-for-all-mix-ive-asked-them-a-thousand-times-i-do-not-know",
      "2025-10-01" },
    { "How Flemming Rasmussen produced Metallica's classic Master Of Puppets (MusicRadar, Paul Elliott)",
      "https://www.musicradar.com/artists/producers-engineers/all-the-albums-i-did-with-metallica-were-recorded-on-24-track-analogue-tape-theres-not-a-computer-in-sight-how-flemming-rasmussen-produced-metallicas-classic-master-of-puppets",
      "2025-05-01" },
    { "Killswitch Engage's 8 tips on mastering metalcore (MusicRadar, Matt Parker)",
      "https://www.musicradar.com/news/guitars/killswitch-engages-8-tips-on-mastering-metalcore-594489",
      "2014-02-14" },
    { "Adam D: Perfecting Rhythm Guitars, The Dangers of Over-Producing, and Shaping Metalcore (URM Academy)",
      "https://urm.academy/adam-d-perfecting-rhythm-guitars-the-dangers-of-over-producing-and-shaping-metalcore/",
      "2025-08-21" },
    { "Spiritbox guitarist Mike Stringer's 13 tips for using guitar plugins (MusicRadar, Stuart Williams)",
      "https://www.musicradar.com/news/spiritbox-mike-stringer-plugins",
      "2021-09-17" },
    { "Architects: 'Tom is there throughout the album in ambient tracks, clean parts and more' (MusicRadar, Amit Sharma)",
      "https://www.musicradar.com/news/architects-tom-is-there-throughout-the-album-in-ambient-tracks-clean-parts-and-more",
      "2019-02-18" },
    { "Bullet For My Valentine: '...our strengths are in songwriting, not just guitar playing' (MusicRadar, Amit Sharma)",
      "https://www.musicradar.com/news/bullet-for-my-valentine-right-now-i-believe-more-than-ever-that-our-strengths-are-in-songwriting-not-just-guitar-playing",
      "2018-06-14" },
    { "How Colin Richardson Defined The Sound of Modern Metal (Nail The Mix)",
      "https://www.nailthemix.com/colin-richardson",
      "2025-08-06" },
    { "The Andy Sneap Sound: Deconstructing His Iconic Metal Mixes (Nail The Mix)",
      "https://www.nailthemix.com/andy-sneap",
      "2025-08-06" },
    { "How To Use EQ Cuts And Boosts on Modern Metal Guitars (ft. Architects) (Nail The Mix)",
      "https://www.nailthemix.com/architects-metalcore-guitar-tone-udd-mix",
      "2025-05-29" },
    { "Andy Sneap: The More Things Change... (Tape Op #144, Avinash Mittur)",
      "https://tapeop.com/interviews/144/andy-sneap",
      "2021-07" },
    { "Andy Sneap von Judas Priest im Interview: Mehr Produzent als Gitarrist? (GITARRE & BASS, Matthias Mineur)",
      "https://www.gitarrebass.de/stories/andy-sneap-von-judas-priest-im-interview-mehr-produzent-als-gitarrist/",
      "2023-03-08" },
    { "As I Lay Dying: An Ocean Between Us (Exclaim!, Dave Synyard)",
      "https://exclaim.ca/music/article/as_i_lay_dying-ocean_between_us",
      "2007-08-13" },
    { "Miss May I - Ryan Neff Interview (UberProAudio)",
      "https://www.uberproaudio.com/interviews/735-miss-may-i-ryan-neff-interview",
      "2010-10-04" },
    { "Ozzy Producer Max Norman Reveals Randy Rhoads' Studio Secret (Gibson, Michael Leonard)",
      "https://legacy.gibson.com/News-Lifestyle/News/en-us/Randy-Rhoads-Studio-Solos.aspx",
      "2017-07-18" },
    { "Why Slash hated recording guitar for 'Appetite for Destruction' (Far Out, Tim Coffman)",
      "https://faroutmagazine.co.uk/slash-hated-recording-guitar-for-appetite-for-destruction/",
      "2024-04-06" },
    { "The End of Heartache (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/The_End_of_Heartache",
      "accessed 2026-10-24" },
    { "The Fall of Ideals (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/The_Fall_of_Ideals",
      "accessed 2026-10-24" },
    { "The Oncoming Storm (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/The_Oncoming_Storm",
      "accessed 2026-10-24" },
    { "The Poison (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/The_Poison_(Bullet_for_My_Valentine_album)",
      "accessed 2026-10-24" },
    { "Ascendancy (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Ascendancy_(album)",
      "accessed 2026-10-24" },
    { "An Ocean Between Us (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/An_Ocean_Between_Us",
      "accessed 2026-10-24" },
    { "Monument (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Monument_(Miss_May_I_album)",
      "accessed 2026-10-24" },
    { "Rust in Peace (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Rust_in_Peace",
      "accessed 2026-10-24" },
    { "Countdown to Extinction (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Countdown_to_Extinction",
      "accessed 2026-10-24" },
    { "Appetite for Destruction (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Appetite_for_Destruction",
      "accessed 2026-10-24" },
    { "Metallica (The Black Album) (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Metallica_(album)",
      "accessed 2026-10-24" },
    { "...And Justice for All (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/...And_Justice_for_All",
      "accessed 2026-10-24" },
    { "Master of Puppets (Wikipedia; credits adapted from liner notes)",
      "https://en.wikipedia.org/wiki/Master_of_Puppets",
      "accessed 2026-10-24" },
    { "Joey Sturgis: The Real 'Secret Sauce', Balancing Guitars vs Vocals, and Monitoring on a Budget (URM Academy)",
      "https://urm.academy/ep150-dear-joey/",
      "2017-07-24" }
}};

//------------------------------------------------------------------------------
// Reusable rationale strings. The per-feature tag says HOW strongly the
// destination is supported; the source list says WHICH material supports it.
//------------------------------------------------------------------------------
constexpr const char* kRumble      = "Low-end/rumble control described for the selected reference";
constexpr const char* kTightLow    = "Tight low end described for the selected reference";
constexpr const char* kLowWeight   = "Low-end weight described for the selected reference";
constexpr const char* kMidScoop    = "Low-mid control or scoop described for the selected reference";
constexpr const char* kMidBody     = "Midrange body described for the selected reference";
constexpr const char* kUpperMid    = "Upper-mid articulation/aggression described for the selected reference";
constexpr const char* kPresence    = "Presence and pick attack described for the selected reference";
constexpr const char* kSmoothTop   = "Controlled high end described for the selected reference";
constexpr const char* kBrightTop   = "Bright high end described for the selected reference";
constexpr const char* kSpace       = "Engineering interpretation of lead-guitar space in a dense mix";

constexpr FeatureTarget f (double db, double tol, double weight, double maxCorrection,
                           Evidence evidence, const char* rationale)
{
    return { db, tol, weight, maxCorrection, evidence, rationale };
}

constexpr SourceRef src (int index, const char* claim, Evidence evidence)
{
    return { index, claim, evidence };
}

//------------------------------------------------------------------------------
// Target models. Values are estimates, rounded to 0.5 dB. See the research
// document for the translation rubric and the limitations of each profile.
//------------------------------------------------------------------------------
constexpr std::array<ToneTarget, 16> kTargets {{
    // 1. 2000s Metalcore -----------------------------------------------------
    {
        "genre-metalcore-2000s",
        "2000s Metalcore — Tight Rhythm",
        "2000s Metalcore",
        Category::genre,
        "Killswitch Engage, The End of Heartache (2004); Bullet for My Valentine, The Poison (2005)",
        "Tight, controlled low end; a deliberate low-mid scoop; aggressive upper-mid bite; fizz kept under control. "
        "A rhythm-guitar destination, not a claim that every mid-2000s record shared one curve.",
        "Representative records, not a genre average. The exact EQ of these albums is undocumented; values are "
        "engineering estimates. Captures of a DI, a full mix, or a very different cabinet will not map exactly.",
        0.72,
        {{
            f (-3.0, 0.50, 0.7, 3.0, Evidence::documented, kRumble),
            f (-1.0, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (-2.5, 0.50, 1.0, 3.5, Evidence::documented, kMidScoop),
            f (+1.0, 0.75, 0.7, 2.5, Evidence::secondary, kMidBody),
            f (+3.0, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.0, 1.00, 1.0, 2.5, Evidence::documented, kPresence),
            f (-2.0, 0.50, 0.7, 2.5, Evidence::secondary, kSmoothTop),
            f (-5.0, 1.50, 0.4, 2.0, Evidence::secondary, kBrightTop)
        }},
        {{
            src (7,  "Killswitch's Adam D/Joel describe a mid-range-forward tone with extra low end, an 808 to remove flubbiness, and painful 2.7-3 kHz spikes in modern masters.", Evidence::documented),
            src (8,  "Adam D: turn mids up and treble/bass down; too much low end is common; 1990s/early-2000s metal often aimed to scoop it out and be precision-tight.", Evidence::documented),
            src (12, "Secondary analysis of Colin Richardson's approach: midrange authority and attack, less gain, HPF 80-100 Hz, tame 5-10 kHz fizz, modest 1.5-4 kHz attack boost.", Evidence::secondary),
            src (21, "The End of Heartache (2004) credit list: produced by Adam Dutkiewicz, mixed by Andy Sneap.", Evidence::documented),
            src (24, "The Poison (2005) credit list: produced and mixed by Colin Richardson.", Evidence::documented)
        }},
        5
    },

    // 2. Modern Metalcore ----------------------------------------------------
    {
        "genre-metalcore-modern",
        "Modern Metalcore — Dense & Defined",
        "Modern Metalcore",
        Category::genre,
        "Architects, Holy Hell (2018); Spiritbox, Eternal Blue (2021)",
        "Tight, defined lows with more low-mid body than the 2000s scoop; controlled upper-mid aggression; "
        "a smoother, less fizzy top. Built for dense, layered rhythm arrangements.",
        "The selected records differ substantially from each other. This is a conservative intersection of their "
        "documented approaches, not a claim that all modern metalcore sounds alike.",
        0.65,
        {{
            f (-3.0, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-1.5, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (-0.5, 0.50, 0.9, 3.5, Evidence::secondary, kMidScoop),
            f (+1.0, 0.75, 0.7, 2.5, Evidence::interpretation, kMidBody),
            f (+1.0, 0.50, 0.8, 3.0, Evidence::documented, kUpperMid),
            f (+0.5, 1.00, 0.8, 2.5, Evidence::documented, kPresence),
            f (-4.0, 0.50, 0.9, 2.5, Evidence::secondary, kSmoothTop),
            f (-8.0, 1.50, 0.7, 2.0, Evidence::secondary, kBrightTop)
        }},
        {{
            src (9,  "Spiritbox's Mike Stringer: barely any gain, roll off lows but keep weight, bright/clear top; guitars double-tracked and recorded entirely with plugins.", Evidence::documented),
            src (10, "Architects' Holy Hell: four rhythm tracks (5150 + Victory Kraken), TS9 boost, low B tuning, and a deliberate 'underplay the gain' approach.", Evidence::documented),
            src (14, "Secondary analysis of Henrik Udd's Architects mix: multiband control of high fizz and low boom; cut-then-boost EQ adds body and upper-mid bite.", Evidence::secondary),
            src (7,  "Killswitch's Adam D singles out a painful 2.7-3 kHz spike in many modern masters; the model keeps upper-mid aggression controlled.", Evidence::documented)
        }},
        4
    },

    // 3. Lead Metalcore ------------------------------------------------------
    {
        "genre-metalcore-lead",
        "Lead Metalcore — Cut Through",
        "Lead Metalcore",
        Category::genre,
        "Bullet for My Valentine, The Poison (2005); Trivium, Ascendancy (2005)",
        "A lead-guitar space: more low-mid body and upper-mid presence than a rhythm target, "
        "with a controlled top so solos and harmonised lines stay articulate above a dense mix.",
        "No isolated lead-guitar reference was available. The target is an engineering interpretation of textural "
        "space and documented mix practices, not a measured lead tone. Treat it as a starting point.",
        0.45,
        {{
            f (-2.5, 0.50, 0.5, 3.0, Evidence::interpretation, kRumble),
            f (-0.5, 0.50, 0.5, 3.0, Evidence::interpretation, kTightLow),
            f (+1.5, 0.50, 0.8, 3.5, Evidence::interpretation, kMidScoop),
            f (+2.0, 0.75, 1.0, 2.5, Evidence::interpretation, kMidBody),
            f (+3.5, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.0, 1.00, 1.0, 2.5, Evidence::documented, kPresence),
            f (-2.0, 0.50, 0.6, 2.5, Evidence::secondary, kSmoothTop),
            f (-7.0, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (34, "Joey Sturgis: heavily distorted guitars become effectively linear; low lead notes need automation/EQ so they do not fight the bass and kick.", Evidence::documented),
            src (12, "Secondary analysis of Richardson: midrange authority, a 1.5-4 kHz attack lift and controlled 5-10 kHz fizz.", Evidence::secondary),
            src (11, "Matt Tuck recalls The Poison's 5150 profile being reused later; the band's arrangements include prominent lead/solo parts.", Evidence::documented),
            src (24, "The Poison (2005) is the selected BFMV lead-guitar reference.", Evidence::documented),
            src (25, "Ascendancy (2005) is the selected Trivium twin-lead reference.", Evidence::documented)
        }},
        5
    },

    // 4. Black Album ---------------------------------------------------------
    {
        "genre-thrash-black-album",
        "Classic Thrash — The Black Album (1991)",
        "Black Album",
        Category::genre,
        "Metallica, Metallica (The Black Album, 1991)",
        "A thick, controlled low end with real low-mid weight, a broad mid scoop in the guitar setup, "
        "and a smooth rather than fizzy top. Power and size without the dry glare of Justice.",
        "The album is a multi-guitar, multi-cabinet production, not a single EQ curve. 'Scooped' describes the "
        "documented setup, not a precise measurement. Values are estimates; the room and layering matter as much as EQ.",
        0.70,
        {{
            f (+0.0, 0.50, 1.0, 3.0, Evidence::documented, kRumble),
            f (+2.0, 0.50, 1.0, 3.0, Evidence::documented, kTightLow),
            f (+0.5, 0.50, 0.9, 3.5, Evidence::documented, kMidScoop),
            f (+1.0, 0.75, 0.9, 2.5, Evidence::documented, kMidBody),
            f (+0.5, 0.50, 0.8, 3.0, Evidence::secondary, kUpperMid),
            f (+0.0, 1.00, 0.7, 2.5, Evidence::interpretation, kPresence),
            f (-3.5, 0.50, 0.8, 2.5, Evidence::documented, kSmoothTop),
            f (-8.0, 1.50, 0.5, 2.0, Evidence::documented, kBrightTop)
        }},
        {{
            src (0,  "Staub/Rock: Hetfield's rig was a Mesa-Boogie plus other amps with a 'scooped kind of tone'; 9-11 cabinets and room mics; guitars left uncompressed in the mix.", Evidence::documented),
            src (1,  "Bob Rock: the record added a rich low-end punch that had been missing; the band tracked live and tuned down to D for Sad But True.", Evidence::documented),
            src (31, "Metallica (The Black Album, 1991) credits: produced by Bob Rock, engineered by Randy Staub.", Evidence::documented),
            src (2,  "Bob Rock interview summary: live-in-room tracking, groove and low-end weight; the band wanted the bottom end of Dr. Feelgood.", Evidence::documented)
        }},
        4
    },

    // 5. Megadeth (Rust in Peace) -------------------------------------------
    {
        "genre-thrash-megadeth",
        "Classic Thrash — Megadeth, Rust in Peace (1990)",
        "Megadeth",
        Category::genre,
        "Megadeth, Rust in Peace (1990)",
        "A tight, dry, articulate technical-thrash destination: lean low end, controlled low-mid weight, "
        "strong upper-mid attack and a bright but not fizzy top.",
        "Direct tonal documentation is sparse. The profile is derived from the production credits and the "
        "technical-thrash context, with a deliberately conservative correction. No claim of the record's exact EQ.",
        0.40,
        {{
            f (-3.5, 0.50, 0.8, 3.0, Evidence::interpretation, kRumble),
            f (-1.0, 0.50, 0.8, 3.0, Evidence::interpretation, kTightLow),
            f (-2.0, 0.50, 0.9, 3.5, Evidence::interpretation, kMidScoop),
            f (+1.0, 0.75, 0.8, 2.5, Evidence::interpretation, kMidBody),
            f (+2.5, 0.50, 1.0, 3.0, Evidence::interpretation, kUpperMid),
            f (+1.5, 1.00, 0.9, 2.5, Evidence::interpretation, kPresence),
            f (-1.0, 0.50, 0.7, 2.5, Evidence::interpretation, kSmoothTop),
            f (-6.0, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (28, "Rust in Peace (1990): produced by Dave Mustaine and Mike Clink, mixed by Max Norman, recorded at Rumbo Recorders; widely classified as technical thrash.", Evidence::documented),
            src (29, "Countdown to Extinction (1992) shows Megadeth's next production step with Max Norman; used only as context for the band's evolution, not as Rust in Peace evidence.", Evidence::documented),
            src (19, "Producer context only: Max Norman's documented layered-guitar practice; not a measurement of Rust in Peace.", Evidence::interpretation)
        }},
        3
    },

    // 6. ...And Justice for All ----------------------------------------------
    {
        "genre-thrash-ajfa",
        "Classic Thrash — Metallica, ...And Justice for All (1988)",
        "…And Justice for All",
        Category::genre,
        "Metallica, ...And Justice for All (1988)",
        "Dry, tight and in-your-face: lean low end, a controlled low-mid range, strong upper-mid/treble "
        "attack and almost no room. The opposite of the Black Album's low-end weight.",
        "The album's guitar sound is documented only qualitatively. 'Dry' and 'treble-forward' are supported; "
        "specific frequencies are not. The bass was deliberately buried, so full-mix captures are a poor reference.",
        0.60,
        {{
            f (-4.0, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-2.0, 0.50, 0.9, 3.0, Evidence::documented, kTightLow),
            f (-3.0, 0.50, 1.0, 3.5, Evidence::documented, kMidScoop),
            f (+0.0, 0.75, 0.8, 2.5, Evidence::interpretation, kMidBody),
            f (+3.0, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.0, 1.00, 0.9, 2.5, Evidence::documented, kPresence),
            f (+0.0, 0.50, 0.8, 2.5, Evidence::documented, kSmoothTop),
            f (-6.0, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (3,  "Hetfield (MusicRadar quote): he wanted the guitar as 'in-your-face and dry as could be'; reverb was rejected, though he later felt it became lifeless and flat.", Evidence::documented),
            src (4,  "Steve Thompson: the band was meticulous and tight; mixed on an SSL G series, straight to tape, no Pro Tools.", Evidence::documented),
            src (5,  "Flemming Rasmussen: the band ordered the bass down; Hetfield later explained that fried ears led to turning the high end up.", Evidence::documented),
            src (32, "...And Justice for All (1988) credits: produced by Flemming Rasmussen; mixed by Steve Thompson and Michael Barbiero.", Evidence::documented)
        }},
        4
    },

    // 7. Bullet for My Valentine ---------------------------------------------
    {
        "artist-bullet-for-my-valentine",
        "Bullet for My Valentine — The Poison (2005)",
        "Bullet for My Valentine",
        Category::artist,
        "Bullet for My Valentine, The Poison (2005)",
        "Tight, British metalcore rhythm guitars with a strong upper-mid/presence signature, "
        "a controlled low-mid range and a 5150-based bite.",
        "The band's sound changed substantially after this period. This profile is only for The Poison; "
        "lead-specific and later-record tones are outside its scope.",
        0.55,
        {{
            f (-3.0, 0.50, 0.7, 3.0, Evidence::secondary, kRumble),
            f (-1.0, 0.50, 0.7, 3.0, Evidence::secondary, kTightLow),
            f (-2.0, 0.50, 0.9, 3.5, Evidence::secondary, kMidScoop),
            f (+1.0, 0.75, 0.7, 2.5, Evidence::interpretation, kMidBody),
            f (+2.5, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.5, 1.00, 1.0, 2.5, Evidence::documented, kPresence),
            f (-1.5, 0.50, 0.7, 2.5, Evidence::secondary, kSmoothTop),
            f (-6.0, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (24, "The Poison (2005): produced and mixed by Colin Richardson at The Chapel; engineered by Dan Turner.", Evidence::documented),
            src (11, "Matt Tuck: a 5150 profile from The Poison sessions was reused on later albums; he describes simple, pedal-free tracking.", Evidence::documented),
            src (12, "Secondary analysis of Richardson's approach: midrange authority, less gain, HPF, controlled fizz, attack boost.", Evidence::secondary)
        }},
        3
    },

    // 8. Trivium -------------------------------------------------------------
    {
        "artist-trivium",
        "Trivium — Ascendancy (2005)",
        "Trivium",
        Category::artist,
        "Trivium, Ascendancy (2005)",
        "Tight, articulate thrash-influenced metalcore: controlled low-mid build-up, strong upper-mid cut, "
        "clear attack and a bright but not fizzy top.",
        "No direct Ascendancy guitar-tone interview was obtained. The profile is based on production credits, "
        "Andy Sneap's documented mix approach and the album's thrash/metalcore context.",
        0.45,
        {{
            f (-3.0, 0.50, 0.7, 3.0, Evidence::interpretation, kRumble),
            f (-1.0, 0.50, 0.7, 3.0, Evidence::interpretation, kTightLow),
            f (-1.5, 0.50, 0.9, 3.5, Evidence::secondary, kMidScoop),
            f (+1.0, 0.75, 0.8, 2.5, Evidence::interpretation, kMidBody),
            f (+3.0, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.0, 1.00, 0.9, 2.5, Evidence::secondary, kPresence),
            f (-1.5, 0.50, 0.7, 2.5, Evidence::secondary, kSmoothTop),
            f (-7.0, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (25, "Ascendancy (2005): produced by Jason Suecof and Matt Heafy; mixed and mastered by Andy Sneap.", Evidence::documented),
            src (13, "Secondary analysis of Sneap's approach: tight controlled lows, a 300-500 Hz low-mid scoop, 8-10 kHz low-pass and high clarity.", Evidence::secondary),
            src (15, "Tape Op profile: Sneap's mixes are described as tight, snarling and clear, with the playing audible in full.", Evidence::documented)
        }},
        3
    },

    // 9. As I Lay Dying ------------------------------------------------------
    {
        "artist-as-i-lay-dying",
        "As I Lay Dying — An Ocean Between Us (2007)",
        "As I Lay Dying",
        Category::artist,
        "As I Lay Dying, An Ocean Between Us (2007)",
        "A modern metalcore rhythm destination: tight low end, a controlled low-mid scoop, "
        "sharp upper-mid attack and a darker top than the brighter 2000s records.",
        "The album was produced by Adam Dutkiewicz, mixed by Colin Richardson and re-amped by Andy Sneap, "
        "so its sound is a collaboration. The profile describes the record, not one producer's signature.",
        0.50,
        {{
            f (-3.0, 0.50, 0.7, 3.0, Evidence::interpretation, kRumble),
            f (-1.0, 0.50, 0.7, 3.0, Evidence::interpretation, kTightLow),
            f (-1.5, 0.50, 1.0, 3.5, Evidence::secondary, kMidScoop),
            f (+1.0, 0.75, 0.8, 2.5, Evidence::interpretation, kMidBody),
            f (+2.5, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.0, 1.00, 0.9, 2.5, Evidence::documented, kPresence),
            f (-3.5, 0.50, 0.8, 2.5, Evidence::secondary, kSmoothTop),
            f (-6.0, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (26, "An Ocean Between Us (2007): produced by Adam Dutkiewicz and As I Lay Dying; mixed by Colin Richardson; guitar re-amping by Andy Sneap.", Evidence::documented),
            src (17, "A contemporary review describes the guitars as tight and aggressive ('a saw burning through bone').", Evidence::secondary),
            src (12, "Secondary analysis of Richardson's mix approach: midrange authority and attack, controlled fizz.", Evidence::secondary),
            src (13, "Secondary analysis of Sneap's re-amp/mix approach: tight lows, low-mid control and clear upper-mid presence.", Evidence::secondary)
        }},
        4
    },

    // 10. Miss May I ---------------------------------------------------------
    {
        "artist-miss-may-i",
        "Miss May I — Monument (2010)",
        "Miss May I",
        Category::artist,
        "Miss May I, Monument (2010)",
        "A polished in-the-box metalcore rhythm sound: tight, scooped lows, a bright upper-mid/presence region "
        "and a controlled but modern top.",
        "The profile represents the Joey Sturgis-produced Monument era, not later Miss May I records. "
        "No direct tonal interview from Sturgis about this album was found; gear reports and his general production philosophy inform it.",
        0.50,
        {{
            f (-3.0, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-1.5, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (-2.5, 0.50, 1.0, 3.5, Evidence::documented, kMidScoop),
            f (+1.0, 0.75, 0.7, 2.5, Evidence::secondary, kMidBody),
            f (+2.0, 0.50, 0.9, 3.0, Evidence::documented, kUpperMid),
            f (+2.5, 1.00, 1.0, 2.5, Evidence::documented, kPresence),
            f (-1.0, 0.50, 0.8, 2.5, Evidence::documented, kSmoothTop),
            f (-6.0, 1.50, 0.4, 2.0, Evidence::secondary, kBrightTop)
        }},
        {{
            src (27, "Monument (2010): produced, engineered, mixed and mastered by Joey Sturgis.", Evidence::documented),
            src (18, "Ryan Neff: Peavey 6505+ into Mesa 4x12 with a Tube Screamer and Boss EQ; Sturgis worked fully in-the-box and the band came to value clarity over raw gain.", Evidence::documented),
            src (34, "Sturgis: heavily distorted guitars become linear; weight/density, editing and arrangement matter more than a plugin chain.", Evidence::documented)
        }},
        3
    },

    // 11. Andy Sneap ---------------------------------------------------------
    {
        "producer-andy-sneap",
        "Andy Sneap — Tight & Transparent",
        "Andy Sneap",
        Category::producer,
        "Testament, The Formation of Damnation (2008); Megadeth, Endgame (2009); Killswitch Engage mix work",
        "Tight controlled lows, a purposeful low-mid scoop, articulate upper-mid attack and a smooth top. "
        "Clarity and separation are the priority; the guitars stay aggressive without fizz.",
        "A producer profile, not a claim that every Sneap record shares one curve. The specific EQ moves come from "
        "secondary production analysis; no verified isolated guitar measurement was made.",
        0.60,
        {{
            f (-3.5, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-1.5, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (-2.5, 0.50, 1.0, 3.5, Evidence::secondary, kMidScoop),
            f (+1.0, 0.75, 0.8, 2.5, Evidence::secondary, kMidBody),
            f (+2.5, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+1.5, 1.00, 0.9, 2.5, Evidence::documented, kPresence),
            f (-2.0, 0.50, 0.8, 2.5, Evidence::secondary, kSmoothTop),
            f (-7.0, 1.50, 0.4, 2.0, Evidence::secondary, kBrightTop)
        }},
        {{
            src (15, "Tape Op profiles Sneap's tight, snarling mixes where the bands' chops can be heard with full clarity.", Evidence::documented),
            src (16, "Sneap: he likes clear, transparent productions; the musician's performance must remain audible.", Evidence::documented),
            src (13, "Secondary analysis: 5150/JCM800 amps, SM57+MD421 mics, HPF 80-120 Hz, LPF 8-10 kHz, a 300-500 Hz low-mid scoop and 150-400 Hz multiband control.", Evidence::secondary),
            src (21, "Killswitch Engage's The End of Heartache (2004) was mixed by Sneap, a representative credit.", Evidence::documented)
        }},
        4
    },

    // 12. Colin Richardson ---------------------------------------------------
    {
        "producer-colin-richardson",
        "Colin Richardson — Midrange Authority",
        "Colin Richardson",
        Category::producer,
        "Bullet for My Valentine, The Poison (2005); As I Lay Dying, An Ocean Between Us (2007)",
        "A midrange-forward, attack-led metal guitar sound: tight lows, controlled fizz, strong 700 Hz body "
        "and upper-mid cut. Heavy without turning into a fuzzy wall.",
        "The profile is an interpretation of Richardson's documented production approach across several records. "
        "It does not claim that he used the same EQ on every album.",
        0.55,
        {{
            f (-3.0, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-1.0, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (-1.5, 0.50, 0.9, 3.5, Evidence::secondary, kMidScoop),
            f (+2.0, 0.75, 1.0, 2.5, Evidence::documented, kMidBody),
            f (+3.0, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+2.5, 1.00, 1.0, 2.5, Evidence::documented, kPresence),
            f (-1.5, 0.50, 0.7, 2.5, Evidence::secondary, kSmoothTop),
            f (-7.0, 1.50, 0.4, 2.0, Evidence::secondary, kBrightTop)
        }},
        {{
            src (12, "Secondary analysis of Richardson's approach: midrange authority and attack, less gain, HPF 80-100 Hz, tame 5-10 kHz fizz, a modest 1.5-4 kHz attack boost, SM57+MD421 blend.", Evidence::secondary),
            src (24, "The Poison (2005): produced and mixed by Richardson.", Evidence::documented),
            src (26, "An Ocean Between Us (2007): mixed by Richardson, a representative credit.", Evidence::documented),
            src (11, "Matt Tuck ties The Poison's guitar sound to a 5150, supporting the midrange-forward amp context.", Evidence::documented)
        }},
        4
    },

    // 13. Bob Rock -----------------------------------------------------------
    {
        "producer-bob-rock",
        "Bob Rock — Weight & Smooth Top",
        "Bob Rock",
        Category::producer,
        "Metallica, The Black Album (1991); Mötley Crüe, Dr. Feelgood (1989) as low-end context",
        "A broad hard-rock/heavy-metal destination: generous low-end weight, present low-mids, a warmer midrange "
        "and a smooth, uncompressed top. Size and punch over fizz.",
        "The evidence is concentrated on The Black Album, which is also a genre target. This producer profile is "
        "deliberately broader and includes Dr. Feelgood only as documented low-end context, not as a measured guitar curve.",
        0.65,
        {{
            f (+1.0, 0.50, 1.0, 3.0, Evidence::documented, kRumble),
            f (+3.0, 0.50, 1.0, 3.0, Evidence::documented, kTightLow),
            f (+2.0, 0.50, 0.9, 3.5, Evidence::documented, kMidScoop),
            f (+1.5, 0.75, 0.9, 2.5, Evidence::documented, kMidBody),
            f (-0.5, 0.50, 0.6, 3.0, Evidence::secondary, kUpperMid),
            f (-1.0, 1.00, 0.5, 2.5, Evidence::interpretation, kPresence),
            f (-4.5, 0.50, 0.8, 2.5, Evidence::documented, kSmoothTop),
            f (-9.0, 1.50, 0.5, 2.0, Evidence::documented, kBrightTop)
        }},
        {{
            src (0,  "Black Album: Hetfield's 'scooped' Mesa-plus-other-amps setup, 9-11 cabinets, room mics, guitars uncompressed in the mix.", Evidence::documented),
            src (1,  "Bob Rock: the record added a rich low-end punch that had been missing; live tracking and D tuning contributed to the weight.", Evidence::documented),
            src (2,  "The band wanted Dr. Feelgood's bottom end; Rock had just produced that record.", Evidence::documented),
            src (31, "The Black Album credits: produced by Bob Rock, engineered by Randy Staub.", Evidence::documented)
        }},
        4
    },

    // 14. Flemming Rasmussen ------------------------------------------------
    {
        "producer-flemming-rasmussen",
        "Flemming Rasmussen — Mid-Focused Thrash",
        "Flemming Rasmussen",
        Category::producer,
        "Metallica, Master of Puppets (1986)",
        "A mid-focused, tight thrash destination: solid midrange body, controlled low end, "
        "articulate upper-mids and a smooth rather than bright top. Layered rhythm guitars with a dry, immediate feel.",
        "The sources document gear, mic technique and a studio EQ insert, but not its frequencies. "
        "AJFA is used only as context; the model is for Master of Puppets.",
        0.60,
        {{
            f (-3.0, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-1.0, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (+1.0, 0.50, 0.8, 3.5, Evidence::documented, kMidScoop),
            f (+2.0, 0.75, 1.0, 2.5, Evidence::documented, kMidBody),
            f (+2.5, 0.50, 1.0, 3.0, Evidence::documented, kUpperMid),
            f (+1.0, 1.00, 0.8, 2.5, Evidence::secondary, kPresence),
            f (-2.0, 0.50, 0.8, 2.5, Evidence::documented, kSmoothTop),
            f (-7.5, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (6,  "Rasmussen on Master of Puppets: Mesa/Boogie Mark II C+ preamp into a Marshall power amp and 4x12s; two SM57s plus DPA room mics and AKG Gold tubes; a studio parametric EQ inserted; double/quad-tracked rhythms.", Evidence::documented),
            src (33, "Master of Puppets (1986) credits: produced by Metallica and Flemming Rasmussen.", Evidence::documented),
            src (5,  "Rasmussen on AJFA: the band ordered the bass down; Hetfield later linked the treble-heavy result to fried ears. Used as context for the producer's dry approach.", Evidence::documented)
        }},
        3
    },

    // 15. Mike Clink ---------------------------------------------------------
    {
        "producer-mike-clink",
        "Mike Clink — Raw Hard-Rock Midrange",
        "Mike Clink",
        Category::producer,
        "Guns N' Roses, Appetite for Destruction (1987)",
        "A raw, midrange-forward hard-rock destination: moderate gain, tight lows, strong 700 Hz body "
        "and a smooth top. Performance and midrange cut matter more than modern metal-core scoop or fizz.",
        "This is a hard-rock profile, not a metalcore one. No direct Clink EQ interview was found; "
        "the model is based on the documented guitar/amp choices and the record's raw production context.",
        0.45,
        {{
            f (-3.5, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (-1.5, 0.50, 0.8, 3.0, Evidence::documented, kTightLow),
            f (+1.0, 0.50, 0.9, 3.5, Evidence::documented, kMidScoop),
            f (+2.0, 0.75, 1.0, 2.5, Evidence::documented, kMidBody),
            f (+2.0, 0.50, 0.9, 3.0, Evidence::documented, kUpperMid),
            f (+1.0, 1.00, 0.8, 2.5, Evidence::documented, kPresence),
            f (-1.5, 0.50, 0.7, 2.5, Evidence::secondary, kSmoothTop),
            f (-8.0, 1.50, 0.4, 2.0, Evidence::documented, kBrightTop)
        }},
        {{
            src (30, "Appetite for Destruction (1987): produced by Mike Clink; Slash used a Les Paul copy with Seymour Duncan Alnico II pickups into a Marshall; final overdubs and mixing at Mediasound.", Evidence::documented),
            src (20, "Slash: he went through three or four Marshall heads to find the one that matched the Les Paul; the sound came together at the last minute.", Evidence::documented),
            src (28, "Rust in Peace (1990): co-produced by Mike Clink, a documented thrash credit alongside Appetite.", Evidence::documented)
        }},
        3
    },

    // 16. Max Norman ---------------------------------------------------------
    {
        "producer-max-norman",
        "Max Norman — Polished Thrash Weight",
        "Max Norman",
        Category::producer,
        "Megadeth, Countdown to Extinction (1992); Ozzy Osbourne, Diary of a Madman (1981) as guitar-layering context",
        "A polished, weighty metal destination: fuller lows than Rust in Peace, present low-mids, "
        "clear upper-mids and a smooth top. Lead layers and careful pre-production are part of the picture.",
        "Evidence is thin and spans two very different productions. The profile is an interpretation of Norman's "
        "documented credits and working methods, not a measured Countdown to Extinction guitar curve.",
        0.40,
        {{
            f (-2.5, 0.50, 0.8, 3.0, Evidence::documented, kRumble),
            f (+1.0, 0.50, 0.9, 3.0, Evidence::documented, kTightLow),
            f (+1.0, 0.50, 0.8, 3.5, Evidence::documented, kMidScoop),
            f (+1.5, 0.75, 0.9, 2.5, Evidence::documented, kMidBody),
            f (+2.0, 0.50, 0.9, 3.0, Evidence::documented, kUpperMid),
            f (+1.0, 1.00, 0.8, 2.5, Evidence::secondary, kPresence),
            f (-2.5, 0.50, 0.8, 2.5, Evidence::documented, kSmoothTop),
            f (-7.5, 1.50, 0.4, 2.0, Evidence::interpretation, kBrightTop)
        }},
        {{
            src (29, "Countdown to Extinction (1992): produced and mixed by Max Norman; a more groove/melody-oriented Megadeth record with meticulous pre-production.", Evidence::documented),
            src (19, "Max Norman: Randy Rhoads triple-tracked his solos on Blizzard of Ozz and Diary of a Madman, documenting a layered-guitar approach.", Evidence::documented),
            src (28, "Norman mixed Rust in Peace (1990), anchoring him in Megadeth's thrash catalogue.", Evidence::documented)
        }},
        3
    }
}};

//------------------------------------------------------------------------------
// Input analysis helpers
//------------------------------------------------------------------------------
constexpr int kFftOrder = 13;
constexpr int kFftSize  = 1 << kFftOrder;

double bandPowerDb (const std::vector<double>& power, double lowHz, double highHz,
                    double binHz, double nyquist)
{
    lowHz  = std::max (1.0, lowHz);
    highHz = std::min (highHz, nyquist * 0.95);
    if (highHz <= lowHz)
        return -160.0;

    int lo = static_cast<int> (std::ceil (lowHz / binHz));
    int hi = static_cast<int> (std::floor (highHz / binHz));
    lo = std::max (1, lo);
    hi = std::min (static_cast<int> (power.size()) - 1, hi);

    if (hi < lo)
    {
        // Fall back to the nearest bin for very low bands at low sample rates.
        const int nearest = std::max (1, std::min (static_cast<int> (power.size()) - 1,
                                                   static_cast<int> (std::lround (lowHz / binHz))));
        return 10.0 * std::log10 (power[static_cast<size_t> (nearest)] + 1.0e-20);
    }

    double sum = 0.0;
    for (int bin = lo; bin <= hi; ++bin)
        sum += power[static_cast<size_t> (bin)];

    return 10.0 * std::log10 (sum / static_cast<double> (hi - lo + 1) + 1.0e-20);
}

} // namespace

//==============================================================================
int getNumToneTargets() noexcept { return static_cast<int> (kTargets.size()); }

const ToneTarget& getToneTarget (int index) noexcept
{
    return kTargets[static_cast<size_t> (
        clampValue (index, 0, getNumToneTargets() - 1))];
}

int findToneTargetById (const char* id) noexcept
{
    if (id == nullptr || *id == 0)
        return -1;

    for (int i = 0; i < getNumToneTargets(); ++i)
        if (std::strcmp (kTargets[static_cast<size_t> (i)].id, id) == 0)
            return i;

    return -1;
}

int getNumSources() noexcept { return static_cast<int> (kSources.size()); }

const Source& getSource (int index) noexcept
{
    return kSources[static_cast<size_t> (
        clampValue (index, 0, getNumSources() - 1))];
}

const char* categoryLabel (Category category) noexcept
{
    switch (category)
    {
        case Category::genre:    return "Genres";
        case Category::artist:   return "Artists";
        case Category::producer: return "Producers";
        default:                 return "Genres";
    }
}

const char* evidenceLabel (Evidence evidence) noexcept
{
    switch (evidence)
    {
        case Evidence::documented:     return "documented";
        case Evidence::secondary:      return "secondary analysis";
        case Evidence::interpretation: return "engineering interpretation";
        case Evidence::unknown:        return "unknown";
        default:                       return "engineering interpretation";
    }
}

const char* evidenceTag (Evidence evidence) noexcept
{
    switch (evidence)
    {
        case Evidence::documented:     return "DOC";
        case Evidence::secondary:      return "SEC";
        case Evidence::interpretation: return "EST";
        case Evidence::unknown:        return "UNK";
        default:                       return "EST";
    }
}

//==============================================================================
std::array<double, kNumFeatures> targetShape (const ToneTarget& target) noexcept
{
    std::array<double, kNumFeatures> shape {};
    for (int i = 0; i < kNumFeatures; ++i)
        shape[static_cast<size_t> (i)] = target.features[static_cast<size_t> (i)].relativeDb;
    return shape;
}

//==============================================================================
InputProfile analyseInput (const float* samples, int numSamples, double sampleRate)
{
    InputProfile result;

    if (samples == nullptr || numSamples < kFftSize || ! (sampleRate > 0.0))
        return result;

    const double binHz = sampleRate / static_cast<double> (kFftSize);
    const double nyquist = sampleRate * 0.5;

    std::vector<double> power (static_cast<size_t> (kFftSize / 2 + 1), 0.0);
    std::vector<double> re (static_cast<size_t> (kFftSize));
    std::vector<double> im (static_cast<size_t> (kFftSize));
    std::vector<double> window (static_cast<size_t> (kFftSize));

    for (int i = 0; i < kFftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (
            2.0 * kPi * static_cast<double> (i) / static_cast<double> (kFftSize - 1));

    double totalSquare = 0.0;
    int frames = 0;

    for (int offset = 0; offset + kFftSize <= numSamples; offset += kFftSize / 2)
    {
        double frameSquare = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            const double sample = static_cast<double> (samples[offset + i]);
            frameSquare += sample * sample;
            re[static_cast<size_t> (i)] = sample * window[static_cast<size_t> (i)];
            im[static_cast<size_t> (i)] = 0.0;
        }

        const double meanSquare = frameSquare / static_cast<double> (kFftSize);
        if (meanSquare < 1.0e-8) // roughly -80 dBFS per frame; ignore silence
            continue;

        metaleq::detail::fftRadix2 (re, im);

        for (int bin = 1; bin <= kFftSize / 2; ++bin)
        {
            const double p = re[static_cast<size_t> (bin)] * re[static_cast<size_t> (bin)]
                           + im[static_cast<size_t> (bin)] * im[static_cast<size_t> (bin)];
            power[static_cast<size_t> (bin)] += p;
        }

        totalSquare += frameSquare;
        ++frames;
    }

    if (frames == 0)
        return result;

    for (auto& p : power)
        p /= static_cast<double> (frames);

    const double referenceDb = bandPowerDb (power, 500.0, 2000.0, binHz, nyquist);
    if (! std::isfinite (referenceDb) || referenceDb < -120.0)
        return result;

    for (int i = 0; i < kNumFeatures; ++i)
    {
        const auto& spec = kFeatures[static_cast<size_t> (i)];
        const double bandDb = bandPowerDb (power, spec.lowHz, spec.highHz, binHz, nyquist);
        result.relativeDb[static_cast<size_t> (i)] = bandDb - referenceDb;
    }

    const double rms = std::sqrt (totalSquare / static_cast<double> (frames * kFftSize));
    const double rmsDb = 20.0 * std::log10 (rms + 1.0e-20);
    const double frameScore = clampDouble (static_cast<double> (frames) / 60.0, 0.0, 1.0);
    const double levelScore = clampDouble ((rmsDb + 60.0) / 30.0, 0.0, 1.0);

    result.confidence = clampDouble ((0.35 + 0.65 * frameScore) * (0.4 + 0.6 * levelScore), 0.0, 1.0);
    result.referenceDb = referenceDb;
    result.frames = frames;
    result.valid = result.confidence > 0.05;
    return result;
}

//==============================================================================
double peakingResponseAtRate (double freq, double centreHz, double q,
                              double gainDb, double sampleRate) noexcept
{
    if (! (freq > 0.0) || ! (centreHz > 0.0) || ! (q > 0.0)
        || ! (sampleRate > 0.0) || ! std::isfinite (gainDb))
        return 0.0;

    const double w0 = 2.0 * kPi * centreHz / sampleRate;
    const double cw = std::cos (w0);
    const double sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cw;
    const double a2 = 1.0 - alpha / A;

    const double w = 2.0 * kPi * freq / sampleRate;
    const double cos1 = std::cos (w), sin1 = std::sin (w);
    const double cos2 = std::cos (2.0 * w), sin2 = std::sin (2.0 * w);

    const double numRe = b0 + b1 * cos1 + b2 * cos2;
    const double numIm = -(b1 * sin1 + b2 * sin2);
    const double denRe = a0 + a1 * cos1 + a2 * cos2;
    const double denIm = -(a1 * sin1 + a2 * sin2);

    const double numMag = std::sqrt (numRe * numRe + numIm * numIm);
    const double denMag = std::sqrt (denRe * denRe + denIm * denIm);
    if (denMag <= 1.0e-20)
        return 0.0;

    return 20.0 * std::log10 (numMag / denMag + 1.0e-20);
}

double peakingResponseDb (double freq, double centreHz, double q, double gainDb) noexcept
{
    return peakingResponseAtRate (freq, centreHz, q, gainDb, 48000.0);
}

std::array<double, kNumFeatures> predictShape (
    const std::array<double, kNumFeatures>& inputRelativeDb,
    const std::array<double, kNumCorrectionBands>& gainsDb,
    double sampleRate)
{
    std::array<double, kNumFeatures> predicted = inputRelativeDb;

    for (int i = 0; i < kNumFeatures; ++i)
    {
        double eqDb = 0.0;
        for (int j = 0; j < kNumCorrectionBands; ++j)
            eqDb += peakingResponseAtRate (kFeatures[static_cast<size_t> (i)].centreHz,
                                           kCorrectionFrequencies[static_cast<size_t> (j)],
                                           kCorrectionQ,
                                           gainsDb[static_cast<size_t> (j)],
                                           sampleRate);
        predicted[static_cast<size_t> (i)] += eqDb;
    }

    return predicted;
}

//==============================================================================
CorrectionResult computeCorrection (const InputProfile& input,
                                    const ToneTarget& target,
                                    double sampleRate)
{
    CorrectionResult result;
    result.targetDb = targetShape (target);

    if (! input.valid || ! (sampleRate > 0.0))
        return result;

    const double measurement = clampDouble (input.confidence, 0.0, 1.0);
    const double targetConfidence = clampDouble (target.confidence, 0.0, 1.0);
    const double confidenceScale = 0.80 + 0.20 * std::min (measurement, targetConfidence);

    // Desired per-feature change: target minus input, with a deadband so an
    // already-suitable region is not "corrected" for no reason.
    std::array<double, kNumFeatures> desired {};
    for (int i = 0; i < kNumFeatures; ++i)
    {
        const auto& ft = target.features[static_cast<size_t> (i)];
        const double delta = result.targetDb[static_cast<size_t> (i)]
                           - input.relativeDb[static_cast<size_t> (i)];
        const double tol = std::max (0.0, ft.toleranceDb);
        double d = 0.0;
        if (std::abs (delta) > tol)
            d = delta - std::copysign (tol, delta);
        d = clampDouble (d * confidenceScale, -ft.maxCorrectionDb, ft.maxCorrectionDb);
        desired[static_cast<size_t> (i)] = d;
        result.desiredDb[static_cast<size_t> (i)] = d;
    }

    // Weighted least-squares fit onto the five broad peaking bands. M[i][j] is
    // the response at feature i of a 1 dB peaking section at correction band j.
    constexpr int n = kNumCorrectionBands;
    double a[n][n] {};
    double b[n] {};
    double sensitivity[kNumFeatures][n] {};

    for (int i = 0; i < kNumFeatures; ++i)
        for (int j = 0; j < n; ++j)
            sensitivity[i][j] = peakingResponseAtRate (kFeatures[static_cast<size_t> (i)].centreHz,
                                                       kCorrectionFrequencies[static_cast<size_t> (j)],
                                                       kCorrectionQ, 1.0, sampleRate);

    for (int i = 0; i < kNumFeatures; ++i)
    {
        const double w = std::max (0.0, target.features[static_cast<size_t> (i)].weight);
        if (w <= 0.0)
            continue;

        for (int j = 0; j < n; ++j)
        {
            b[j] += w * sensitivity[i][j] * desired[static_cast<size_t> (i)];
            for (int k = 0; k < n; ++k)
                a[j][k] += w * sensitivity[i][j] * sensitivity[i][k];
        }
    }

    // Small ridge term keeps the fit stable when several broad bands overlap.
    for (int j = 0; j < n; ++j)
        a[j][j] += 0.12;

    // Gaussian elimination with partial pivoting.
    double augmented[n][n + 1] {};
    for (int row = 0; row < n; ++row)
    {
        for (int col = 0; col < n; ++col)
            augmented[row][col] = a[row][col];
        augmented[row][n] = b[row];
    }

    bool singular = false;
    for (int col = 0; col < n && ! singular; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs (augmented[row][col]) > std::abs (augmented[pivot][col]))
                pivot = row;

        if (std::abs (augmented[pivot][col]) < 1.0e-12)
        {
            singular = true;
            break;
        }

        if (pivot != col)
            for (int k = col; k <= n; ++k)
                std::swap (augmented[col][k], augmented[pivot][k]);

        const double pivotValue = augmented[col][col];
        for (int k = col; k <= n; ++k)
            augmented[col][k] /= pivotValue;

        for (int row = 0; row < n; ++row)
        {
            if (row == col)
                continue;
            const double factor = augmented[row][col];
            if (factor == 0.0)
                continue;
            for (int k = col; k <= n; ++k)
                augmented[row][k] -= factor * augmented[col][k];
        }
    }

    if (! singular)
        for (int i = 0; i < n; ++i)
            result.gainsDb[static_cast<size_t> (i)] =
                clampDouble (augmented[i][n], -kGlobalMaxGainDb, kGlobalMaxGainDb);

    result.predictedDb = predictShape (input.relativeDb, result.gainsDb, sampleRate);

    double maxGain = 0.0;
    double maxResidual = 0.0;
    for (int i = 0; i < n; ++i)
        maxGain = std::max (maxGain, std::abs (result.gainsDb[static_cast<size_t> (i)]));
    for (int i = 0; i < kNumFeatures; ++i)
        if (target.features[static_cast<size_t> (i)].weight >= 0.4)
            maxResidual = std::max (maxResidual,
                                    std::abs (result.targetDb[static_cast<size_t> (i)]
                                              - result.predictedDb[static_cast<size_t> (i)]));

    result.maxAbsGainDb = maxGain;
    result.residualDb = maxResidual;
    result.alreadyClose = maxGain < 0.25;
    result.confidence = clampDouble (
        targetConfidence * measurement * clampDouble (1.0 - maxResidual / 9.0, 0.25, 1.0),
        0.0, 1.0);

    return result;
}

bool correctionIsMeaningful (const std::array<double, kNumCorrectionBands>& gainsDb) noexcept
{
    for (const double gain : gainsDb)
        if (std::abs (gain) >= 0.25)
            return true;
    return false;
}

} // namespace metaleq::tone
