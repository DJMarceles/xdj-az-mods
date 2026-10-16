# Drum roll sample format

Drum Roll sample banks live in `DRUM ROLLS/` on a USB stick: four banks, each with up to
four one-shot samples (kick, snare, clap, hihat) in WAV or Ogg Vorbis.

| Side | Component | Source |
|---|---|---|
| Reader | `azstem.so` | `src/azstem/drumroll.c` (bank discovery, file roles, WAV decoding, playback); Ogg Vorbis goes through the stems decoder (`decode_ogg_stereo_44100` in `src/azstem/stem_playback.c`) |
| Controls | `azmod.so` | `src/azmod/drum_roll_display.c`, `src/azmod/bus_interpose.c`, `src/azmod/mod_menu_overlay.c` |
| Writer | AZ-MODS desktop app | copies a user's sample into a bank slot |

"Must" marks what the deck needs to play a sample. "Should" marks what the AZ-MODS app
does, which keeps a bank interchangeable with its banks.

## Controls

Drum Roll is a sampler on the X-PAD, switched on in the Mod Menu. On a deck in SLIP LOOP
pad mode:

| Control | Action |
|---|---|
| Pad 8 | arms the roll on that deck; press again to disarm. Arming one deck disarms the others |
| Pads 1 to 4 (armed) | choose the sample: kick, snare, clap, hihat |
| Pads 5 to 7 (armed) | nothing: pads 1 to 7 are kept from EP147 while armed |
| X-PAD (armed) | a finger plays the chosen sample as a roll. The six zones, left to right, retrigger every 1, 1/2, 1/4, 1/8, 1/16 and 1/32 beat. The X-PAD's frames still reach EP147 unchanged, so the selected Beat FX engages with the roll; turn the FX down for a dry roll |
| Beat FX ON/OFF, FX select knob | disarm every deck |
| MIC 2 HI | roll volume: 12 o'clock unchanged, fully left silent, fully right +6 dB |
| MIC 2 MID | roll pitch: 12 o'clock original, plus or minus 12 semitones at the ends |

**Timing.** The roll runs at the deck's BPM (120 when the BPM is unknown, clamped to 40
to 220). When the track has a beat grid and the deck is playing, hits land on the grid
(Beat Sync, always on in release builds), and the first hit waits for the next boundary
of the chosen rate. On a paused or empty deck, or a track without a grid, the roll starts
at the moment of the press.

**Playback.** Each hit plays the sample from its start, and the next hit cuts the
previous one with a short fade. The roll is mixed on top of the deck's audio, after the
deck's own processing.

## Layout

```
<stick>/
  DRUM ROLLS/
    Bank 1/
      kick.wav
      snare.wav
      clap.wav
      hihat.wav
    Bank 2/
      kick.ogg
      ...
    Bank 3/
    Bank 4/
```

| Rule | Detail |
|---|---|
| `DRUM ROLLS` | a directory, name matched ignoring ASCII case (`Drum Rolls` works), exactly one space |
| `Bank 1` to `Bank 4` | directories directly inside it, matched ignoring ASCII case, one space, no leading zero (`Bank 01` and `Bank1` are not found). There are exactly four banks |
| Dot names | entries whose name starts with `.` are skipped everywhere, so macOS `._name` files are never taken for a sample |
| Anything else | in `DRUM ROLLS/` or in a bank: ignored |

### Which USB

The deck looks for `DRUM ROLLS/` on every mounted USB, not only the one carrying the
mods: under `/media/usb`, `/run/media` and `/media` (searched in that order), in each
entry and one folder level below it (so `<stick>/<folder>/DRUM ROLLS/` is also found).
The first match wins; with two USBs that both hold `DRUM ROLLS/`, directory order
decides, so which one is used is not defined.

| When | What the deck does |
|---|---|
| No `DRUM ROLLS/` found yet | searches every 5 seconds |
| `DRUM ROLLS/` found | records which of `Bank 1` to `Bank 4` exist |
| After that, once a second | checks that the folder is still there and still the same folder (same device and inode) |
| USB removed, or another USB in its place (even at the same mount path) | searches again at once and records the banks again |
| A `Bank N` folder added or removed while the USB stays in | the recorded list is not refreshed until the USB is reinserted or the deck software restarts. The list only drives the "no samples" message below: loading a bank looks its folder up by name at that moment, so a bank added later still loads when it is selected |

### Which bank

The Mod Menu's Drum Roll page selects one of Bank 1 to Bank 4; a fresh setup selects
Bank 1. The choice is saved as `drum-roll-bank` in `azmod.conf` (`0` = Bank 1 to `3` =
Bank 4; see [MOD-MENU-KEYS.md](MOD-MENU-KEYS.md)), and is also written one-hot to
positions 7 to 10 of `azmod.cfg` line 1 (see [USB-STICK.md](USB-STICK.md#azmodcfg)).

When no `Bank N` folder was found (or no `DRUM ROLLS/` at all), the Drum Roll row reads "Drum Roll - no samples on
USB" and its page shows "No drum roll samples on this USB."

## When samples load

Samples are decoded into memory; the files are open only while that happens. A bank
loads:

- when Drum Roll is turned on (the selected bank);
- when another bank is selected while Drum Roll is on;
- when `DRUM ROLLS/` is found while Drum Roll is already on (the USB went in after the
  deck started, or replaced the USB the bank came from).

While Drum Roll is off nothing is decoded, so the USB's files are never held open and
ejecting the USB never waits on them.

Loading runs on azstem's background thread, which looks for work every 200 ms; the bank
is ready as soon as its files have decoded. The new bank replaces the old one as a
whole, and only once at least one of its files has decoded.

| Event | Effect on the loaded samples |
|---|---|
| A file on the USB is replaced | not heard until the bank loads again: turn Drum Roll off and on, or select another bank and back |
| An empty or missing bank is selected | no file decodes, so the previous bank keeps playing |
| Drum Roll is turned off | the bank stays in memory until another bank replaces it or the deck software restarts |
| The USB is removed | the bank keeps playing from memory. A different USB holding the selected bank replaces it; one that does not leaves the previous samples playing |

### Memory

Every sample is held as 32-bit float stereo at 44100 Hz: 8 bytes per frame, 352,800
bytes per second of audio. Four 1-second one-shots take about 1.4 MB.

Each sample is capped at **8 seconds**. A longer file is not refused: only its first
8 seconds are read from the USB and decoded, and the end fades out like any other
sample. The slowest roll rate is one beat, 1.5 seconds at the 40 BPM floor, so audio
past that plays only as the tail after the finger lifts. A full bank at the cap is about
11 MB.

## Files and pads

The deck reads a bank folder in directory order and considers each entry whose name does
not start with `.` and ends in `.wav` or `.ogg`, ignoring case. Every other file is
skipped.

Each considered file gets a slot from these words in its name, ignoring ASCII case,
after removing every `-`, `_` and space, tested in this order:

| Order | Slot | Pad | Matches |
|---|---|---|---|
| 1 | kick | 1 | `kick` |
| 2 | snare | 2 | `snare` |
| 3 | clap | 3 | `clap` |
| 4 | hihat | 4 | `hihat` or `hat` |

The extension is part of the name tested; neither `.wav` nor `.ogg` contains a slot word.

| Name | Slot |
|---|---|
| `Kick Clap.wav` | kick (the first test that matches decides) |
| `Hi-Hat 3.ogg`, `hi_hat.wav`, `Open Hat.wav` | hihat |
| `What.wav` | hihat (`hat` matches inside other words) |
| `01 Kick.wav`, `KICK.WAV` | kick |
| `vocal chop.wav`, `perc.wav` | none: skipped |

When two files match the same slot, the first in directory order is used and the rest
are skipped. Directory order on FAT and exFAT is not alphabetical, so a bank with two
files for one slot plays whichever the file system lists first.

Writers should name the files exactly `kick`, `snare`, `clap` and `hihat`, with the
extension `.wav` or `.ogg` in lowercase, which is what the AZ-MODS app writes.

### Missing and bad files

| Situation | Deck behavior |
|---|---|
| A slot has no file, or its file fails to decode | the slot is empty; its pad still selects it, and the roll then plays nothing |
| At least one slot decoded | the bank plays |
| A file fails to decode | skipped; nothing is shown on the deck. With `/home/root/settings/azmod-logs.on` present when the deck software starts, `/tmp/azstem.log` names the reason for every skipped file (see [USB-STICK.md](USB-STICK.md#logs)) |

## Audio encoding

**Ogg Vorbis** (`.ogg`): decoded with stb_vorbis to 32-bit float, up to the 8-second
cap. A file the deck cannot get the memory to resample to 44100 Hz is skipped, never
played at the wrong pitch.

**WAV** (`.wav`): a RIFF `WAVE` file. The deck walks its chunks, reads `fmt `, and stops
at the first `data` chunk; other chunks are skipped. Accepted:

| `fmt ` format | Bits per sample |
|---|---|
| 1 (PCM) | 8 (unsigned), 16, 24, 32 (signed, little-endian) |
| 3 (IEEE float) | 32, 64 |
| 0xFFFE (extensible) | as PCM or float, when its subformat GUID is the standard PCM or IEEE float one and its extension is at least 22 bytes |

Refused (the slot reads as missing):

| Refused | Examples |
|---|---|
| any other format code | ADPCM, A-law, mu-law, MP3 in WAV |
| any other bit depth | 12 or 20 bits in a plain PCM header |
| channel count | 0, or more than 32 |
| sample rate | below 4000 Hz or above 384000 Hz |
| container | RF64, big-endian (`RIFX`) |
| chunk order | a `data` chunk before `fmt ` |

For both types:

| Property | Deck behavior |
|---|---|
| Sample rate | any (WAV within the limits above). Rates other than 44100 Hz are resampled to 44100 Hz with linear interpolation when the bank loads |
| Channels | stereo as is; mono copied to both sides; more than two channels: the first two |
| Length | up to 8 seconds are used; the rest of a longer file is ignored |
| Level | samples are used as decoded; float values beyond full scale are kept. Normalize in the file |
| Start | each hit starts at the first sample, with a 16-frame fade-in. Leading silence in the file delays every hit |
| End | the last 192 frames fade out; the next hit also fades the previous one out over 192 frames |

**What AZ-MODS writes.** The app copies the user's file into the bank unchanged: no
conversion, no trimming, no format check beyond the extension. Third-party writers
should write 44100 Hz, 16-bit or 24-bit PCM WAV (or Ogg Vorbis), mono or stereo, with
the hit starting on the first sample.

## How the AZ-MODS app writes a bank

The app shows the four banks with a kick, snare, clap and hihat slot each. For each slot
it shows the file the deck would pick, using the deck's rules (but see
[Where writer and reader disagree](#where-writer-and-reader-disagree)).

Adding a sample to a slot:

1. it refuses any extension other than `.wav` and `.ogg`;
2. it finds `DRUM ROLLS` and `Bank N` at the root of the USB ignoring case, creating
   them (in exactly those spellings) when absent;
3. it deletes every file in that bank that its own slot rule assigns to the same slot
   (the deck's rule, with the differences listed under
   [Where writer and reader disagree](#where-writer-and-reader-disagree)), so the slot has
   one file only;
4. it copies the user's file to `Bank N/<slot>.<ext>` (`kick.wav`, `hihat.ogg`, ...),
   without extended attributes, then removes the macOS `._` file beside it when there is
   one.

Clearing a slot deletes every file in the bank that the app's slot rule assigns to it.

The app also creates an empty `DRUM ROLLS/` at the root when it prepares a USB. It
creates a `Bank N` folder only when a sample is added to it.

## Writing a bank safely

The general rules for writing to the USB (stage under a dot name, rename into place,
remove AppleDouble files) are in
[STEMS-FORMAT.md, "Writing a folder safely"](STEMS-FORMAT.md#writing-a-folder-safely).
For a bank:

1. Find an existing `DRUM ROLLS` at the root of the USB ignoring case, or create
   `DRUM ROLLS`. Do the same for `Bank N` inside it.
2. Remove any file in the bank that would match the slot being written (apply the rules
   above, or remove all four slots when replacing a whole bank).
3. Write each file under a dot name, for example `.kick.wav.part`, flush it, then rename
   it to `kick.wav`.
4. On macOS, delete the `._<name>` AppleDouble files.

The deck decodes a bank only when it loads it (see [When samples load](#when-samples-load)),
so a bank written while Drum Roll is off is never read half-written. A bank that changed
while it was loaded is heard after Drum Roll is turned off and on, or after switching
banks.

## Where writer and reader disagree

| Topic | Deck | AZ-MODS app |
|---|---|---|
| Separators | removes only `-`, `_` and spaces before matching, and lowers only ASCII letters | removes every character that is not a letter or digit, and lowers any letter. `Sn.are.wav` is snare to the app and matches nothing on the deck. The files the app writes itself read the same on both |
| Duplicates | plays the first in directory order | shows the first in name order. The app never writes duplicates; third-party writers must not either |
| Decodability | refuses a compressed WAV, an unusual bit depth or a damaged file, and leaves the slot empty (see [Audio encoding](#audio-encoding)) | accepts any `.wav` or `.ogg` and shows it in its slot |
| Length | uses the first 8 seconds | copies a file of any length |
| Location | also finds a `DRUM ROLLS` one folder deeper | reads and writes only `<stick>/DRUM ROLLS` |

## Requirements summary

| Item | Deck | AZ-MODS app |
|---|---|---|
| `DRUM ROLLS/` at the USB root (ASCII case ignored) | required (one level deeper also found) | written as `DRUM ROLLS` |
| `Bank 1` to `Bank 4` (ASCII case ignored) | required | written as `Bank N` |
| `.wav` or `.ogg` extension | required | required |
| Slot word in the name | required (substring, first match in the order kick, snare, clap, hihat) | writes `kick`, `snare`, `clap`, `hihat` |
| One file per slot | recommended (otherwise directory order decides) | enforced on add |
| All four slots | optional | optional |
| PCM or float WAV, or Ogg Vorbis | required to decode | not checked |
| 44100 Hz | recommended (other rates are resampled) | not checked |
| Mono or stereo | both work | not checked |
| Short one-shot, hit on the first sample | recommended (only the first 8 s are used) | not checked |

## Worked example

A tool puts four samples into Bank 2 of a USB mounted at `/Volumes/USB`: `BD_909.wav`
(48 kHz, 24-bit mono), `Snare Tight.wav` (44.1 kHz, 16-bit stereo), `clap.ogg` and
`OH 01.wav`.

1. `/Volumes/USB/Drum Rolls` already exists, so it is used as is. It holds no `Bank 2`,
   so create `/Volumes/USB/Drum Rolls/Bank 2`.
2. Name each file for its slot. `BD_909.wav` contains no slot word, so it must be
   renamed; `OH 01.wav` would also match nothing. The tool writes:

   ```
   Drum Rolls/Bank 2/
     kick.wav      from BD_909.wav (48 kHz; resampled at load)
     snare.wav     from Snare Tight.wav
     clap.ogg      from clap.ogg
     hihat.wav     from OH 01.wav
   ```

   each first as `.kick.wav.part` and so on, renamed into place once written.
3. Delete any `._kick.wav` and similar files macOS created.

On the deck: Mod Menu > Drum Roll on, Bank 2 selected. Load a track, put the deck in
SLIP LOOP, press pad 8 to arm, press pad 2 for the snare and hold a finger on the X-PAD:
the snare rolls at the rate of the zone under the finger. Pad 1 switches to the kick,
decoded from 48 kHz mono to 44.1 kHz stereo when the bank loaded.

With the original names, the deck would have loaded only `Snare Tight.wav` (snare) and
`clap.ogg` (clap); pads 1 and 4 would play nothing.
