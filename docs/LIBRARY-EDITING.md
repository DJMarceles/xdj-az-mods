# Library editing

Playlist Editing writes to the rekordbox library on a DJ's USB stick. A bug here does not
stop the music; it loses playlists, or leaves a USB that rekordbox refuses to open. Every
rule on this page comes from one of those outcomes; the short list is in
[Rules for contributors](#rules-for-contributors).

## Code

The deck side is part of `azmod.so`:

| File | Role |
|---|---|
| `src/azmod/playlist_editor.c` | knob reorder of tracks and playlists; NEW, RENAME and folder REMOVE; the hand-off to each USB's library thread; the `export.pdb` follow-up after every edit (`mv_pdb_sync`) and after EP147's own playlist writes (the writer hooks) |
| `src/azmod/playlist_actions.c` | the MENU actions: REMOVE, ADD TO, COPY, CUT, PASTE, UNDO, Tag List copy; every result message |
| `src/azmod/menu_popup.c` | the buttons in EP147's own MENU panel |
| `src/azmod/library_edits.c` | every write: the undo journal, the library backup, cross-stick copy, the SQL spool |
| `src/azmod/library_db.c` | SQLite helpers on EP147's own connection; the per-stick library server check |
| `src/azmod/pdb_sync.c` | keeps `export.pdb` consistent with `exportLibrary.db` (libc only) |
| `src/azmod/pdb_stock.c` | the schedule for re-syncing `export.pdb` after EP147's own playlist writes (libc only) |
| `src/azmod/stem_marks.c` | Stem Marks' read-only library connection and its eject rules |
| `src/azmod/sdk_lib.c` | the community mod Library service's read-only connection |

The desktop app's backup and repair are specified in
[The app's Library Backup and Repair](#the-apps-library-backup-and-repair) by what they read
and write; the app's source is not in this tree.

## The library files

All three live in `PIONEER/rekordbox/` on the USB:

| File | Format | Read by |
|---|---|---|
| `exportLibrary.db` | OneLibrary: SQLite, encrypted with SQLCipher | the XDJ-AZ (EP147), current players, rekordbox |
| `export.pdb` | DeviceSQL, a paged binary format | older players, and rekordbox when it shows the USB as a **Device Library** |
| `exportExt.pdb` | DeviceSQL extension file | never written by the deck; checked and repaired by the desktop app |

EP147 reads and writes `exportLibrary.db` only. azmod makes every Playlist Editing change in
OneLibrary first, then brings `export.pdb` into line with it. A mismatch shows when the
USB is opened in rekordbox (see
[What rekordbox checks](#what-rekordbox-checks-when-it-opens-a-device-library)).

**The key.** Playlist Editing never needs the database key: every write runs on EP147's own,
already open connection. Two features open a read-only connection of their own, Stem Marks
and the community mod Library service; their key comes from `assets/onelibkey_sm.h.in`, a
template that `tools/extract-assets.py` fills from an official `azmod.so` (see
[BUILDING.md](../BUILDING.md)). The key is not part of this source.

## Playlist Editing on the deck

Playlist Editing is a Mod Menu row. The hooks are installed at every EP147 start and follow
the row live: off, the gesture and the MENU buttons below are inert and the deck edits as
stock. The `export.pdb` follow-up after EP147's own playlist writes runs either way
([After EP147's own writes](#after-ep147s-own-writes)).

### What EP147 does natively

| Where | Stock action |
|---|---|
| EP147's library writers (`music_library::InformationUpdater`, `SqlitePlaylistUpdater`) | reorder, remove and add playlist tracks; azmod calls these instead of writing those rows itself |
| MENU on the playlist list | **DELETE** (one playlist), **ALL DELETE** (every playlist and folder on the USB) |
| Tag List | **CREATE PLAYLIST**, **REMOVE ALL TRACKS** |
| Track rows | **PLAYED / UNPLAYED** |

### What azmod adds

| Action | Where | How the write is made |
|---|---|---|
| Reorder a track (hold the browse knob, turn, click; BACK cancels) | a playlist's track list, sorted by `#` ascending | EP147's `movePlaylistTrack` |
| REMOVE (track) | MENU on a track row of a playlist | EP147's `removePlaylistTrack` |
| ADD TO, PASTE of a track (same USB) | MENU on any track row | EP147's `SqlitePlaylistUpdater` add, at position N+1 |
| PASTE of tagged tracks (same USB) | MENU on a playlist row | as ADD, once per track; refused before the first write if any track is not on that USB |
| Reorder a playlist or folder (knob) | the playlist list | azmod's SQL: siblings renumbered 1..N |
| Drop a playlist or folder onto a folder (knob) | the playlist list | azmod's SQL: re-parent |
| NEW PLAYLIST, NEW FOLDER | MENU > NEW | azmod's SQL: one `INSERT` |
| RENAME | MENU > EDIT | azmod's SQL: one `UPDATE` |
| REMOVE (playlist or folder) | MENU > EDIT | azmod's SQL: the whole tree, one transaction |
| COPY / CUT, then PASTE of a playlist or folder (same USB) | MENU | azmod's SQL: new rows, or a re-parent for CUT |
| PASTE onto the other USB | MENU | [cross-stick copy](#cross-stick-copy-and-the-spool): files, then an SQL spool |
| UNDO | the small button in the corner where the panel's X was | azmod's SQL: restores the rows the last edit changed |

Changes to the stock buttons:

| Stock | With Playlist Editing on |
|---|---|
| DELETE (playlist list) | relabeled REMOVE; runs azmod's journaled remove, so UNDO can restore it |
| ALL DELETE | hidden and disabled; a click that still reaches it is refused (it removes every playlist on the USB behind a box that only says "ALL DELETE") |
| Tag List actions, PLAYED / UNPLAYED | stock |

Tracks are reordered and removed only in a list sorted by `#` ascending: only there does a
display row equal a stored position.

### How one edit runs

`playlist_editor.c`, `library_edits.c`:

1. **Request.** The message thread fills the one shared request slot (`g_mv`) with the
   operation, the USB's media id and the rows it names, and arms it. One edit is in flight
   at a time. A knob drop or BACK is recorded by the mixer-bus read thread
   (`drag_handoff.h`) and performed on the next message tick (`drag_commit`,
   `drag_cancel`).
2. **The right USB's thread.** Each USB has its own EP147 library server
   (`MusicLibraryServer`) on its own thread. Only the server whose own database carries that
   media id takes the request, at a message boundary (`mlserver_onmsg_hook`); each link from
   the server to its SQLite connection is checked by vtable before use (`mlserver_chain`).
   A request not taken within 2.5 s (`MV_DEADLINE_MS`) expires and nothing is written.
3. **Preconditions.** The connection is open (SQLite's magic checked) and in autocommit, so
   EP147 is not mid-transaction.
4. **Backup.** The first write after the USB was mounted is preceded by
   [the deck's library backup](#the-decks-library-backup). No backup, no edit.
5. **Prove the rows.** The edit re-reads what it is about to change and refuses if the USB
   no longer matches what was on screen:
   - the track at the displayed position must be the grabbed track;
   - the target playlist must still carry the name it had when it was chosen (ids are per
     USB, and another USB can sit in the same port);
   - a knob reorder must find the siblings' names spelling exactly the rows that were on
     screen.

   A playlist whose tracks are not numbered 1..N is first renumbered in
   `(sequenceNo, rowid)` order, the order rekordbox 7 renumbers to.
6. **Write, then read back.** On EP147's own connection. A multi-row edit is one
   transaction, read back before `COMMIT` and rolled back on any mismatch. A single-row edit
   is read back after it is written.
7. **Refresh.** EP147's list caches are dropped so the open screen re-reads in place.
8. **Device Library.** `export.pdb` is brought into line (`mv_pdb_sync`, then `pdbs_sync`;
   see [Keeping export.pdb in sync](#keeping-exportpdb-in-sync)). A failure there leaves
   `export.pdb` as it was; the OneLibrary edit stands.
9. **Result.** One line in EP147's banner strip, unless the list already shows the outcome.

### Undo

| | |
|---|---|
| Recorded | before the write: the rows the edit can touch (the track lists of the playlists involved, the children of the folders involved); after `COMMIT`: the same rows again |
| Depth | 32 entries (`JS_MAX`), in memory only; an EP147 restart clears it |
| Precondition | the rows are exactly what the edit left |
| Write | delete those rows and re-insert the "before" rows in one transaction, then read them back |
| Rows changed since (EP147, rekordbox, an edit outside azmod) | the entry is dropped with a message; nothing is written |
| Track MOVE made by CUT and PASTE | one entry |

### Limits

| Limit | Value | Source |
|---|---|---|
| Tracks in one cross-stick copy | 300 | `CP_MAX_TRACKS`, `library_edits.c` |
| Playlists and folders inside one removed or copied folder | 500 | `FD_MAX`, `playlist_editor.c` |
| Tagged tracks in one COPY | 100 | `TAG_MAX`, `azmod_internal.h` |
| One spool file | 1 MB | `LS_SPOOL_CAP`, `library_edits.c` |
| Undo depth | 32 | `JS_MAX`, `library_edits.c` |
| Request taken by a library thread | 2.5 s | `MV_DEADLINE_MS`, `playlist_editor.c` |

### Cross-stick copy and the spool

PASTE onto the other USB copies the files first and the rows last (`library_edits.c`):

1. The source USB's library thread reads the rows, read-only.
2. A `SCHED_IDLE` worker copies the audio, its three analysis files (`ANLZ0000.DAT`, `.EXT`,
   `.2EX`, which carry the cues) and its artwork. Each file goes to `<dst>.azcp` and is
   renamed into place, so nothing sees half a file.

   | File | Rule |
   |---|---|
   | Audio | keeps its path; if a file of a different size is already there, the copy is refused and nothing is overwritten |
   | Analysis | folders are named per USB; if the target has a different file at that path, a fresh sibling folder is used and the new row points at it |
   | Artwork | goes to `PIONEER/Artwork/azcp/<source media id>_<name>`, since artwork names are per-stick sequences |

3. The worker writes the rows as SQL to `<stick>/PIONEER/azspool/azcp-<...>.sql` (written
   as `.tmp`, fsynced, then renamed) and waits up to 30 s for it to be applied. A spool not
   applied by then stays in place and is applied when that USB's library thread is next
   free.
4. The target USB's library thread applies the spool (`ls_apply_file`):

   | Check | On failure |
   |---|---|
   | file under 1 MB, with no NUL byte | renamed `.rejected`, nothing written |
   | every non-blank, non-`--` line is one `INSERT INTO`, `INSERT OR IGNORE INTO` or `UPDATE` on `content`, `artist`, `album`, `genre`, `key`, `image`, `playlist`, `playlist_content` or `label`, with exactly one `;`, at its end | renamed `.rejected` before anything is written |
   | connection in autocommit | the spool stays; retried on the next message |
   | library backup taken | renamed `.failed`, nothing applied |
   | the whole file runs as one transaction | any failing statement rolls all of it back; renamed `.failed` |

   Inside the same transaction, after the spool's statements, a pasted track the target
   already had is pointed at the copied artwork when its own artwork file is gone; a failure
   there keeps the rows already written. After `COMMIT`, `export.pdb` is synced with the source USB's
   `export.pdb` as the donor of track rows, and only then is the spool deleted: the deletion
   is what tells the worker the paste is done.

A pasted playlist is always a new list. If its name is taken at that level it is named
`<name> (1)`, `(2)` and so on, as rekordbox's own device import does; the free name is chosen
inside the spool's transaction.

## Keeping export.pdb in sync

`pdb_sync.c` runs on the USB's library thread after every Playlist Editing write, after
every applied cross-stick spool and after EP147's own playlist writes. It is plain C with no EP147 calls, so the host tests compile it natively. One sync
runs at a time across all USBs (`g_ps_lock`).

### After EP147's own writes

Stock CREATE PLAYLIST (Tag List), and stock DELETE or ALL DELETE where reachable, write
`exportLibrary.db` and leave `export.pdb` as it was, so on a stock deck the two drift apart.
azmod follows those writes whether or not Playlist Editing is on (`pdb_stock.c`, scheduled
from `playlist_editor.c`):

| | |
|---|---|
| Watched | the nine write slots (`+0x10` to `+0x50`) of `music_library::SqlitePlaylistUpdater`, the device implementation of EP147's playlist writer (create, add track, the deletes, remove, move). Every stock playlist write goes through it on the USB's library thread, inside that USB's `MusicLibraryServer::onMessage`; a slot hit there marks the message. azmod's own edits and spool are excluded (they sync themselves). Nothing else is intercepted: no SQLite calls, no file polling; the history and cloud writers are other classes. The slots are hooked only when the vtable's RTTI names `SqlitePlaylistUpdater` and every slot holds an EP147 code address; otherwise none is hooked and stock writes are not followed. |
| When | when the marked message returns, its USB is scheduled. The sync is due 1 s after the last stock write (`PST_DEBOUNCE_MS`), so a create and its track rows, or several deletes in a row, give one sync. The message tick wakes the library threads, and that USB's own server runs the sync at the start of its next message, never on the UI thread. A connection inside a transaction is left alone and retried. A due sync no library thread takes within 15 s (`PST_EXPIRE_MS`; the USB went away) is dropped. |
| Identity | the USB whose library server ran the write. A server that cannot be named, or a USB whose mount does not resolve, is not synced. |
| Playlist Editing toggle | no effect: the sync runs whether the row is on or off |
| Backups | `export.pdb.azbak` as for any sync. The paired library backup (`exportLibrary-BACKUP.db` with `export-BACKUP.pdb`) is attempted only after the sync succeeded or found nothing to do, so the pair is consistent. It is the same once-per-insertion backup an edit takes, so it is skipped when that insertion was already backed up. A failed sync takes none and the earlier pair stands. |

### What it changes

| Table | Change |
|---|---|
| `track` (0), cross-stick paste only | for every OneLibrary track with no `export.pdb` track row but one in the **source** USB's `export.pdb`: the source's own rekordbox-made row is copied with its ids remapped. Artist, album, genre, label and key are reused by name, or a minimal new row is appended; an artwork row is added for the pasted artwork path; the analysis path (string 14) is set to where the paste put the analysis files, and a row whose path cannot be set is not copied; the track gets the next free id. A track with no source row (a source USB without `export.pdb`, a track added another way) is skipped and logged. Track rows are never synthesized. |
| `track` (0), existing rows | a track this file already lists, whose OneLibrary artwork is a paste's copy under `/PIONEER/Artwork/azcp/` while its own artwork file is gone, is pointed at an artwork row for the copy (`ps_paste_art`) |
| `playlist_tree` (7) | rewritten to mirror OneLibrary's `playlist`: rows in playlist id order; each row's sort value is its 0-based rank under its parent by `(sequenceNo, playlist_id)` |
| `playlist_entries` (8) | rewritten to mirror `playlist_content`: entries numbered 1..n per playlist in `(sequenceNo, rowid)` order; an entry whose track has no `export.pdb` track row is left out |

Pages these changes do not touch keep their bytes.

### The DeviceSQL layout it relies on

Little-endian throughout. File header offsets are from the start of the file; page offsets
are from the start of the page. A file is accepted only if it is 4 KB to 64 MB, its page
length is 512 to 65536 and divides the file size, and it has at most 64 tables (`ps_load`).

**File header (page 0)**

| Offset | Size | Field |
|---|---|---|
| `0x04` | u32 | page length |
| `0x08` | u32 | number of tables |
| `0x0c` | u32 | next unused page |
| `0x14` | u32 | sequence |
| `0x18` | u32 | a second counter, compared by rekordbox's open checks |
| `0x1c` | 16 per table | `{type, empty_candidate, first_page, last_page}` |

**Data page header**

| Offset | Size | Field |
|---|---|---|
| `0x04` | u32 | this page's index |
| `0x08` | u32 | table type |
| `0x0c` | u32 | next page in the table's chain |
| `0x10` | u32 | sequence |
| `0x18` | 13 bits | slot count (deleted slots included): byte `0x18` plus the low 5 bits of `0x19` |
| `0x19` | 11 bits | present row count: `0x19 >> 5 \| 0x1a << 3` |
| `0x1b` | u8 | flags: `0x40` = index page, `0x10` = the page has deleted slots |
| `0x1c` | u16 | free bytes |
| `0x1e` | u16 | used bytes of the row heap |
| `0x20` | u16 | dirty-row count, low 13 bits (`0x1fff` = nothing pending); bits 14 and 15 kept |
| `0x22` | u16 | first dirty row, low 13 bits (`0x1fff` = none) |

**Row heap and row index.** Rows grow upward from `0x28`. The row index grows down from the
end of the page in groups of 16 rows, `0x24` bytes per group. For group `g`, with
`base = page_length - 0x24 * g`:

| Position | Content |
|---|---|
| `base - 6 - 2k` | offset of row `k` of the group, relative to `0x28` |
| `base - 4` | the group's present mask |
| `base - 2` | the group's dirty mask |

**Index page free list.** A table's index page lists the data pages that have deleted
slots: capacity at `0x24`, count at `0x26` and again at `0x38`, `u32` entries of
`page << 3` from `0x3c`. Unused entries hold `0x1ffffff8`.

**Rows written**

| Table | Layout |
|---|---|
| `playlist_tree` | `u32 parent, u32 0, u32 sort, u32 id, u32 is_folder`, then the name |
| `playlist_entries` | `u32 entry_index, u32 track_id, u32 playlist_id` |
| `track` | id at `0x48`; 21 `u16` string offsets from `0x5e` (string 14: analysis path, string 20: file path); `u32` references at `0x0c`, `0x24`, `0x2c`, `0x44` (artist), `0x1c` (artwork), `0x20` (key), `0x28` (label), `0x3c` (genre), `0x40` (album) |

**Strings**

| Content | Encoding |
|---|---|
| ASCII, shorter than 126 bytes | one length byte `(len + 1) << 1 \| 1`, then the bytes |
| anything else | `0x90`, a `u16` total length including the 4-byte header, one pad byte, then UTF-16LE. A name that does not fit is cut at a character boundary; ill-formed UTF-8 becomes U+FFFD. |

### Rules every written page obeys

From rekordbox's own DeviceSQL page manager, as checked against rekordbox 7.2.19. Rules 1
and 2, broken, make rekordbox crash or hang; rules 3 and 4 keep azmod's pages the shape
rekordbox itself writes.

1. **Exactly one dirty row.** Before rekordbox allocates, frees or reuses space on a page, it
   walks the dirty masks from group `first >> 4` toward the start of the page, subtracting
   each mask's bit count until the dirty count reaches exactly 0, with no other bound. A
   count the masks cannot supply walks off the page and crashes rekordbox. Every page
   `pdb_sync.c` writes leaves exactly one dirty row, the last one written (count 1,
   first = that row), as rekordbox does after inserting one row. A fresh page is clean
   (`0x1fff / 0x1fff`).
2. **No data page with zero slots** in `playlist_tree` or `playlist_entries`: a 0-slot data
   page sends rekordbox's open into a runaway allocation. With at least as many rows as
   pages, rows are spread so that every page keeps at least one. With fewer rows than
   pages, rows are packed from the first page and each surplus page becomes a
   **tombstone**: one deleted slot, no present row, flag `0x10`, clean dirty state, which is
   how rekordbox leaves a page whose rows it deleted.
3. **The free list names exactly the pages with deleted slots**, as on every file rekordbox
   writes. It is rebuilt after each rewrite.
4. **At most 255 rows per page** written by azmod (`PS_MAXROWS`), so the slot bits in `0x19`
   stay 0 on azmod's pages.

### Write safety

`pdbs_sync()` never writes a file it has not proven:

1. The new file is built in memory.
2. It is re-parsed (`ps_verify`): every table chain walks; no page belongs to two chains; no
   table's `empty_candidate` page is in use; every row index entry is in range; no 0-slot
   playlist data page; the row counts of tables 0, 7 and 8 are exactly what was intended.
3. It passes rekordbox's open checks (`ps_open_ok`,
   [below](#what-rekordbox-checks-when-it-opens-a-device-library)).
4. Every changed data page passes the dirty-row walk (`ps_walk_ok`).
5. If nothing changed, nothing is written.
6. Once per insertion of the USB (keyed on `exportLibrary.db`'s device and inode), the
   current file is written whole to `export.pdb.azbak` and fsynced. If that fails, nothing
   is written.
7. The new file replaces `export.pdb` whole (`ps_replace`), never by rewriting pages in
   place: an in-place write cut by a pulled USB leaves pages of two versions under the real
   name. The sequence keeps a whole file on the media at every step:

   | Step | |
   |---|---|
   | 1 | the old `export.pdb` is held open, so its clusters stay allocated |
   | 2 | the new file is written to `export.pdb.aznew` and fsynced |
   | 3 | `export.pdb.aznew` is renamed over `export.pdb` |
   | 4 | the directory is fsynced |
   | 5 | the new file is fsynced: the directory slot now names the new clusters |
   | 6 | the old file is closed (its clusters freed), the new one fsynced again and read back |

   On vfat a rename over an existing file reuses the target's directory slot, so there is
   no moment without an `export.pdb`; a pull between steps leaves the old or the new file
   whole, at worst plus unreferenced clusters. On exFAT a pull inside the rename can leave
   only `export.pdb.aznew`; the next sync finds `export.pdb` missing and puts a whole
   `.aznew` (it parses and passes the open checks) back under the real name (`ps_recover`).
   A stale `.aznew` byte-identical to `export.pdb` may share its clusters, so it is removed
   only when `FIBMAP` proves the two are separate.
8. If the replace fails before the rename, `export.pdb` is as it was. If the read-back
   differs, the original bytes are put back with the same replace.

## Transactions and renames

| Write | How | Why |
|---|---|---|
| OneLibrary edit | EP147's own connection, autocommit checked first; multi-row edits in one transaction, read back before `COMMIT` | SQLCipher and locking stay EP147's; nothing interleaves with EP147's writes on that thread |
| Library backup | copied over the older slot in place (no `O_TRUNC`, no rename), under a read transaction | a pulled USB can break at most the slot being written |
| `export.pdb` | whole new file as `.aznew`, ordered fsyncs, renamed over the old one, read back | the name always holds one whole version |
| `export.pdb.azbak` | whole file in place (truncated only if it shrank), fsynced | written before any change to `export.pdb` |
| Copied audio, analysis, artwork | `<file>.azcp`, then rename | a new file: nothing reads it until the rename |
| Spool file | `.tmp`, fsync, then rename | same |

## The deck's library backup

`lib_backup()` in `library_edits.c` runs before the first write to a USB's
`exportLibrary.db` after it was mounted, and before any spool is applied. One backup runs at a
time across all USBs.

| Step | Rule |
|---|---|
| Already done? | "mounted" means the database file's `(st_dev, st_ino)`; a re-insert or a different USB is a new pair and is backed up again. The record is in memory, so an EP147 restart also takes a new backup. |
| Integrity | `PRAGMA quick_check` must say `ok`. A library that fails never overwrites a backup, and editing that USB is refused ("This USB's library failed a check · editing is off for it"). |
| WAL | if `exportLibrary.db-wal` is not empty, `PRAGMA wal_checkpoint(TRUNCATE)` runs; if it is still not empty, the backup and the edit are refused |
| Space | free space for twice the library, plus twice `export.pdb`, plus 1 MB |
| Slot | `exportLibrary-BACKUP.db` or `exportLibrary-BACKUP-2.db`. A slot that cannot be a whole file (missing, under 4096 bytes, or not a multiple of 1024 bytes) is written first; otherwise the **older** one. The newer good backup is never touched, so the newest backup is the one with the later modification time, whichever name it has. |
| Copy | inside a read transaction on EP147's connection, so no writer commits during it; hashed as it is read, fsynced, read back and hashed again |
| Paired `export.pdb` | the same moment's `export.pdb` goes into the matching slot, `export-BACKUP.pdb` or `export-BACKUP-2.pdb`, so a restore never pairs one moment's library with another moment's Device Library. No `export.pdb` on the USB: that slot's `.pdb` is deleted. The copy fails: that slot's `.pdb` is deleted and the edit is refused. |
| Any other failure | nothing changes; the edit is refused ("Couldn't back up this USB's library · nothing was changed") |

## Read-only connections and USB eject

Stem Marks (`stem_marks.c`) reads every mounted USB's library to decide which rows have
stems. It never delays an eject or blocks an EP147 write:

| Rule | Mechanism |
|---|---|
| No locks | its own connection, `file:<stick>/PIONEER/rekordbox/exportLibrary.db?immutable=1`, opened `SQLITE_OPEN_READONLY \| SQLITE_OPEN_URI`. `immutable=1` takes no lock, so EP147's writer never sees `SQLITE_BUSY` because of it. A read torn by an EP147 write fails decryption or the title check and is treated as unknown, never as a wrong answer. The connection is reopened when the file's size or modification time changes. |
| Only while EP147 holds the USB | the deck's eject script polls, once a second and up to 20 times, until EP147 has no file open on the USB, and only then unmounts. Stem Marks' connection is an EP147 file descriptor, so a cached one would hold every eject for the full 20 s. EP147 closes its own connection to the USB's library first when it ejects, so Stem Marks counts EP147's descriptors on that `exportLibrary.db` (`/proc/self/fd`, minus its own, at most four times a second per USB) and uses the USB only while that count is above zero. |
| Hands off on the first close | when EP147's count for a USB drops, an eject has begun: Stem Marks closes its connection to that USB and stays away from it for 10 s (`SM_HANDSOFF_MS`). The eject then completes at the script's next poll instead of waiting out all 20. |
| Idle close | a connection unused for 2 s (`SM_IDLE_MS`) is closed |

The community mod Library service (`sdk_lib.c`) opens its own `immutable=1`, read-only
connection per call, reads a snapshot, and closes it before calling the mod back. The file's
size and time are compared before and after; a change retries the read once, and a second
change returns -1.

Any code that opens files on a USB holds nothing open on a USB EP147 is letting go of.

## What rekordbox checks when it opens a Device Library

### "Device library is corrupted."

rekordbox says this, and offers to delete the Device Library, when its start-up of the
Device Library fails:

1. A file named `brokendb` exists in `PIONEER/rekordbox/`. rekordbox's crash handler writes
   it when rekordbox crashes while opening that USB's Device Library, and nothing in
   rekordbox removes it. After one such crash the USB reads as corrupted every time,
   whatever `export.pdb` holds.
2. DeviceSQL's open page checks fail, for any page on any table's chain:

   | Check | Fails when |
   |---|---|
   | page index field | not this page's index (0 is allowed) |
   | page index | `>=` the header's next unused page |
   | page type | greater than the number of tables |
   | next page | `>=` next unused page, unless it is the end marker `0x3ffffff` |
   | page `0x14` | greater than the header's `0x18` |
   | page sequence | greater than the header's sequence, when header `0x18` is 0 or equals page `0x14` |
   | next page | greater than the table's `empty_candidate`, unless the end marker |
   | row counts and dirty fields | when the top 3 bits of `u32 @0x18` are neither 0 nor 2: slot or present count above the limit for the page length (`0x1fe` up to 4 KB pages, `0x3fe` up to 8 KB, `0x7fe` above), or a dirty field above `0x1fff` |

   `ps_open_ok()` in `pdb_sync.c` is the same check; `export.pdb` is never written when it
   fails.
3. Reading any of its 20 tables fails.

A 0-slot playlist data page ([rule 2](#rules-every-written-page-obeys)) passes these checks
and makes the open never return, the kind of crash that leaves `brokendb` behind.

### "We have found a library inconsistency."

Shown when a OneLibrary track has no `export.pdb` track row. A difference in playlist
contents alone does not trigger it. This is why a cross-stick paste copies the source's track
rows, and why a track with no source row keeps the page showing.

### A crash on a later write

A page whose dirty-row count is not supplied by its dirty masks passes the open, then
crashes rekordbox the next time it allocates, frees or reuses space on that page (for
example, Delete Playlist on the device).

## The app's Library Backup and Repair

The AZ-MODS desktop app has two tools for a USB with a rekordbox library. The restore works
in the folder that holds the library (`PIONEER/rekordbox/`, or `.PIONEER/rekordbox/` when
that copy is newer); the check and repair work in `PIONEER/rekordbox/`.

### Restoring a deck backup

The app lists the two slots the deck writes, newest first by modification time, and checks
each read-only (SQLCipher, `PRAGMA quick_check`, playlist and track counts). A slot's `.pdb`
counts as its pair only when it is a whole DeviceSQL file (page length 512 to 65536, at
least two pages, size a whole number of pages) and its modification time is within 120 s of
the slot's library.

A restore:

1. checks the chosen backup first and refuses it if the check fails; nothing is changed;
2. asks for confirmation when `exportLibrary.db-wal` or `-shm` exists, since rekordbox may
   have the library open;
3. deletes nothing: the current `exportLibrary.db` and any `-journal`, `-wal` or `-shm`
   beside it are renamed to `exportLibrary-REPLACED-<time>.db[-*]` (a stale journal left in
   place would be applied to the restored file);
4. copies the backup in under a temporary name, fsyncs it, checks the size, and renames it
   to `exportLibrary.db`; the backup itself stays;
5. when the slot has a paired `.pdb`, does the same for `export.pdb`, keeping the current one
   as `export-REPLACED-<time>.pdb`;
6. checks the restored library again and reports whether `export.pdb` was restored. A backup
   without a `.pdb` restores the library alone, and the result says so.

### Checking and repairing the Device Library

The check reads `export.pdb` and `exportExt.pdb` and reports, per file:

| Finding | Fails the check |
|---|---|
| a page whose dirty-row walk does not stop inside the page's own row groups | yes |
| a 0-slot playlist data page | yes |
| a rekordbox open-check failure | yes |
| a free list that does not match the pages with deleted slots | no: rekordbox's open accepts it |
| `brokendb` present | reported |

The repair runs three steps, each only when needed:

1. **Dirty state.** Each defective data page is put in the clean state that rekordbox's own
   walk leaves: dirty count and first dirty row `0x1fff` (bits 14 and 15 kept), every dirty
   mask 0, and flag `0x10` cleared when the slot count equals the row count. Rows and every
   other byte are untouched. Both files are planned before either is written, and the
   repair refuses if any byte outside those header and mask bytes would change, if a defect
   would remain, or if a page's row groups would reach below its header.
2. **Playlist tables.** When `export.pdb` has a 0-slot playlist page, `playlist_tree` and
   `playlist_entries` are rebuilt from the USB's `exportLibrary.db` with the same rules as
   `pdb_sync.c` (byte-identical output wherever that succeeds). The result must pass the
   open checks, the 0-slot check, the free-list check and the dirty-row walk before anything
   is written.
3. **`brokendb`.** Renamed to `brokendb.azbak-<time>`, only when `export.pdb` now passes the
   open checks and has no 0-slot page and no dirty-row defect; otherwise rekordbox would
   crash on it again.

Every file the repair changes is first copied to `<name>.azbak-<time>` and the copy is
verified by hash. The new content is written to a temporary file, read back and checked; the
original is checked to be unchanged since it was read (rekordbox, the deck or another program
could have written it); only then is the temporary file renamed over it. Nothing is deleted.

## Rules for contributors

- **Write a library row only on that USB's library thread, and only on EP147's own
  connection.** Resolve the USB from the server's own database, never by arrival order or
  by walking from one server to another.
- **No write without a backup**, and no backup over a good one from a library that fails
  `quick_check`.
- **Prove the rows before writing, read them back after.** If what is on the USB does not
  match what was on screen, refuse.
- **Never leave `export.pdb` in a state rekordbox has not been seen to accept.** Every page
  written passes `ps_verify`, `ps_open_ok` and `ps_walk_ok`; a new kind of page gets a check
  there first.
- **Write a file that must survive a pull so that its name always holds one whole
  version:** in place for the backup slots, a new name renamed into place for new files, and
  the ordered replace above for `export.pdb`.
- **Never synthesize an `export.pdb` track row.** Copy rekordbox's own, or leave the track
  out.
- **Hold no file open on a USB that EP147 is letting go of.**
