/* SPDX-License-Identifier: GPL-3.0-only */
/* The rekordbox library services for community mods (guest/azmod/sdk_lib.c): lib_playlists and
 * lib_tracks, read-only. A small exportLibrary.db (plain SQLite: sqlite3_key is a no-op here)
 * sits on two fake sticks under a scratch media folder. Questions asked:
 *   - playlists come back with their parents and folder flags, in the library's order;
 *   - a playlist's tracks come back in playlist order with artist, album, key, BPM, length and
 *     an absolute path; playlist 0 is every track; a callback that returns nonzero stops;
 *   - stick 0 is the USB 1 port and 1 is USB 2, by the deck's own bus rule (not mount names);
 *     a stick behind a hub, or two libraries in one port, is -1, never a guess;
 *   - refusals (thread, callback, stick number) go to the mod's log;
 *   - the connection the services open cannot write;
 *   - nothing is read from the control-bus thread or the UI thread.
 * The controls open the database read-write and read on the UI thread; each must fail. LIB_INC = the sdk_lib.c under test. */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "azmod-mod.h"

#define PL_ATTR_FOLDER 1
#define ONELIBRARY_KEY_SM "test-key"
static __thread int g_cm_in_frame, g_cm_on_ui;
static char g_media[PATH_MAX], g_mounts[PATH_MAX], g_sysblock[PATH_MAX];
#define SDK_LIB_MEDIA g_media
#define SDK_LIB_MOUNTS g_mounts
#define SDK_LIB_SYSBLOCK g_sysblock
static char g_cursrc[PATH_MAX];
#define SDK_LIB_CURSRC_DIR g_cursrc

/* the spool service's flag (library_edits.c) and azstem's playheads, as lib_spool_kick sees them */
static int g_ls_leftover; /* _Atomic int in library_edits.c; clang refuses __atomic_* on _Atomic */
static long long g_head[4] = {-1, -1, -1, -1};
static int g_moving[4];
long long azstem_play_obs(int deck);
long long azstem_play_obs(int deck) {
    if (deck < 0 || deck > 3 || g_head[deck] < 0)
        return -1;
    return g_moving[deck] ? (g_head[deck] += 4800) : g_head[deck];
}
static void deck_src(int d, const char *path) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/azstem.cursrc%d", g_cursrc, d + 1);
    FILE *f = fopen(p, "w");
    fprintf(f, "%s\n", path);
    fclose(f);
}

/* the community host, as sdk_lib.c sees it */
typedef struct {
    char id[33];
} cm_mod;
static cm_mod g_mod = {"mod-a"};
static char g_last_log[256];
static uintptr_t g_not_mine;
static cm_mod *cm_self(const char *id, uintptr_t ra, const char *what) {
    (void) ra, (void) what;
    return !g_cm_in_frame && id && !strcmp(id, "mod-a") ? &g_mod : NULL;
}
static int cm_called_by(cm_mod *c, uintptr_t a) {
    (void) c;
    return a != g_not_mine;
}
#define CM_RA() ((uintptr_t) 0)
static void cm_logf(cm_mod *c, int force, const char *fmt, ...) {
    (void) c, (void) force;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_log, sizeof g_last_log, fmt, ap);
    va_end(ap);
}

/* plain SQLite has no encryption: the services' key call is accepted and does nothing */
int sqlite3_key(sqlite3 *db, const void *k, int n);
int sqlite3_key(sqlite3 *db, const void *k, int n) {
    (void) db, (void) k, (void) n;
    return 0;
}

#include LIB_INC

static void sql(sqlite3 *db, const char *q) {
    char *err = NULL;
    if (sqlite3_exec(db, q, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "fixture: %s: %s\n", q, err);
        exit(2);
    }
}
static void make_stick(const char *name, const char *pl_name) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", g_media, name);
    mkdir(p, 0755);
    strcat(p, "/PIONEER");
    mkdir(p, 0755);
    strcat(p, "/rekordbox");
    mkdir(p, 0755);
    strcat(p, "/exportLibrary.db");
    sqlite3 *db;
    if (sqlite3_open(p, &db) != SQLITE_OK)
        exit(2);
    sql(db, "CREATE TABLE playlist (playlist_id INTEGER, sequenceNo INTEGER, name TEXT, image_id INTEGER,"
            " attribute INTEGER, playlist_id_parent INTEGER)");
    sql(db, "CREATE TABLE playlist_content (playlist_id INTEGER, content_id INTEGER, sequenceNo INTEGER)");
    sql(db, "CREATE TABLE content (content_id INTEGER, title TEXT, artist_id_artist INTEGER, album_id INTEGER,"
            " key_id INTEGER, path TEXT, bpmx100 INTEGER, length INTEGER)");
    sql(db, "CREATE TABLE artist (artist_id INTEGER, name TEXT)");
    sql(db, "CREATE TABLE album (album_id INTEGER, name TEXT)");
    sql(db, "CREATE TABLE key (key_id INTEGER, name TEXT)");
    char q[256];
    sql(db, "INSERT INTO playlist VALUES (1, 1, 'Sets', 0, 1, 0)");
    snprintf(q, sizeof q, "INSERT INTO playlist VALUES (2, 1, '%s', 0, 0, 1)", pl_name);
    sql(db, q);
    sql(db, "INSERT INTO playlist VALUES (3, 2, 'Warmup', 0, 0, 0)");
    sql(db, "INSERT INTO artist VALUES (7, 'Artist A'), (8, 'Artist B')");
    sql(db, "INSERT INTO album VALUES (5, 'Album X')");
    sql(db, "INSERT INTO key VALUES (3, '8A')");
    sql(db, "INSERT INTO content VALUES (10, 'Track Ten', 7, 5, 3, '/Contents/a/ten.mp3', 12800, 301),"
            " (11, 'Track Eleven', 8, NULL, NULL, '/Contents/b/eleven.mp3', 17400, 245),"
            " (12, 'Track Twelve', 7, 5, 3, '/Contents/a/twelve.mp3', 12000, 199)");
    sql(db, "INSERT INTO playlist_content VALUES (2, 12, 1), (2, 10, 2), (3, 11, 1)");
    sqlite3_close(db);
}

/* A block device in the fake sysfs, on USB bus `bus` (e.g. "1-1"), and its line in mounts. */
static void plug(const char *dev, const char *bus, const char *mp_name) {
    char real[PATH_MAX], link[PATH_MAX], cmd[3 * PATH_MAX];
    snprintf(real, sizeof real, "%s/devices/platform/ehci/usb1/%s/%s:1.0/host0/target0:0:0/0:0:0:0/block/%s",
             g_sysblock, bus, bus, dev);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", real);
    if (system(cmd)) {
    }
    snprintf(link, sizeof link, "%s/%s", g_sysblock, dev);
    unlink(link);
    if (symlink(real, link)) {
    }
    FILE *f = fopen(g_mounts, "a");
    if (f) {
        fprintf(f, "/dev/%s %s/%s vfat rw,relatime 0 0\n", dev, g_media, mp_name);
        fclose(f);
    }
}

struct pl_seen {
    int n;
    char names[8][64];
    int parent[8], folder[8];
};
static int on_pl(int id, int parent, const char *name, int folder, void *u) {
    struct pl_seen *s = u;
    (void) id;
    if (s->n < 8) {
        snprintf(s->names[s->n], 64, "%s", name);
        s->parent[s->n] = parent;
        s->folder[s->n] = folder;
        s->n++;
    }
    return 0;
}
static int on_count_pl(int id, int parent, const char *name, int folder, void *u) {
    (void) id, (void) parent, (void) name, (void) folder;
    ((struct pl_seen *) u)->n++;
    return 0;
}
struct tr_seen {
    int n, stop_after;
    char line[8][320];
};
static int on_tr(const az_track *t, void *u) {
    struct tr_seen *s = u;
    if (s->n < 8)
        snprintf(s->line[s->n], 320, "%u|%s|%s|%s|%s|%.2f|%.0f|%s", t->id, t->title, t->artist,
                 t->album, t->key, t->bpm, t->len_s, t->path);
    s->n++;
    return s->stop_after && s->n >= s->stop_after;
}

int main(void) {
    ut_suite = "sdk: library (read-only)";
    char root[PATH_MAX];
    snprintf(root, sizeof root, "%s/.az-sdklib.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(root))
        return 2;
    snprintf(g_media, sizeof g_media, "%s/media", root);
    snprintf(g_sysblock, sizeof g_sysblock, "%s/sys", root);
    snprintf(g_mounts, sizeof g_mounts, "%s/mounts", root);
    mkdir(g_media, 0755);
    mkdir(g_sysblock, 0755);
    FILE *mf = fopen(g_mounts, "w");
    if (mf) {
        fputs("/dev/mmcblk0p7 /home/root/settings ext4 rw 0 0\n", mf);
        fclose(mf);
    }
    /* mount names in the opposite order to the ports, so name order cannot pass */
    make_stick("sdb1", "Closing");
    plug("sdb1", "1-1", "sdb1");   /* USB 1 */
    make_stick("sda1", "Peak");
    plug("sda1", "4-1", "sda1");   /* USB 2 */
    make_stick("sdc1", "Hub");
    plug("sdc1", "1-1.2", "sdc1"); /* behind a hub: port unknown */

    /* the bus rule itself, on sysfs paths of the shapes the deck has */
    CHECK("port rule: 3-1 and 1-1 are USB 1, 2-1 and 4-1 are USB 2",
          sdk_port_of_sysfs("/sys/devices/platform/fd800000.usb/usb3/3-1/3-1:1.0/host0/target0:0:0/0:0:0:0/block/sda/sda1") == 1 &&
              sdk_port_of_sysfs("/sys/devices/platform/x/usb1/1-1/1-1:1.0/host2/target2:0:0/2:0:0:0/block/sdb") == 1 &&
              sdk_port_of_sysfs("/sys/devices/platform/x/usb2/2-1/2-1:1.0/host1/target1:0:0/1:0:0:0/block/sdc/sdc1") == 2 &&
              sdk_port_of_sysfs("/sys/devices/platform/x/usb4/4-1/4-1:1.0/host3/target3:0:0/3:0:0:0/block/sdd/sdd1") == 2);
    CHECK("port rule: a hub, a virtio disk and a path with no host are unknown",
          sdk_port_of_sysfs("/sys/devices/platform/x/usb1/1-1/1-1.2/1-1.2:1.0/host0/target0:0:0/0:0:0:0/block/sda/sda1") == 0 &&
              sdk_port_of_sysfs("/sys/devices/platform/a003e00.virtio_mmio/virtio1/block/vda/vda1") == 0 &&
              sdk_port_of_sysfs("/host0") == 0 && sdk_port_of_sysfs("") == 0);
    struct pl_seen ps = {0};
    char db0[PATH_MAX];
    CHECK("playlists on stick 0: read", sdk_lib_playlists("mod-a", 0, on_pl, &ps) == 0 && ps.n == 3);
    CHECK("stick 0 is the USB 1 port (bus 1-1), not the first mount name", ps.n == 3 && !strcmp(ps.names[2], "Closing"));
    CHECK("by parent, then position; folders and parents come through",
          ps.n == 3 && !strcmp(ps.names[0], "Sets") && ps.folder[0] == 1 && !strcmp(ps.names[1], "Warmup") &&
              ps.parent[1] == 0 && ps.folder[1] == 0 && ps.parent[2] == 1 && ps.folder[2] == 0);
    struct pl_seen ps1 = {0};
    CHECK("stick 1 is the USB 2 port (bus 4-1)", sdk_lib_playlists("mod-a", 1, on_pl, &ps1) == 0 && ps1.n == 3 &&
                                                     !strcmp(ps1.names[2], "Peak"));
    CHECK("stick 2 does not exist: -1, in the log", sdk_lib_playlists("mod-a", 2, on_pl, &ps1) == -1 &&
                                                        strstr(g_last_log, "USB 1) or 1"));
    CHECK("a stick behind a hub is never taken for a port", !sdk_stick_mount(0, db0, sizeof db0) ||
                                                                !strstr(db0, "sdc1"));
    g_not_mine = (uintptr_t) on_pl;
    CHECK("a callback that is not the mod's: refused, in the log",
          sdk_lib_playlists("mod-a", 0, on_pl, &ps1) == -1 && strstr(g_last_log, "not inside this mod"));
    g_not_mine = 0;
    CHECK("another mod's id: refused", sdk_lib_playlists("mod-b", 0, on_pl, &ps1) == -1);

    struct tr_seen ts = {0};
    char want0[320], want1[320];
    snprintf(want0, sizeof want0, "12|Track Twelve|Artist A|Album X|8A|120.00|199|%s/sdb1/Contents/a/twelve.mp3", g_media);
    snprintf(want1, sizeof want1, "10|Track Ten|Artist A|Album X|8A|128.00|301|%s/sdb1/Contents/a/ten.mp3", g_media);
    CHECK("a playlist's tracks: read", sdk_lib_tracks("mod-a", 0, 2, on_tr, &ts) == 0 && ts.n == 2);
    CHECK("in playlist order, with artist, album, key, BPM, length and an absolute path",
          ts.n == 2 && !strcmp(ts.line[0], want0) && !strcmp(ts.line[1], want1));
    struct tr_seen all = {0};
    CHECK("playlist 0 = every track", sdk_lib_tracks("mod-a", 0, 0, on_tr, &all) == 0 && all.n == 3);
    CHECK("a track with no album or key gives empty text, not garbage",
          all.n == 3 && strstr(all.line[1], "|Track Eleven|Artist B|||174.00|245|") != NULL);
    struct tr_seen stop = {0, 1};
    CHECK("a callback that returns nonzero stops the listing", sdk_lib_tracks("mod-a", 0, 0, on_tr, &stop) == 0 && stop.n == 1);
    g_cm_in_frame = 1;
    struct tr_seen none = {0};
    CHECK("refused from the control-bus thread", sdk_lib_tracks("mod-a", 0, 2, on_tr, &none) == -1 && none.n == 0);
    g_cm_in_frame = 0;
    g_cm_on_ui = 1;
    struct pl_seen none_ui = {0};
    CHECK("refused on the UI thread (init, tick, paint)", sdk_lib_playlists("mod-a", 0, on_pl, &none_ui) == -1 &&
                                                              sdk_lib_tracks("mod-a", 0, 2, on_tr, &none) == -1 &&
                                                              none_ui.n == 0 && none.n == 0);
    g_cm_on_ui = 0;

    /* the services' own connection cannot write */
    char db[PATH_MAX];
    snprintf(db, sizeof db, "%s/sdb1/PIONEER/rekordbox/exportLibrary.db", g_media);
    void *h = sdk_db_open(db), *st = NULL;
    int wrote = 0;
    if (h && g_sdk_sq.prepare(h, "INSERT INTO playlist VALUES (99, 9, 'x', 0, 0, 0)", -1, &st, NULL) == 0 && st)
        wrote = g_sdk_sq.step(st) == 101 /* SQLITE_DONE */;
    if (st)
        g_sdk_sq.finalize(st);
    if (h)
        g_sdk_sq.close(h);
    struct pl_seen after = {0};
    sdk_lib_playlists("mod-a", 0, on_pl, &after);
    CHECK("the services' connection is read-only: an INSERT through it fails", h && !wrote && after.n == 3);

    /* lib_spool_kick: asks the existing spool service to look, unless a deck PLAYS from the stick */
    snprintf(g_cursrc, sizeof g_cursrc, "%s", g_media);
    char t0[PATH_MAX], t1[PATH_MAX];
    snprintf(t0, sizeof t0, "%s/sdb1/Contents/a.mp3", g_media);
    snprintf(t1, sizeof t1, "%s/sda1/Contents/b.mp3", g_media);
    g_ls_leftover = 0;
    CHECK("spool kick: no deck playing: asked (the service's flag set)",
          sdk_lib_spool_kick("mod-a", 0) == 0 && g_ls_leftover == 1);
    g_ls_leftover = 0;
    g_head[1] = 1000, g_moving[1] = 1;
    deck_src(1, t0);
    CHECK("spool kick: deck 2 plays a track from that USB: refused, nothing set",
          sdk_lib_spool_kick("mod-a", 0) == -1 && g_ls_leftover == 0 && strstr(g_last_log, "deck 2 is playing"));
    deck_src(1, t1);
    CHECK("spool kick: deck 2 plays from the other USB: asked", sdk_lib_spool_kick("mod-a", 0) == 0 && g_ls_leftover == 1);
    g_ls_leftover = 0;
    char sp[PATH_MAX];
    snprintf(sp, sizeof sp, "%s/sda1/PIONEER/azspool", g_media);
    mkdir(sp, 0755);
    strcat(sp, "/dl-1.sql");
    FILE *spf = fopen(sp, "w");
    fclose(spf);
    CHECK("spool kick: refused while the other USB, which a deck plays from, has a spool waiting",
          sdk_lib_spool_kick("mod-a", 0) == -1 && g_ls_leftover == 0 && strstr(g_last_log, "spool waiting"));
    unlink(sp);
    deck_src(1, "");
    CHECK("spool kick: a playing deck whose USB is unknown: refused",
          sdk_lib_spool_kick("mod-a", 0) == -1 && g_ls_leftover == 0 && strstr(g_last_log, "cannot place"));
    g_moving[1] = 0;
    CHECK("spool kick: the same deck stopped: asked", sdk_lib_spool_kick("mod-a", 0) == 0 && g_ls_leftover == 1);
    g_ls_leftover = 0;
    g_cm_on_ui = 1;
    CHECK("spool kick: refused on the UI thread", sdk_lib_spool_kick("mod-a", 0) == -1 && g_ls_leftover == 0);
    g_cm_on_ui = 0;
    CHECK("spool kick: a bad stick number refused", sdk_lib_spool_kick("mod-a", 2) == -1 && g_ls_leftover == 0);

    /* a library past the per-call cap: the first SDK_LIB_ROWS rows, and the call says it was cut */
    {
        char big[PATH_MAX];
        snprintf(big, sizeof big, "%s/sdb1/PIONEER/rekordbox/exportLibrary.db", g_media);
        sqlite3 *db;
        if (sqlite3_open(big, &db) != SQLITE_OK)
            return 2;
        sql(db, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 20001) "
                "INSERT INTO content SELECT 1000 + x, 'Bulk', 7, 5, 3, '/Contents/bulk.mp3', 12000, 100 FROM c");
        sql(db, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 20001) "
                "INSERT INTO playlist SELECT 1000 + x, x, 'Bulk', 0, 0, 1 FROM c");
        sqlite3_close(db);
        struct tr_seen bt = {0};
        struct pl_seen bp = {0};
        int rt = sdk_lib_tracks("mod-a", 0, 0, on_tr, &bt);
        CHECK("over the cap: lib_tracks returns 1 after exactly SDK_LIB_ROWS tracks", rt == 1 && bt.n == SDK_LIB_ROWS);
        int rp = sdk_lib_playlists("mod-a", 0, on_count_pl, &bp);
        CHECK("over the cap: lib_playlists returns 1 after exactly SDK_LIB_ROWS entries",
              rp == 1 && bp.n == SDK_LIB_ROWS);
    }

    /* two libraries in one port (two partitions, or a hub): which one EP147 shows is unknown */
    make_stick("sdd1", "Second");
    plug("sdd1", "2-1", "sdd1");
    struct pl_seen amb = {0};
    CHECK("two libraries in the USB 2 port: -1, never a guess", sdk_lib_playlists("mod-a", 1, on_pl, &amb) == -1 &&
                                                                    amb.n == 0);
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd)) {
    }
    return ut_summary();
}
