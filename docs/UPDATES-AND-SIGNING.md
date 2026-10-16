# Updates and signing

| Side | Source | Role |
|---|---|---|
| Deck | `deck/stick/deck/lib/az-modupdate` | the on-deck updater: `check`, `update`, `staged` |
| Deck | `deck/payload/usr/sbin/az-usbload` | the loader: runs the updater (`modupdate_watch`), applies its result, enforces the trust pins (the TRUST block) |
| Deck | `src/azmod/updates.c`, `src/azmod/changelog.c` | the Updates screen (Mod Menu > Updates) and its changelog box |
| Release | `deck/make_pins.py`, `deck/stage_stick.py` | write `deck/trust.pins`, lay out `MODS/deck/` |
| Computer | the AZ-MODS desktop app | the other reader. Its source is not in this tree; only the formats and rules it shares with the deck are given here |

"Must" marks what both readers need to accept a release. "Should" marks what the official
releases do.

## Overview

```
 az-mods-updates (GitHub releases)          desktop app              deck
 ---------------------------------          -----------              ----
 tag mods-v1.2.3
   manifest.json + .minisig  ----------->   verify both     |    verify both
   mods-v1.2.3.tar.gz + .minisig -------->  signatures,     |    signatures,
   CHANGELOG.txt + .minisig                 pin every file, |    pin every file,
                                            keep the set    |    write MODS/,
                                            on the computer |    stage the set
                                                  |              (applied by Reload
                                                  v               or the next boot)
                                            write MODS/ on a stick
                                            (applied at the deck's next boot)
```

| Trust root | Where | What it signs |
|---|---|---|
| One minisign public key, key id `5B07109A2F3C24FE` | embedded in `az-modupdate` (`AZ_PUB`), the loader (`TRUST_PUB`) and the desktop app | `manifest.json`, the archive, `CHANGELOG.txt`, `deck/trust.pins` |

The manifest pins every file of the set by SHA-256. Nothing in a release, a pointer file or
a repository name changes which key a reader trusts. The same key decides what a USB may
run: see [The deck's trust pins](#the-decks-trust-pins).

## The channel

A channel is the set of GitHub releases of one repository. The official channel is
[az-mods-updates](https://github.com/Kyle-Hosman/az-mods-updates/releases).

### Finding the channel repository

Both readers follow a pointer, `channel.json`, at the root of the default branch of the
source repository:

```json
{"repo": "Kyle-Hosman/az-mods-updates"}
```

| Reader | Pointer URL | Timeout | Accepted `repo` | Fallback |
|---|---|---|---|---|
| deck | `https://raw.githubusercontent.com/Kyle-Hosman/xdj-az-mods/main/channel.json` | 10 s | `[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+` | `Kyle-Hosman/az-mods-updates` |
| app | the same URL | 30 s | `[\w.-]+/[\w.-]+` | the same repository |

Anything else in `repo`, or any failure to fetch, means the fallback. The pointer moves the
channel without a new mods or app release. It carries no trust: a repository it names still
has to serve sets signed with the embedded key.

### Which release is the newest

A release counts when its tag is `mods-v<version>` and it is not a draft.

| | Deck (`az-modupdate`, `newest`) | App |
|---|---|---|
| API request | `GET /repos/<repo>/releases?per_page=30` | `GET /repos/<repo>/releases?per_page=100` |
| Tag pattern | `mods-v` + exactly three numeric parts (`mods-v1.2.3`) | `mods-v` + two or more numeric parts |
| Drafts | skipped | skipped (`AZMODS_CHANNEL_DRAFTS=1` includes them, for testing) |
| Pre-releases | counted like any other release | counted like any other release |
| Ordering | numeric, part by part | numeric tuple compare |

| Rule for publishers | Why |
|---|---|
| Tag with exactly three parts, `MAJOR.MINOR.PATCH` | the deck ignores any other tag |
| Keep MINOR and PATCH below 1000 | the deck packs a version as `MAJOR * 1000000 + MINOR * 1000 + PATCH` to compare |
| Treat "publish" (leaving draft) as the release moment | GitHub's pre-release flag means nothing to either reader; only a draft is hidden |

The unauthenticated GitHub API allows 60 requests per hour per IP address, shared by every
device on the network. The deck reports HTTP 403 or 429, or a body containing
`rate limit exceeded`, as "GitHub is busy right now, try again in a few minutes".

## Release assets

| Asset | Required | What it is |
|---|---|---|
| `manifest.json` | yes | the set's file list with a SHA-256 for each file |
| `manifest.json.minisig` | yes | its minisign signature |
| `mods-v<version>.tar.gz` | yes | every file the manifest names, at its manifest path |
| `mods-v<version>.tar.gz.minisig` | yes | its minisign signature |
| `CHANGELOG.txt` | no | the set's changelog, the same bytes as the set's `deck/CHANGELOG.txt` |
| `CHANGELOG.txt.minisig` | with `CHANGELOG.txt` | its signature |

Names are exact. A deck that finds the changelog shows it before the release is installed; a
release without it installs normally. The release body (the text on the GitHub page) is shown
by the app as release notes; the deck never reads it.

## manifest.json

```json
{
  "version": "1.2.3",
  "source": {
    "repo": "https://github.com/Kyle-Hosman/xdj-az-mods",
    "commit": "1a2b3c4",
    "build": "azmod-build:18.04, make azmod azstem (the release build)"
  },
  "entries": [
    {
      "id": "core",
      "name": "AZ MODS core",
      "version": "1.0.0",
      "required": true,
      "description": "Mod Menu, Stems, Drum Roll, 60 FPS Waveforms and Pad Colors.",
      "files": [
        {
          "name": "azmod.so",
          "sha256": "<64 lowercase hex>"
        },
        {
          "name": "0.bin",
          "sha256": "<64 lowercase hex>",
          "dir": "azmod-pngpatch"
        },
        {
          "name": "az-modupdate",
          "sha256": "<64 lowercase hex>",
          "dir": "deck/lib"
        }
      ]
    }
  ]
}
```

Top level:

| Field | Type | Meaning |
|---|---|---|
| `version` | string | the set's version, `MAJOR.MINOR.PATCH`; the release tag is `mods-v` + this value |
| `source` | object | provenance, not read by either reader (see [Source provenance](#source-provenance)) |
| `entries` | array | the installable parts of the set |
| `_comment` | string | optional; ignored |

Each entry:

| Field | Type | Meaning |
|---|---|---|
| `id` | string | stable identifier, unique in the manifest |
| `name` | string | shown by the app |
| `version` | string | the entry's own version, shown by the app |
| `required` | boolean | the app always installs a required entry; others can be deselected |
| `description` | string | shown by the app |
| `dir` | string | optional: the default `dir` for every file of the entry |
| `files` | array | the entry's files |

Each file:

| Field | Type | Meaning |
|---|---|---|
| `name` | string | the file name |
| `sha256` | string | SHA-256 of the file, 64 lowercase hex digits |
| `dir` | string | optional: the folder under `MODS/` that holds the file, forward slashes; overrides the entry's `dir` |

### The path of a file

A file lives at `MODS/<dir>/<name>` on the USB, or `MODS/<name>` with no `dir`. The same
path is its member name in the archive. `deck/stage_stick.py --manifest` writes the `deck/`
files in this shape (`"dir": "deck"` or `"deck/<subfolder>"`). The official sets use three
areas:

| Path | Contents |
|---|---|
| `MODS/*.so` | the libraries: the core (`azmod.so`, or `azmod-core.so`) and `azstem.so` |
| `MODS/azmod-pngpatch/` | image patches the core applies |
| `MODS/deck/` | the deck-side scripts as `deck/stage_stick.py` lays them out (`version`, the loader, `lib/`, `boot.d/`), the set's `CHANGELOG.txt` and `CHANGELOG.json`, and `trust.pins` with `trust.pins.minisig` |

### What the deck's parser needs

The deck has no JSON parser: `az-modupdate` reads the manifest with `sed` and `awk`, a line
at a time. A manifest that installs on a deck also satisfies:

| Rule | Because |
|---|---|
| Pretty-printed, one key per line, as `json.dumps(..., indent=2)` writes it | keys are matched per line |
| Every object's opening `{` ends its line, and its closing `}` (or `},`) is on a line of its own | objects are tracked by these lines |
| An entry's `dir`, when used, comes before its `files` key | a file's `dir` falls back to the nearest enclosing object's `dir` seen so far |
| No `"` inside a `name` or `dir` value | values end at the first `"` |
| The top-level `version` comes before `entries` | the set's version is the first `"version": "X.Y.Z"` line in the file |
| The file list names a core: a path `azmod.so` or `azmod-core.so` | otherwise "manifest has no core mod" |

The set's `version` must equal the version in the release tag and be newer than the USB's
`MODS/deck/version`. Both readers take the version from the signed manifest, never from the
tag.

## The archive

`mods-v<version>.tar.gz` is a gzip-compressed tar holding every file the manifest names, at
its manifest path, and nothing else.

| Rule | Who checks |
|---|---|
| Members are regular files (directories may be present) | the app refuses a member that is a link or device, or whose name starts with `/` or `..` or contains `/../` |
| Every manifest file is present and matches its pin | both readers, after the archive signature |
| A member the manifest does not name is never written to a USB | both readers (each copies only the manifest's files) |
| Members sorted, owner and group 0, no macOS `._*` entries (should) | the official release tooling, which also checks before signing that the archive holds exactly the manifest's files and every one matches its pin |

## Signatures

Every `.minisig` is a standard [minisign](https://jedisct1.github.io/minisign/) signature
file in minisign's default (pre-hashed) mode:

```
untrusted comment: <anything>
<base64: "ED" + key id (8 bytes) + Ed25519 signature (64 bytes)>
trusted comment: timestamp:<seconds>	file:<name>	hashed
<base64: Ed25519 global signature (64 bytes)>
```

| Signature | Covers |
|---|---|
| File signature (line 2) | Ed25519 over the BLAKE2b-512 digest of the file (pre-hashed mode), or over the file itself (legacy mode) |
| Global signature (line 4) | Ed25519 over the 64 signature bytes followed by the trusted comment text (line 3 without its `trusted comment: ` prefix) |

Every reader verifies both. They differ in the details:

| | Deck updater (`az-modupdate`) | Deck loader (`trust.pins`) | App |
|---|---|---|---|
| Pre-hashed mode when | line 3 ends in `hashed` | always (legacy mode refused) | the last whitespace-separated word of line 3 is `hashed` |
| Signature blob | 74 bytes | 74 bytes, algorithm `ED` | 74 bytes, algorithm `ED` or `Ed` |
| Key id | not compared | must equal the key's | must equal the key's |
| Trusted comment and global signature | required | required, line 3 must start with `trusted comment: ` | checked when present |
| Implementation | `openssl` 3 on the stock firmware (`dgst -blake2b512`, `pkeyutl -verify -rawin`) | the same | Ed25519 in the app |

A signer keeps minisign's default trusted comment, or any trusted comment whose last word is
`hashed`, and always writes the full four-line file. A custom trusted comment
(`minisign -t`) that does not end in `hashed` makes the app and the deck's updater check the
signature against the file itself instead of its digest, so they refuse it; the loader, which
always assumes pre-hashed mode, accepts it.

`AZ_PUB` in `az-modupdate` and `TRUST_PUB` in the loader hold the base64 line of a minisign
public key file; the app embeds the same value.

## How the deck updates itself

The Updates screen never touches the network. It writes a request; the loader's watcher
runs the updater outside EP147.

### Requests and checks

| Step | Detail |
|---|---|
| Request | the screen writes `check` or `update` to `/run/.az-modupdate.req.tmp` and renames it to `/run/az-modupdate.req` |
| Watcher | `modupdate_watch` in `az-usbload` serves one request at a time. Before each, it picks the USB again as at boot and passes the mount (`AZ_MNT`) and its `MODS/` folder (`AZ_MODS`) |
| Automatic check | about 45 s after the watcher starts and after every EP147 restart (`AZ_MODUPDATE_AUTOCHECK`, seconds), then every 30 minutes (`AZ_MODUPDATE_INTERVAL`, default 1800), while WiFi is up and a mods USB is in. The red dot on the Mod Menu's Updates icon follows the result |

### check

1. Probe `https://github.com` (10 s). No answer: `state=offline`.
2. Find the newest release ([above](#which-release-is-the-newest)).
3. Fetch that release's `CHANGELOG.txt` and its signature (at most 128 KB and 4 KB), verify,
   and keep it as `/tmp/az-modupdate/CHANGELOG.txt`. A missing or refused changelog is
   logged and never stops the check.
4. Compare with `MODS/deck/version` on the USB: newer on the channel is
   `state=available`, otherwise `state=uptodate`. A USB without `MODS/deck/version` is
   always offered the channel's set.

### update

`update` runs `check` first and stops unless a newer set is available. Then, in
`/tmp/az-modupdate/stage`:

| # | Step | On failure |
|---|---|---|
| 1 | download `manifest.json` and its signature; verify | stop |
| 2 | read the set's `version` from the signed manifest: it must equal the tag's version and be newer than the USB's `MODS/deck/version` | stop |
| 3 | read the file list and require a core in it | stop |
| 4 | download `mods-v<version>.tar.gz` and its signature; verify | stop |
| 5 | unpack, then check every manifest file: present, and its SHA-256 equal to the pin | stop |
| 6 | remount the USB read-write if it was read-only; write each file as `<path>.new` and rename it over `<path>` | stop at the first failed file |
| 7 | delete every file under `MODS/deck/` that the manifest does not name | |
| 8 | `sync`, restore a read-only mount, `state=installed`: "Update installed. Reload to apply." | |

Step 2 is the rollback defense. A tag is only a name: anyone who controls the channel
repository, or redirects the pointer, can put a newer-looking tag on an old genuinely signed
set. Reading the version from the signed manifest turns that into a refusal. The app applies
the same rule.

Nothing is written to the USB before both signatures and every pin have passed. A failure
while writing (USB full or pulled) can leave a mix of old and new files; the status says
"could not write the USB (a partial set is on it: re-run, or use the app)". The deck installs
every file in the manifest; it has no notion of optional entries.

### After an install

The updater never restarts EP147 ([the restart policy](USB-STICK.md#the-restart-policy)). The sequence:

1. The watcher sees `installed` (or `reloading`) after an `update` and re-runs the loader from
   the top. The re-run copies the new `MODS/deck/` to RAM, checks it against its pins and hands
   over to the new set's `deck/az-usbload`.
2. The loader copies the new files into its cache, writes the preload for the next EP147
   start, writes `/run/az-reload-pending` (first line `update`) and leaves EP147 running.
3. It runs `az-modupdate staged`: `state=uptodate`, same message.
4. While `/run/az-reload-pending` says `update`, the Updates screen and the Mod Menu's
   Reload row show "Reload to apply."
5. The new set loads at the user's Reload or at the next boot. Every EP147 start removes the
   marker (`az-usbload --post-start`).

`state=reloading` is the final state of an updater that restarts EP147 itself; the watcher
and the screen treat it like `installed`.

### The status file

`/tmp/az-modupdate/status`, rewritten whole on every change (temporary file, then rename):

```
state=available
local=1.2.2
remote=1.2.3
done=0
total=0
msg=v1.2.3 available (this stick: 1.2.2)
```

| Key | Meaning |
|---|---|
| `state` | `idle`, `checking`, `uptodate`, `available`, `downloading`, `verifying`, `installing`, `installed`, `error` or `offline`; readers also accept `reloading` |
| `local` | the set on the USB (`MODS/deck/version`) |
| `remote` | the newest version on the channel, empty when unknown |
| `done`, `total` | files verified so far, and the number in the set |
| `msg` | one line for the user; always the last line |

The updater always exits 0, so the watcher never stops; the answer is the status file. Its log
is `/tmp/az-modupdate/log`.

### The changelog on the deck

The Updates screen shows the first of these that exists and is not empty:

| # | File | What it is |
|---|---|---|
| 1 | `/tmp/az-modupdate/CHANGELOG.txt` | the newest release's changelog, fetched and verified by the last online check |
| 2 | `/tmp/az-deck/CHANGELOG.txt` | the changelog of the set on the USB (the loader copies `MODS/deck/` to `/tmp/az-deck/` at boot) |

It keeps the first 64 KB (`AZMOD_CHANGELOG_MAX`). The format is plain ASCII, newest first,
one entry per line, never hard-wrapped (the screen wraps):

```
# MODS
## 1.2.3  <YYYY-MM-DD>
+ A new feature.
! A fixed bug.
^ An improvement.
~ A change in behavior.
```

| Line | Shown as |
|---|---|
| `# <title>` | a section heading |
| `## <version>  <date>` | a version heading: `v<version>`, then the date (two spaces before it); a `YYYY-MM-DD` date is shown as `MM-DD-YYYY` |
| `+ text` | NEW |
| `! text` | FIXED |
| `^ text` | IMPROVED |
| `~ text` | CHANGED |
| anything else | a plain paragraph |

Inside the `# MODS` section only, the version heading equal to the USB's set is tagged
INSTALLED, and the one equal to the offered update UPDATE AVAILABLE. Other sections (the
desktop app's) carry no tags.

## How the desktop app installs a set

The app ships no mods: it downloads the newest set from the channel when it starts and installs
from the newest set it has verified.

**Download**, for the newest release when no verified copy of that version exists:

1. Fetch `manifest.json` and its signature into a staging folder; verify.
2. Take the set's version from the signed manifest; refuse the set unless it equals the tag's
   version and is newer than the newest set already verified on this computer.
3. Fetch the archive and its signature; verify.
4. Unpack (with the member checks above), then check every manifest file: present, a per-file
   `.minisig` if one is present, and the SHA-256 pin.
5. Write a `.verified` marker (`{"version": ..., "manifest_sha256": ...}`) and rename the
   staging folder to the version number. Any failure removes the staging folder and leaves the
   previous verified set in place.

A set counts as verified only while the marker is present, its folder name equals the version
its signed manifest names, the manifest signature verifies and every file matches its pin.
These are checked again each time the set is used. Sets are ordered by that signed version.
Offline, the app uses the newest set verified on an earlier start.

**Install**, writing a set to a USB:

1. Refuse before touching the USB if any file of the entries being installed does not match
   its pin.
2. Leave the master switch (`mods` in `MODS/azmods.cfg`) as it is.
3. For each file of each selected entry (required entries always): skip it when the USB
   already has the pinned bytes; otherwise write a temporary file, flush it, rename it over the
   target, and check its SHA-256 again.
4. Delete the files of deselected entries, and every file under `MODS/deck/` that the selected
   entries do not name (and macOS `._*` files there).
5. Remove the deck's failure marker `MODS/.pending-powercycle` and any stale
   `MODS/.uninstall-result`.

Outside `MODS/deck/`, files the manifest does not name are never touched: SSH keys, switches, a
user's own files. The app does not talk to the deck, so a set it writes is applied the next
time the deck's loader runs: the next boot with that USB in.

**Which version a USB has.** The app reports the newest known set whose pins the USB
carries in full. A USB whose files match no known set is described by its
`MODS/deck/version`.

## The deck's trust pins

The manifest protects a set on its way to a USB. The trust pins protect the deck from what
is on a USB: the loader runs code from the USB as root, and anyone can write a USB.

### Format

`MODS/deck/trust.pins`:

```
azmods-pins 1
azmod.so <64 lowercase hex>
azstem.so <64 lowercase hex>
deck/az-usbload <64 lowercase hex>
deck/lib/az-usbpick <64 lowercase hex>
deck/boot.d/10-sshgate.sh <64 lowercase hex>
...
```

| Part | Rule |
|---|---|
| Line 1 | exactly `azmods-pins 1` |
| Other lines | `<path under MODS/> <sha256>`, one space. Path segments start with `[A-Za-z0-9_+-]` and contain only `[A-Za-z0-9._+-]`; a line that does not match is not a pin |
| Coverage | every file under `deck/` except `trust.pins` and `trust.pins.minisig`, and every `.so` at the top of the set |
| Signature | `deck/trust.pins.minisig`, by the release key, pre-hashed (see [Signatures](#signatures)) |

`deck/make_pins.py <set folder> [--sign <minisign secret key>]` writes both. It refuses a set
with no `deck/` folder, with no top-level `.so`, or with a name the loader would not accept.
Without `--sign` only `trust.pins` is written, and the set does not run on a deck until it is
signed.

### Order at release time

1. Stage `deck/` (`deck/stage_stick.py`) and put the release libraries beside it.
2. Run `deck/make_pins.py <set> --sign <key>`.
3. Add `deck/trust.pins` and `deck/trust.pins.minisig` to the manifest like any other `deck/`
   file.
4. Sign the manifest.

The manifest pins the signed pins, so the app and the deck's updater install them without
knowing what they are.

### What the loader does with them

`deck/payload/usr/sbin/az-usbload`, the TRUST block:

| Case | Result |
|---|---|
| Genuine signed pins | every file of the RAM copy of `MODS/deck` whose bytes are not pinned is deleted before anything in that copy runs, is sourced or is cached. That covers the USB's loader, the helpers it sources or starts, the boot hooks, and the scripts the mods start from the cache |
| A `.so` in `MODS/` | staged only when its bytes are pinned; the copy on the settings partition is checked against the pin again (the USB can change between the check and the copy). Without a pinned core nothing loads |
| No genuine pins | no deck script runs, no `.so` is staged, the cached `deck/` is removed, and the deck boots stock. EP147 itself is untouched, so a refused USB cannot stop the deck from booting |
| No USB | the cached `deck/` is checked against its own pins before it runs; `az-usbload --post-start` hands over to the cached loader only when it verifies |

| Not covered by the pins | Instead |
|---|---|
| `MODS/azmod-pngpatch/` | data: the core writes a blob only at the start of one of the running build's own PNG images (the table in `src/azmod/ep147png.h`), and only when the blob is a PNG no longer than that image; on a build without a table every blob is refused |
| Community mods | third-party code the user chose to install, under their own rules ([COMMUNITY-MODS-INTERNALS.md](COMMUNITY-MODS-INTERNALS.md)) |

### Developer override

`/home/root/settings/azmods-unsigned.on` on the deck's settings partition turns the check off:
deck scripts and every `.so` run unverified, and the loader logs
`trust: DEVELOPER OVERRIDE (...)` on every boot. Creating it takes root on that deck; the same
name on a USB does nothing. `AZ_TRUST_PUB` replaces the key for the test suites only; nothing
on a deck sets it.

### The firmware's loader and the USB's loader

The firmware carries a bootstrap copy of the loader, which hands the boot over to the USB's
`deck/az-usbload` after `sh -n`. A firmware loader that carries the TRUST block removes an
unpinned USB loader before the handover. An older firmware loader hands over after `sh -n`
alone; the USB's loader then checks its own folder again and applies the rule to everything
else. That protects a deck from a set altered on its way, but not from a USB whose loader was
replaced; only a firmware update closes that gap.

## Version rules

| Rule | Why |
|---|---|
| A new set is newer than the newest published set | apps install the highest verified version, so a lower or equal one would never be installed. The official release tooling refuses to publish one |
| The set's `MODS/deck/version` equals the manifest's `version` | the deck compares the channel with that file: a lower value offers the same update forever, a higher one hides real updates |
| `AZ_CORE_VERSION` in `src/azmod/azmod-mod.h` is the mods release version | community mods state the core they need as `min_core`, read as "needs AZ-MODS `<min_core>` or newer" ([COMMUNITY-MODS-INTERNALS.md](COMMUNITY-MODS-INTERNALS.md)) |
| Releases are immutable | both readers pin by hash, so replacing an asset in place breaks installs of that version; publish a new version instead |

## Source provenance

The manifest's `source` block records where `azmod.so` and `azstem.so` came from:

| Field | Value |
|---|---|
| `source.repo` | this source repository |
| `source.commit` | the commit of it the two libraries were built from, 7 to 40 lowercase hex digits |
| `source.build` | the build command |

Neither reader uses these fields. The official release tooling refuses a manifest whose
`source` has other fields, names another repository or has no valid commit, and refuses a set in
which any `.so` changed since the previous published set while `source.commit` did not.

To check an official release: check out the commit its manifest names in `source.commit`
(in this repository), build in the release container, and run `./verify.sh path/to/manifest.json`
([BUILDING.md](../BUILDING.md), "Check a build against an official release"). A match proves
the official `azmod.so` and `azstem.so` are exactly this source. The check covers those two
files only; BUILDING.md lists what else a release carries.

## Running your own channel (forks)

A fork that ships its own builds needs its own key and its own channel; the mechanism above
works unchanged with different values.

**1. Make a key.**

```sh
minisign -G -p my-mods.pub -s my-mods.key
```

The second line of `my-mods.pub` is the public key line every reader embeds.

**2. Make a channel repository** with releases tagged `mods-v<version>` carrying the assets
above, signed with your key. A release reaches decks when it is published (taken out of draft).

**3. Point the deck scripts at your channel and key**, in `deck/stick/deck/lib/az-modupdate`:

| Variable | Set it to | Environment override |
|---|---|---|
| `POINTER` | the raw URL of your fork's `channel.json` | `AZ_MODS_POINTER` |
| `REPO` | your channel repository (`owner/name`), used when the pointer cannot be read | `AZ_MODS_REPO` (also skips the pointer) |
| `AZ_PUB` | your public key line | `AZ_MODS_PUB` |

Change `POINTER` as well as `REPO`: the default pointer is the official `channel.json`, which
sends a deck to the official channel, where every set fails your key. Put a `channel.json`
naming your channel at the root of your fork. For Community Mods, `LIST_URL` in
`deck/stick/deck/lib/az-community` names the default repository list
([COMMUNITY-MODS-INTERNALS.md](COMMUNITY-MODS-INTERNALS.md)).

**4. Sign your trust pins with your key**, and put your public key line in `TRUST_PUB` in
`deck/payload/usr/sbin/az-usbload`. A deck runs a fork's scripts and libraries only when its
loader trusts the fork's key, or under the [developer override](#developer-override).

**5. Ship the deck scripts in your set** (`deck/stage_stick.py` lays them out as
`MODS/deck/`), so decks running your set keep updating from your channel.

**The desktop app** trusts only the official key and follows only the official pointer. It
never installs a fork's set, and installing official mods with it replaces the fork's files,
including `MODS/deck/`. A fork's first set reaches a USB by copying its files into `MODS/` in
the manifest's layout; from then on the fork's own `az-modupdate` keeps it current.

**Naming.** A fork's key, key files and key comment carry the fork's own name, so a signature
is never mistaken for an official one. Its releases are not presented as official AZ-MODS
builds (release titles, changelog, Mod Menu); the tag and asset names above are the protocol
and stay the same. A fork's channel and pointer do not point at the official repositories, and
users are not asked to replace the official key in the official app.

## Where the readers disagree

Both readers take a set's version from its signed manifest and refuse the set unless that
version equals the release tag's.

| Point | Deck | App |
|---|---|---|
| Tags with two or four parts | ignored | accepted |
| A set's version must be newer than | the USB's `MODS/deck/version` | the newest set already verified on the computer |
| `dir` on an entry | applied only when it comes before the entry's `files` | applied |
| Archive member paths | relies on the archive signature and copies only manifest paths | refuses absolute paths, `..` and links |
| Key id in a signature | not compared by the updater; compared by the loader for `trust.pins` | must match |
| Signature file without a trusted comment | refused | accepted |
| Optional entries | installs every file | installs required plus selected entries |
| Release body | not read | shown as release notes |

A release that follows every "must" above (three-part versions, a pretty-printed manifest,
four-line pre-hashed signatures with a trusted comment ending in `hashed`, an archive of plain
files) is read the same way by both.
