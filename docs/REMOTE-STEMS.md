# Remote Stems

MENU > MAKE STEMS on a track, a playlist, a folder or the Tag List sends each track over the
network to a computer running the AZ-MODS desktop app, which separates it and serves the stems
back. The deck writes them into the USB's `STEMS/` folder, where `azstem.so` finds them the
next time the track loads.

| Side | Source | Role |
|---|---|---|
| Deck | `src/azmod/remote_stems.c` | MENU > MAKE STEMS and CANCEL STEMS: reads the rows on the owning USB, writes the queue, starts the helper, shows its status, reads its progress files |
| Deck | `src/azmod/playlist_actions.c` | the MENU banner messages |
| Deck | `src/azmod/stem_marks.c` | the per-row queued and progress animation |
| Deck | `deck/azrs/azrs.sh` (staged as `MODS/deck/lib/azrs.sh`) | the helper: discovery, authentication, upload, polling, download, writing `STEMS/` |
| Deck | `deck/test/remotestems.sh` | the helper's test suite, against a fake server |
| Computer | the AZ-MODS desktop app | the server. Its source is not in this tree; this page specifies what it does on the wire |

"Must" marks what the other side relies on. Where the deck's parser is stricter than JSON,
the rule is stated; a third-party server has to respect it.

## Overview

```
 deck: azmod                 deck: azrs.sh                 computer: AZ-MODS
 -----------                 -------------                 -----------------
 MENU > MAKE STEMS
   writes MODS/azstems-queue/<n>.req
   starts the azrs unit  --> reads the queue
                             mDNS: _azstems._tcp, TXT id=
                             GET  /v2/hello?kid=&nd= ---->  proof of the token
                             (every request below signed with the session key)
                             POST /v1/jobs          ----->  job created, folder chosen
                             PUT  /v1/jobs/<id>/audio --->  queued
                             GET  /v1/jobs/<id>  (poll) ->  queued / running / done
                             GET  /v1/jobs/<id>/files/<name>
                             writes STEMS/.<folder>.part, renames it into place
                             DELETE /v1/jobs/<id>   ----->  job removed
   shows /tmp/azrs/status
   animates rows from /tmp/azrs/jobs
```

The deck is the client for everything. The computer never connects to the deck, never writes
to it, and holds no credentials for it.

## The queue on the USB

The queue lives on the USB, so it travels with the USB and survives a reboot or an EP147
restart.

### Requests

MAKE STEMS writes one file per track to `<stick>/MODS/azstems-queue/`:

```
<ms><seq>-<content_id>.req
```

`<ms>` is a millisecond clock, `<seq>` three digits, `<content_id>` the track's id in that
USB's library. The file is written as `<name>.tmp` and renamed. Its lines are `key=value`:

| Key | Written by | Meaning |
|---|---|---|
| `file` | azmod, always the first line | the track's path in the library, relative to the USB, with a leading `/` |
| `title` | azmod, optional | the track's title, for messages (a file name can be an opaque id) and for matching the row (see [Rows](#rows)); control characters written as spaces |
| `job` | the helper | the job id on the computer, once created |
| `srv` | the helper | the instance id of the computer the job was sent to |
| `up` | the helper | `1` once the audio is uploaded |

| Rule | Detail |
|---|---|
| Which USB | the row is read on the library thread of the USB whose own database carries it, never guessed across USBs |
| Duplicates | a track whose `file=` line is already queued is not queued again ("Already queued for stems") |
| Playlist, folder, Tag List | a background worker queues every track in it and under it (at most 2000) that is not already queued and does not already have stems by `azstem.so`'s own rule, on any USB |
| No pairing | refused when the USB has no `MODS/azstems.pair` ("Plug this USB into AZ-MODS once, with Remote Stems on") |

### Cancel

MENU > CANCEL STEMS on a queued row renames each of that track's requests (or each request of a
playlist's tracks, matched by the content id in the name) from `.req` to `.cancel`. The row's
mark stops at once. The helper deletes the job on the computer it was sent to, if it was sent
and that computer is on the network, and removes the file.

### The helper

| Property | Value |
|---|---|
| Unit | transient systemd unit `azrs`: `systemd-run --unit=azrs --collect -p Nice=19 -p CPUAffinity=0-3 /bin/sh /home/root/settings/az-usbload/deck/lib/azrs.sh` (the loader's staged copy, present with or without the USB) |
| Started | through `posix_spawn` with an argument list and an empty `LD_PRELOAD` (EP147 is not forked, no shell runs). It runs outside EP147, so an EP147 restart never interrupts a transfer |
| When | when azmod queues or cancels work, and every 10 s if any USB under `/media/usb` has a `.req` and the helper is not running (`/tmp/azrs/pid`) |
| Loop | a pass every 3 s; exits after 120 s with nothing to do (`AZRS_IDLE`) |
| At start | deletes leftover `.<folder>.part` and `.<folder>.old` folders from the `STEMS` folder of every USB that has a pair file |
| Log | `/tmp/azrs/log`; past 256 KB (`AZRS_LOG_MAX`, checked every 32 lines) it moves to `/tmp/azrs/log.1` |

## Pairing

Pairing ties a stick to one or more computers. While Remote Stems is on, the desktop app pairs
each USB plugged into the computer, once per plug-in, and writes `<stick>/MODS/azstems.pair`:

```json
{"id": "3fa2c41b9e07", "token": "6c0d1f...32 hex...", "keys": [{"id": "3fa2c41b9e07", "token": "6c0d1f...", "at": 1767225600}, {"id": "91be0a55c3d2", "token": "e47a20...", "at": 1767139200}]}
```

| Field | Meaning |
|---|---|
| `keys` | one entry per computer, **newest first**: the computer the USB was paired with (or plugged into) last comes first. At most 8 |
| `keys[].id` | the computer's instance id: 12 lowercase hex digits, chosen once per installation |
| `keys[].token` | the secret that computer issued for this USB: 32 lowercase hex digits |
| `keys[].at` | when it was paired, in seconds since the Unix epoch |
| `id`, `token` | a copy of the first key, for helpers that read only one |

- Pairing again with the same computer keeps its token and moves its key to the front.
  Unpairing removes only that computer's key; with no keys left, the file is deleted.
- The computer keeps every token it issued, with the USB it was issued for, and accepts only
  those.
- The helper reads the keys with a pattern, not a JSON parser: each key object has `"id"`
  immediately followed by `"token"`, as `{"id": "...", "token": "...", ...}` (any spaces after
  `:` and `,`). A file with only the top-level `id` and `token` is read as a list of one.

## Authentication (wire version 2)

The token never crosses the network. The deck and the computer prove they share it.

| Value | Definition |
|---|---|
| `kid` | the first 16 hex digits of SHA-256(token): names the key, reveals nothing |
| `nd` | the deck's nonce: 32 lowercase hex digits (`openssl rand -hex 16`), fresh per hello |
| `ns` | the server's nonce: 32 lowercase hex digits, fresh per hello; also the session's name |
| `proof` | HMAC(token, `"azrs-srv\n" + id + "\n" + nd + "\n" + ns`) |
| session key `K` | HMAC(token, `"azrs-ses\n" + nd + "\n" + ns`), used as 32 raw bytes |
| request signature | HMAC(K, `method + "\n" + path + "\n" + ctr + "\n" + content_length`) |

HMAC is HMAC-SHA-256 throughout, written as lowercase hex. The token is used as its ASCII
text. In the request signature, `path` is the request target as sent (query included),
`ctr` the decimal counter, and `content_length` the `Content-Length` header value, empty when
the request has none.

### The handshake

1. The deck sends `GET /v2/hello?kid=<kid>&nd=<nd>`, with no other authentication.
2. The server finds the token whose `kid` matches and answers **200**
   `{"id", "v": 2, "name", "ns", "proof"}`.
3. The deck requires `id` equal to the instance id it is looking for, `ns` 32 hex digits and
   `proof` 64 hex digits equal to the HMAC it computes with its own token. Anything else is
   refused (logged as "REFUSED ...: it claims to be <id> but cannot prove it holds this USB's
   key").
4. Both sides derive `K`. The deck sets its counter for that server to 0.

| Hello answer | Server sends it when | The deck |
|---|---|---|
| 200 | the `kid` names a token it issued | checks the proof |
| 400 | `kid` or `nd` is missing or malformed | treats the server as not found |
| 401 with `"v": 2` | no issued token matches | records `/tmp/azrs/rejected.<id>`: the computer is there but no longer knows this USB |
| 401 or 404 without `"v": 2` | (an app that predates version 2) | records `/tmp/azrs/oldapp.<id>`, and never sends that app the token |
| 429 | 20 failed hellos or signatures from that address within 60 s | treats the server as not found |

### Signed requests

Every request after the hello carries:

```
X-AZ-Auth: ns=<ns> ctr=<n> sig=<64 hex>
```

| Server check | Failure |
|---|---|
| the header parses (`ns` 32 hex, `ctr` 1 to 15 digits, `sig` 64 hex) and names a live session | 401 "no session: say hello first" |
| `sig` equals the signature it computes | 401 "bad signature" |
| `ctr` is greater than the last one accepted in that session | 401 "replayed request" |
| the session's token is still issued | 401 "this USB is not paired with this computer ..." |
| an `Authorization: Bearer` header (a version 1 deck) | 401 "this deck's mods are too old for this AZ-MODS: update the mods on the deck" |

The deck opens a new session each time it looks for a computer (every pass), and increments
the counter before every request. The server keeps a session for 2 hours and at most 64
sessions, dropping the oldest first.

## Security model

| Property | Holds | Mechanism |
|---|---|---|
| The token stays on the USB | yes | only `kid`, nonces, the proof and HMACs cross the network |
| The deck talks only to a computer that holds this USB's token | yes, at the hello | the proof binds the deck's fresh nonce; a device that advertises the instance id (public in the mDNS record) cannot produce it |
| Only a paired USB can queue work or read jobs | yes | every request is signed with a session key derived from the token; a job belongs to the token that created it |
| A recorded request cannot be replayed or moved to another URL | yes | the counter only increases, and the signature binds method, path and length |
| Confidentiality | no | plain HTTP: the uploaded audio and the returned stems cross the network unencrypted |
| Integrity of bodies and responses | no | the signature covers the body's length, not its bytes, and responses after the hello are not signed. A device that can intercept and modify traffic between the deck and the computer can alter an upload or the files sent back |
| The token at rest | plain text | in `MODS/azstems.pair`, readable by anyone holding the USB |
| Connections from public addresses | refused | the server closes a connection from a non-local (globally routable) address before reading it |

What the deck enforces on whatever it downloads: the folder name must not be empty, start with
`.` or contain `/`; file names likewise; every file must match the size the server announced;
and a set is published only as one rename (see [How the deck runs one request](#how-the-deck-runs-one-request)).
Within those limits, a party able to modify traffic in transit can still replace the stems
folder of any track on that USB, or fill the USB.

## Discovery

### The record (computer side)

The app advertises one DNS-SD service while Remote Stems is on, and withdraws it (a clean mDNS
goodbye) when it is turned off or the app quits:

| Item | Value |
|---|---|
| Service type | `_azstems._tcp.local.` |
| Instance name | `AZ-MODS on <hostname> <first 4 digits of the id>`: the hostname cut to 40 characters, anything but letters, digits and `-` replaced by `-`; the id keeps two instances on one computer apart |
| Target host | `azmods-<id>.local.` |
| Port | 8766 by default (`AZMODS_REMOTE_PORT`); any free port if that one is taken. A client takes the port from the record |
| Addresses | the IPv4 address of the computer's default route, and the addresses its hostname resolves to except loopback and link-local |
| TXT `v` | `2`, the wire version |
| TXT `id` | the instance id |
| TXT `ips` | the advertised IPv4 addresses, comma-separated, at most 8 |

`ips` exists because a resolver may hand out only one address per interface, and on a computer
with virtual network adapters that one can be unreachable from the deck. The server listens on
all interfaces (`0.0.0.0`).

### Finding a computer (deck side)

For each key in the pair file, newest first, the helper looks for that computer (`server_for`):

1. With a cached `host:port` for that id (`/tmp/azrs/server.<id>`), it says hello there. A
   proven answer with the same `id` makes it the server; otherwise the cache is dropped.
2. Otherwise it browses `_azstems._tcp` with `avahi-browse -rtp`, keeps resolved IPv4 records
   whose TXT has `id=<id>`, and for each record tries the resolved address first, then each
   address in `ips`, skipping any already tried. The first address whose hello is proven is
   used and cached.

A record whose hello fails or carries another id is never used: nothing is uploaded to a stale
or foreign record.

| Situation | Where the request goes |
|---|---|
| A new request | the newest key whose computer is on the network |
| Already sent (`job=` and `srv=` set), that computer reachable | stays with it |
| Already sent, that computer not reachable | `job`, `up` and `srv` are cleared, and it is sent again to the newest reachable one |

When no computer is found, the helper reports it and waits 27 s more before the next pass:

| Condition | Message |
|---|---|
| a computer answered the hello as not knowing this USB (`rejected.<id>`) | Not paired with <computer>: plug this USB into AZ-MODS once, with Remote Stems on |
| an app older than wire version 2 answered (`oldapp.<id>`) | Update AZ-MODS on <computer> to use Remote Stems with this deck (without "on <computer>" when its name is unknown) |
| one key, nothing answered | <computer> not found on the network |
| several keys, nothing answered | None of this USB's computers are on the network |

## HTTP protocol

### Conventions

| Rule | Detail |
|---|---|
| Transport | plain HTTP/1.1 |
| Authentication | `GET /v2/hello` unauthenticated; every other endpoint requires `X-AZ-Auth` ([Signed requests](#signed-requests)) |
| Job ids | 16 lowercase hex digits. A job belongs to the token that created it; a job id used under another token gets 404 |
| Responses | every response except a file download is a JSON object with `Content-Type: application/json; charset=utf-8`. An error is `{"error": "<message>", "v": 2}` with its status code (a 500 carries no `v`) |
| Strings | **literal UTF-8, never `\uXXXX` escapes**: the deck reads responses with `sed` and does not decode escapes, so a folder named `Café` arrives as its own bytes. The deck cannot read a string value that contains `"` |
| Keys | the deck reads each field by key from the whole response, so a key that appears more than once is ambiguous. Do not repeat `id`, `state`, `pct`, `position`, `folder`, `error`, `skip`, `ns`, `proof` or `v` in nested objects, or `name` in the hello answer |
| `files` entries | `"name"` immediately followed by `"size"`: `{"name": "drums.ogg", "size": 4718592}` (any spaces after `:` and `,`) |
| Request bodies | the deck escapes only `"` and `\` in strings, so a control character can arrive raw inside a string. The server accepts it |

### The job object

`GET /v1/jobs/<id>`, a successful `PUT`, and `GET /v1/jobs` return jobs in this form:

```json
{"id": "a1b2c3d4e5f60718", "state": "running", "pct": 37, "position": null, "folder": "Artist - Song", "file": "Contents/Artist/Artist - Song.mp3", "files": null, "error": null}
```

| Field | Type | Meaning |
|---|---|---|
| `id` | string | the job id |
| `state` | string | `waiting_upload`, `queued`, `running`, `done` or `error` |
| `pct` | integer | 0 to 100. While running it moves between 1 and 99 and never goes backward; 100 when done |
| `position` | integer or null | while queued, how many deck jobs are ahead of it (0 = next); null otherwise, including while a queued job waits for a separation the app started itself |
| `folder` | string | the `STEMS/` folder name the stems belong in. Provisional until `done` |
| `file` | string | the USB-relative path given at creation |
| `files` | array or null | only when `done`: `[{"name", "size"}]`, the files to download |
| `error` | string or null | when `state` is `error`, why |

### Job lifecycle

```
POST /v1/jobs          -> waiting_upload
PUT  .../audio         -> queued        (or error: no usable folder name)
the engine takes it    -> running
the engine finishes    -> done          (all four stems written)
                       -> error         (failed, or canceled on the computer)
DELETE                 -> gone
```

| Behavior | Detail |
|---|---|
| Scheduling | one job at a time, first come first served, through the same separation engine the app uses for its own batches. A deck job waits while the app is separating something else |
| Quality | the app's current Stem Quality setting |
| Expiry | a `done` or `error` job the deck never deletes is removed after 24 hours |
| App restart | a job still waiting for its upload is dropped (the deck's next request for it gets 404 and it starts over); a queued or running job is queued again from the start; `done` and `error` jobs are kept |
| Cancel in the app | sets the job to `error` with `"error": "cancelled on the computer"` |

### `GET /v2/hello`

The handshake ([Authentication](#authentication-wire-version-2)). Query `kid` and `nd`.
Response **200**:

```json
{"id": "3fa2c41b9e07", "v": 2, "name": "Studio Mac", "ns": "<32 hex>", "proof": "<64 hex>"}
```

| Field | Meaning |
|---|---|
| `id` | the instance id, as in the pair file and the TXT record |
| `v` | the wire version, 2 |
| `name` | optional: the computer's display name, at most 40 characters. The deck uses it in messages and falls back to `AZ-MODS` |
| `ns` | the server's nonce for this session |
| `proof` | the proof of the token |

### `GET /v1/health`

Signed. **200** `{"id", "v": 2, "name"}`. The current deck helper identifies a server by its
hello and does not call this endpoint.

### `POST /v1/jobs`

Creates a job for one track. `Content-Length` is required; the body is a JSON object of at
most 1 MB:

```json
{"file": "Contents/Artist/Artist - Song.mp3", "size": 9123456, "mtime": 1767225600,
 "existing": [{"name": "Artist - Song", "source": {"file": "Music/Artist - Song.mp3", "size": 8011223}, "files": ["vocals.ogg", "other.ogg", "bass.ogg", "drums.ogg", "source.json"]}]}
```

| Field | Required | Meaning |
|---|---|---|
| `file` | yes | the track's path relative to the USB root, forward slashes, no leading `/`, no empty, `.` or `..` component, no `\` or NUL |
| `size` | yes | the file's size in bytes, a positive integer, at most 512 MB |
| `mtime` | yes | the file's modification time in seconds, an integer |
| `existing` | no | the deck's listing of the folders in that USB's `STEMS/` whose names could belong to this track's key (`<key>`, `<key>_` or `<key>-XXXXXXXX`, ASCII case ignored; see [STEMS-FORMAT.md](STEMS-FORMAT.md)): each with `name`, `source` (the folder's `source.json`, inserted verbatim, or null) and `files` (the file names in it) |

The deck inserts each `source.json` as it is, with line breaks removed; a folder whose
`source.json` is not valid JSON makes the whole body invalid.

The file's extension must be one of `.mp3 .m4a .aac .mp4 .wav .aif .aiff .flac .ogg .opus`,
ignoring case.

The server chooses the folder with the rules the app uses when it separates a track itself
([STEMS-FORMAT.md](STEMS-FORMAT.md), "Folder names" and "source.json"):

1. The best-ranked existing folder whose `source.json` matches this file. If its
   `source.json` names this path (ASCII case ignored) and size, and its `files` include
   `vocals`, `other`, `bass` and `drums` (any extension), there is nothing to do; otherwise
   that folder is used.
2. Otherwise `<key>` (or `<key>_` when the key needs it), unless a folder of that name has a
   `source.json` that identifies another file (one without an identity is reused); then
   `<key>-` followed by the CRC-32 of `file`, as 8 lowercase hex digits. If that name is
   taken too, the request is refused.

Before the upload the server cannot compute the fingerprint, so a folder that only a
fingerprint could claim counts as another song's. The choice is made again after the upload,
with the fingerprint, and only the folder in the `done` response is final.

| Status | Body | Meaning |
|---|---|---|
| **201** | `{"id": "<job>", "folder": "<provisional>"}` | a new job, `waiting_upload` |
| **200** | `{"id": "<job>", "folder": "..."}` | this token already has a job for the same `file` and `size` that is not in `error`: that job is returned |
| **200** | `{"id": null, "folder": "...", "skip": true}` | the USB already has complete stems for this file (rule 1); no job. Checked before the existing-job match |
| 400 | error | a missing or invalid field, a body that is not a JSON object, or no usable folder name (the key cannot be stored, or every candidate name belongs to another song) |
| 413 | error | `size` over 512 MB |
| 415 | error | not an audio extension the app separates |

### `PUT /v1/jobs/<id>/audio`

Uploads the track's bytes, exactly as they are on the USB. The body is either exactly
`size` bytes with `Content-Length`, or chunked transfer encoding totalling exactly `size`
bytes. `Expect: 100-continue` is honored: an unknown job gets 404 before the body is sent.

| Status | Body | Meaning |
|---|---|---|
| **200** | the job object | received in full; normally `queued`, or `error` if no usable folder name remains now that the fingerprint is known |
| 400 | error | a body that is not exactly `size` bytes |
| 404 | error | no such job (for example, the app restarted before the upload) |
| 409 | error | the job is not waiting for its audio (or another upload of it is in progress) |
| 413 | error | `Content-Length` over 512 MB |

After an error from the upload itself, or a 401 or 429, the server closes the connection: the
unread body makes it unusable.

### `GET /v1/jobs/<id>`

The job object (**200**), or **404** when there is no such job.

### `GET /v1/jobs/<id>/files/<name>`

One file of a finished job as `application/octet-stream` with `Content-Length`. `<name>` must
be one of the job's `files`.

| Status | Meaning |
|---|---|
| **200** | the file |
| 404 | no such job, or not one of its files |
| 409 | the job is not `done` |

A finished job lists, in this order and when present: `drums.ogg`, `bass.ogg`, `other.ogg`,
`vocals.ogg`, `versions.txt`, `source.json`. A job reaches `done` only when all four `.ogg`
files exist. The files follow [STEMS-FORMAT.md](STEMS-FORMAT.md); `source.json` describes the
deck's file (its USB path, size and modification time, and the fingerprint of the uploaded
bytes, which are the deck's bytes), never the computer's working copy.

### `DELETE /v1/jobs/<id>`

Removes the job and its files on the computer, stopping the separation if it is running.
**200** `{"ok": true}`, or **404**.

### `GET /v1/jobs`

This token's jobs: **200** `{"jobs": [<job object>, ...]}`, oldest first. The deck does not use
it.

### Other errors

| Status | Meaning |
|---|---|
| 401 | no valid signature ([Signed requests](#signed-requests)) |
| 404 | no such endpoint (checked after the signature, so an unsigned request gets 401) |
| 405 | method not allowed on that path |
| 429 | too many failed hellos or signatures from this address; wait a minute |
| 500 | an unexpected server error; the body names it |

## How the deck runs one request

Each pass, for each queued request, the helper advances it as far as it can (`step` in
`azrs.sh`):

1. **The file.** If `<stick><file>` is missing: "Stems: the file is missing - <title>", and
   the request is dropped.
2. **Create** (no `job=` yet). It lists the matching `STEMS/` folders and their
   `source.json`, then `POST /v1/jobs`.

   | Answer | Result |
   |---|---|
   | `skip: true` | "Stems already on this USB: <title>"; the request is dropped |
   | 200 or 201 with an `id` | `job=` and `srv=` are written to the request |
   | 200 or 201 without an `id` | "Stems failed on <computer>: no job id"; dropped |
   | 401 | "Not paired with <computer>: ..."; the request is kept, for after the USB is paired again |
   | 409 | retried on the next pass |
   | any other 4xx | a definite rejection of this track: "Stems failed on <computer>: <error>" (or `HTTP <code>` when the body has no `error`); dropped |
   | no answer (curl's code `000`), a 5xx, anything else | retried on the next pass |

   If the request was canceled meanwhile, the new job is deleted.
3. **Upload** (no `up=1` yet). Unless the job was created by this pass's POST (201), the helper
   first reads it with `GET /v1/jobs/<id>`: a job past `waiting_upload` already has its audio,
   so `up=1` is written without sending the file again (an earlier PUT reached the computer but
   its response was lost, or the POST returned an existing job, 200). A 404 clears `job=`;
   an unknown answer retries on the next pass. Then `PUT /v1/jobs/<id>/audio` with the file:

   | Answer | Result |
   |---|---|
   | 200 | `up=1` (if the request was canceled meanwhile, the job is deleted instead) |
   | 404 | `job=` is cleared; the next pass creates a new job |
   | 409 | the helper reads the job's state: past `waiting_upload`, `up=1`; still `waiting_upload` (an earlier upload is still arriving), retried on the next pass |
   | anything else, including a timeout | retried on the next pass |
4. **Poll.** `GET /v1/jobs/<id>`.

   | Answer | Result |
   |---|---|
   | 404 | `job=` is cleared and `up=0` written; the next pass starts over |
   | `queued`, `running` | progress is reported; the request stays |
   | `error` | reported (canceled or failed), the job is deleted, the request is dropped |
   | `done` | continue |
5. **Download.** The `folder` from the `done` response is used; one that is empty, starts with
   `.` or contains `/` drops the request. Into `STEMS/.<folder>.part/` (the `STEMS` folder found
   ignoring case, or created), each listed file is downloaded to `<name>.tmp`, its size
   compared with the listed size, and renamed to `<name>`. A file name that is empty, starts
   with `.` or contains `/`, a failed download or a size mismatch deletes the `.part` folder,
   and the next pass downloads again. A `done` response without `files` is retried on the next
   pass. A `.part` folder that cannot be created: "Stems failed: this USB cannot be written",
   and the request is dropped.
6. **Publish.** `sync`; if `STEMS/<folder>` exists it is renamed to `STEMS/.<folder>.old`;
   `.<folder>.part` is renamed to `<folder>`; the `.old` folder is deleted; `sync`. This is
   the staging procedure of [STEMS-FORMAT.md](STEMS-FORMAT.md), "Writing a folder safely": an
   existing set is swapped, never merged, so the old `azstem.env` goes with it.
7. **Finish.** `DELETE /v1/jobs/<id>`, "Stems ready: <title>", and the request is removed.

A track that is already loaded keeps playing without the new stems; they load the next time
the track is loaded.

### Timeouts

All times are `curl` limits in `azrs.sh`.

| Call | Limit |
|---|---|
| `GET /v2/hello` while discovering | 2 s to connect, 4 s in total |
| `GET /v2/hello` on a cached address | 4 s in total |
| `POST /v1/jobs` | 20 s |
| `PUT .../audio` | 900 s in total; aborted if slower than 2000 bytes/s for 30 s |
| `GET /v1/jobs/<id>` | 10 s |
| `GET .../files/<name>` | 600 s per file |
| `DELETE /v1/jobs/<id>` | 10 s |
| Pass interval | 3 s; 30 s while no paired computer is found |
| Helper idle exit | 120 s with nothing queued |

## What the deck shows

### MENU

MAKE STEMS appears on track rows, playlist and folder rows, and the Tag List. On a track row in
the queue, or a playlist row holding a queued track, it reads CANCEL STEMS; folders and the Tag
List keep MAKE STEMS. The feature follows the Mod Menu row Stems > Remote
Stems, read live.

Messages in EP147's banner strip when queueing or canceling:

| Message | When |
|---|---|
| Plug this USB into AZ-MODS once, with Remote Stems on | the USB has no `MODS/azstems.pair` |
| Already queued for stems | the track is already queued |
| Every track in <playlist> already has stems | nothing to queue |
| Everything in <playlist> is already queued | nothing new to queue |
| That playlist is empty | no tracks |
| Could not queue: this USB cannot be written | the queue could not be written |
| This track could not be queued for stems | any other MAKE STEMS failure |
| Busy · try again | another edit or read is in flight |
| Stems canceled / Stems canceled for N tracks | CANCEL STEMS succeeded |
| Nothing in the queue to cancel | no request matched |
| Could not cancel: this USB cannot be written / Could not cancel | CANCEL STEMS failed |

A successful MAKE STEMS shows no message; the rows animate instead.

### Rows

azmod animates each queued track's row from three files the helper writes in `/tmp/azrs/`
(`jobs` and `job` replaced atomically, `done` appended to). `sm_pend_scan` in `remote_stems.c` reads them; `stem_marks.c` draws
the rows.

| File | Content |
|---|---|
| `jobs` | one line per track in flight: `<content_id> <pct> <phase>` |
| `job` | the track stepped last, as `cid=<id> pct=<n> ph=<phase>`; azmod reads it only when `jobs` is absent |
| `done` | the content ids finished in this run of the helper, one per line |

| Phase | Shown as |
|---|---|
| (a `.req`, not in `jobs`), `wt`, `up` | every tile dim, still: queued, sent and waiting its turn, or sending the track |
| `st` | being separated: 1, 2, 3, 4 tiles lit in a cycle (`pct` is not drawn) |
| `dn` | the stems coming back: one tile lit at a time, top to bottom |

A row is matched by content id and title, so a request without a `title=` line is not animated.
At most 32 queued tracks are tracked.

A track listed in `done` goes straight to the full stems mark when its request leaves the
queue.

### Status line

The helper writes `/tmp/azrs/status` only when it changes: line 1 the message, line 2
`banner=1` when the message reports a problem the rows cannot show, `banner=0` otherwise. With
several tracks in flight it reports one per pass, preferring running over sending over waiting.

| Status | In the banner strip |
|---|---|
| Sending to <computer>: <title> | no (the row shows it) |
| Waiting on <computer>: <title> / Waiting on <computer> (N ahead): <title> | no |
| Making stems on <computer>: N% - <title> | no |
| Stems ready: <title> | no |
| Stems canceled on <computer>: <title> / Stems canceled: <title> | no |
| Stems already on this USB: <title> | yes |
| Stems failed on <computer>: <error> | yes |
| Stems failed: this USB cannot be written | yes |
| Stems: the file is missing - <title> | yes |
| <computer> not found on the network | yes |
| None of this USB's computers are on the network | yes |
| Update AZ-MODS on <computer> to use Remote Stems with this deck | yes |
| Not paired with <computer>: plug this USB into AZ-MODS once, with Remote Stems on | yes |
| Plug this USB into AZ-MODS once, with Remote Stems on | yes |

azmod checks the file once a second and puts a status line in the banner strip when line 2 is
`banner=1`, never the line already present when azmod started. Without line 2 (an older
helper), azmod shows a line that contains "failed", "not found", "Turn on", "missing",
"cannot" or "already".

## Worked example

A USB paired with one computer (instance id `3fa2c41b9e07`, token `6c0d...`) queues
`Contents/Artist/Artist - Song.mp3`, 9,123,456 bytes, content id 812, with no stems folder for
it yet. `X-AZ-Auth` values are abbreviated.

1. azmod writes `MODS/azstems-queue/1767225600123004-812.req`:

   ```
   file=/Contents/Artist/Artist - Song.mp3
   title=Song
   ```

2. The helper browses `_azstems._tcp`, finds a record with `id=3fa2c41b9e07` at 192.0.2.20
   port 8766, and says hello:

   ```
   GET /v2/hello?kid=<first 16 hex of sha256(token)>&nd=<32 hex>

   200 {"id": "3fa2c41b9e07", "v": 2, "name": "Studio Mac", "ns": "<32 hex>", "proof": "<64 hex>"}
   ```

   The proof checks out with the USB's token; both sides derive the session key.

3. It creates the job:

   ```
   POST /v1/jobs
   Content-Type: application/json; charset=utf-8
   X-AZ-Auth: ns=<ns> ctr=1 sig=<64 hex>

   {"file":"Contents/Artist/Artist - Song.mp3","size":9123456,"mtime":1767225600,"existing":[]}

   201 {"id": "a1b2c3d4e5f60718", "folder": "Artist - Song"}
   ```

   and adds `job=a1b2c3d4e5f60718` and `srv=3fa2c41b9e07` to the request.

4. It uploads the track:

   ```
   PUT /v1/jobs/a1b2c3d4e5f60718/audio
   Content-Length: 9123456
   X-AZ-Auth: ns=<ns> ctr=2 sig=<64 hex>

   200 {"id": "a1b2c3d4e5f60718", "state": "queued", "pct": 0, "position": 0, "folder": "Artist - Song", "file": "Contents/Artist/Artist - Song.mp3", "files": null, "error": null}
   ```

   and adds `up=1`.

5. It polls on each pass (every 3 s), saying hello again first:

   ```
   GET /v1/jobs/a1b2c3d4e5f60718
   200 {..., "state": "running", "pct": 37, "position": null, ...}

   GET /v1/jobs/a1b2c3d4e5f60718
   200 {"id": "a1b2c3d4e5f60718", "state": "done", "pct": 100, "position": null, "folder": "Artist - Song", "file": "Contents/Artist/Artist - Song.mp3", "files": [{"name": "drums.ogg", "size": 4718592}, {"name": "bass.ogg", "size": 3145728}, {"name": "other.ogg", "size": 5242880}, {"name": "vocals.ogg", "size": 3670016}, {"name": "versions.txt", "size": 74}, {"name": "source.json", "size": 118}], "error": null}
   ```

6. It downloads each file into `STEMS/.Artist - Song.part/`, checking each size:

   ```
   GET /v1/jobs/a1b2c3d4e5f60718/files/drums.ogg     200, 4718592 bytes
   ...
   GET /v1/jobs/a1b2c3d4e5f60718/files/source.json   200, 118 bytes
   ```

7. It renames `STEMS/.Artist - Song.part` to `STEMS/Artist - Song`, syncs, and deletes the job:

   ```
   DELETE /v1/jobs/a1b2c3d4e5f60718
   200 {"ok": true}
   ```

8. It removes the request, writes `812` to `/tmp/azrs/done`, and reports "Stems ready: Song".
   The row shows the stems mark; the next load of the track plays its stems.
