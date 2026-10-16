/* SPDX-License-Identifier: GPL-3.0-only */
/* A pasted track's artwork on a stick that already has the track (cp_track_sql,
 * cp_build_spool and ls_art_fix, extracted from library_edits.c; ps_file_ok from
 * pdb_sync.c), on a real SQLite library and a stick directory with real files:
 *   - the target's own row is reused (one content row per path), and when its art is a
 *     missing file, unset, or names no image row, it is pointed at the art the copy wrote;
 *   - a row whose own art file is on the stick keeps it, also beside a broken row with the
 *     same path;
 *   - art the copy did not manage to write is never pointed at;
 *   - a track new to the target gets the copied art, as before.
 * Controls: the spool without its art lines (a reused row keeps its broken art), and
 * ls_art_fix replacing art that is on the stick. */
#define _GNU_SOURCE /* RTLD_DEFAULT (glibc) */
#include <dlfcn.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

#define PL_TGT_NAME_MAX 96
#define PL_ATTR_LIST 0
#define PL_ATTR_FOLDER 1
static long long drag_now_ms(void) { return 123456; }
static struct {
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*bind_int64)(void *, int, long long);
    int (*step)(void *);
    long long (*column_int64)(void *, int);
    const unsigned char *(*column_text)(void *, int);
    int (*finalize)(void *);
} g_sq;
#include CP_INC

static const char *SCHEMA =
    "CREATE TABLE artist(artist_id INTEGER PRIMARY KEY, name TEXT);"
    "CREATE TABLE genre(genre_id INTEGER PRIMARY KEY, name TEXT);"
    "CREATE TABLE label(label_id INTEGER PRIMARY KEY, name TEXT);"
    "CREATE TABLE key(key_id INTEGER PRIMARY KEY, name TEXT);"
    "CREATE TABLE image(image_id INTEGER PRIMARY KEY, path TEXT);"
    "CREATE TABLE album(album_id INTEGER PRIMARY KEY, name TEXT, artist_id, image_id, isComplation);"
    "CREATE TABLE content(content_id INTEGER PRIMARY KEY, title,titleForSearch,subtitle,bpmx100,length,"
    "trackNo,discNo,artist_id_artist,artist_id_remixer,artist_id_originalArtist,artist_id_composer,"
    "artist_id_lyricist,album_id,genre_id,label_id,key_id,color_id,image_id,djComment,rating,releaseYear,"
    "releaseDate,dateCreated,dateAdded,path,fileName,fileSize,fileType,bitrate,bitDepth,samplingRate,isrc,"
    "isHotCueAutoLoadOn,analysisDataFilePath,analysedBits,masterDbId,masterContentId,contentLink);"
    "CREATE TABLE playlist(playlist_id INTEGER PRIMARY KEY, sequenceNo, name, image_id, attribute,"
    " playlist_id_parent);"
    "CREATE TABLE playlist_content(playlist_id, content_id, sequenceNo);";

/* the target stick: its rows, and which of their art files exist */
static const char *TARGET =
    "INSERT INTO image VALUES(10,'/PIONEER/Artwork/00353/b10.jpg');" /* file gone */
    "INSERT INTO image VALUES(11,'/PIONEER/Artwork/00001/b11.jpg');" /* file there */
    "INSERT INTO image VALUES(12,'/PIONEER/Artwork/00001/b12.jpg');" /* file there */
    "INSERT INTO image VALUES(13,'/PIONEER/Artwork/00353/b13.jpg');" /* file gone */
    "INSERT INTO content(content_id,title,path,image_id) VALUES(1,'A','/Contents/a.mp3',10);"
    "INSERT INTO content(content_id,title,path,image_id) VALUES(2,'B','/Contents/b.mp3',11);"
    "INSERT INTO content(content_id,title,path,image_id) VALUES(3,'C','/Contents/c.mp3',0);"
    "INSERT INTO content(content_id,title,path,image_id) VALUES(4,'D','/Contents/d.mp3',99);"
    "INSERT INTO content(content_id,title,path,image_id) VALUES(5,'A','/Contents/a.mp3',12);"
    "INSERT INTO content(content_id,title,path,image_id) VALUES(6,'F','/Contents/f.mp3',13);"
    "INSERT INTO content(content_id,title,path,image_id) VALUES(7,'G','/Contents/g;h.mp3',NULL);";

static char g_root[600];
static void put_file(const char *rel) {
    char p[800], d[800];
    snprintf(p, sizeof p, "%s%s", g_root, rel);
    for (char *s = strchr(p + strlen(g_root) + 1, '/'); s; s = strchr(s + 1, '/')) {
        snprintf(d, sizeof d, "%.*s", (int) (s - p), p);
        mkdir(d, 0755);
    }
    FILE *f = fopen(p, "wb");
    if (!f || fputs("jpg", f) < 0 || fclose(f)) exit(2);
}
static void trk(struct cp_trk *t, const char *path, const char *title, const char *img) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < CPF_N; i++) t->isnull[i] = 1;
    snprintf(t->f[CPF_PATH], sizeof t->f[CPF_PATH], "%s", path);
    t->isnull[CPF_PATH] = 0;
    snprintf(t->f[CPF_TITLE], sizeof t->f[CPF_TITLE], "%s", title);
    t->isnull[CPF_TITLE] = 0;
    snprintf(t->img_dst, sizeof t->img_dst, "%s", img ? img : "");
}
static int count(sqlite3 *db, const char *q) {
    sqlite3_stmt *st = 0;
    int n = -1;
    if (sqlite3_prepare_v2(db, q, -1, &st, 0) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}
static void art_of(sqlite3 *db, int cid, char *out, size_t cap) {
    char q[200];
    sqlite3_stmt *st = 0;
    snprintf(q, sizeof q, "SELECT i.path FROM content c LEFT JOIN image i ON i.image_id=c.image_id WHERE c.content_id=%d", cid);
    out[0] = 0;
    if (sqlite3_prepare_v2(db, q, -1, &st, 0) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0))
        snprintf(out, cap, "%s", (const char *) sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
}

int main(void) {
    ut_suite = "cross-stick spool: artwork";
    *(void **) &g_sq.prepare = (void *) sqlite3_prepare_v2;
    *(void **) &g_sq.bind_int64 = (void *) sqlite3_bind_int64;
    *(void **) &g_sq.step = (void *) sqlite3_step;
    *(void **) &g_sq.column_int64 = (void *) sqlite3_column_int64;
    *(void **) &g_sq.column_text = (void *) sqlite3_column_text;
    *(void **) &g_sq.finalize = (void *) sqlite3_finalize;
    const char *tmp = getenv("TMPDIR");
    snprintf(g_root, sizeof g_root, "%s/cpart.XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    if (!mkdtemp(g_root)) return 2;
    put_file("/PIONEER/Artwork/00001/b11.jpg");
    put_file("/PIONEER/Artwork/00001/b12.jpg");
    put_file("/PIONEER/Artwork/azcp/0102_b1.jpg");
    put_file("/PIONEER/Artwork/azcp/0102_b2.jpg");
    put_file("/PIONEER/Artwork/azcp/0102_b3.jpg");
    put_file("/PIONEER/Artwork/azcp/0102_b4.jpg");
    put_file("/PIONEER/Artwork/azcp/0102_b5.jpg");
    put_file("/PIONEER/Artwork/azcp/0102_b7.jpg");
    /* 0102_b6.jpg is not written: the copy of F's art failed */

    sqlite3 *db = 0;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK || sqlite3_exec(db, SCHEMA, 0, 0, 0) != SQLITE_OK ||
        sqlite3_exec(db, TARGET, 0, 0, 0) != SQLITE_OK)
        return 2;
    struct cp_trk t[7];
    trk(&t[0], "/Contents/a.mp3", "A", "/PIONEER/Artwork/azcp/0102_b1.jpg");
    trk(&t[1], "/Contents/b.mp3", "B", "/PIONEER/Artwork/azcp/0102_b2.jpg");
    trk(&t[2], "/Contents/c.mp3", "C", "/PIONEER/Artwork/azcp/0102_b3.jpg");
    trk(&t[3], "/Contents/d.mp3", "D", "/PIONEER/Artwork/azcp/0102_b4.jpg");
    trk(&t[4], "/Contents/e.mp3", "E", "/PIONEER/Artwork/azcp/0102_b5.jpg");
    trk(&t[5], "/Contents/f.mp3", "F", "/PIONEER/Artwork/azcp/0102_b6.jpg");
    trk(&t[6], "/Contents/g;h.mp3", "G", "/PIONEER/Artwork/azcp/0102_b7.jpg");
    memset(&g_cp, 0, sizeof g_cp);
    g_cp.dst_attr = PL_ATTR_FOLDER;
    g_cp.dst_pl = 0;
    snprintf(g_cp.src_pl_name, sizeof g_cp.src_pl_name, "Party");
    snprintf(g_cp.src_mount, sizeof g_cp.src_mount, "/media/usb/sda1");
    g_cp.trk = t;
    g_cp.ntrk = 7;
    static char buf[1 << 20];
    int n = cp_build_spool(buf, sizeof buf);
    CHECK("the spool builds", n > 0);
    char *err = 0;
    int rc = sqlite3_exec(db, buf, 0, 0, &err);
    if (rc != SQLITE_OK) printf("      sql error: %s\n", err ? err : "?");
    sqlite3_free(err);
    CHECK("the spool applies", rc == SQLITE_OK);
    int fixed = ls_art_fix(db, g_root, buf, (size_t) n, stdout);
    CHECK("ls_art_fix runs", fixed >= 0);
    CHECK("the target's rows are reused: one new content row (E)", count(db, "SELECT COUNT(*) FROM content") == 8);
    CHECK("the new list holds all 7 tracks",
          count(db, "SELECT COUNT(*) FROM playlist_content pc JOIN playlist p USING(playlist_id) WHERE p.name='Party'") == 7);
    char a[300];
    art_of(db, 1, a, sizeof a);
    CHECK("a reused row whose art file is gone shows the copied art", !strcmp(a, "/PIONEER/Artwork/azcp/0102_b1.jpg"));
    art_of(db, 2, a, sizeof a);
    CHECK("a reused row whose own art is on the stick keeps it", !strcmp(a, "/PIONEER/Artwork/00001/b11.jpg"));
    art_of(db, 3, a, sizeof a);
    CHECK("a reused row with image_id 0 shows the copied art", !strcmp(a, "/PIONEER/Artwork/azcp/0102_b3.jpg"));
    art_of(db, 4, a, sizeof a);
    CHECK("a reused row naming no image row shows the copied art", !strcmp(a, "/PIONEER/Artwork/azcp/0102_b4.jpg"));
    art_of(db, 5, a, sizeof a);
    CHECK("a second row with the same path keeps its own art that is on the stick", !strcmp(a, "/PIONEER/Artwork/00001/b12.jpg"));
    art_of(db, 6, a, sizeof a);
    CHECK("art the copy did not write is never pointed at", !strcmp(a, "/PIONEER/Artwork/00353/b13.jpg"));
    art_of(db, 7, a, sizeof a);
    CHECK("a path with ';' (char(59) in the SQL) is matched", !strcmp(a, "/PIONEER/Artwork/azcp/0102_b7.jpg"));
    CHECK("the track new to the stick gets the copied art",
          count(db, "SELECT COUNT(*) FROM content c JOIN image i ON i.image_id=c.image_id WHERE c.path='/Contents/e.mp3' "
                    "AND i.path='/PIONEER/Artwork/azcp/0102_b5.jpg'") == 1);
    CHECK("rows changed = 4", fixed == 4);
    CHECK("applied again: nothing changes", ls_art_fix(db, g_root, buf, (size_t) n, 0) == 0);
    /* lines as ls_apply_file leaves them (every line end a NUL) read the same */
    sqlite3_exec(db, "UPDATE content SET image_id=10 WHERE content_id=1", 0, 0, 0);
    for (int i = 0; i < n; i++)
        if (buf[i] == '\n') buf[i] = 0;
    CHECK("NUL-ended lines (ls_apply_file's buffer) are read too", ls_art_fix(db, g_root, buf, (size_t) n, 0) == 1);
    sqlite3_close(db);
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
    if (system(cmd)) return 2;
    return ut_summary();
}
