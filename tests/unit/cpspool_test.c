/* SPDX-License-Identifier: GPL-3.0-only */
/* The cross-stick copy's SQL spool (cp_track_sql / cp_build_spool, extracted from
 * guest/azmod/library_edits.c by run.sh), applied to a real SQLite library:
 *   - a playlist pasted onto another stick becomes a new list holding every entry of the
 *     source in order, a track that appears twice included;
 *   - Control: a track pasted onto an existing list that already holds it adds no second row,
 *     while a track it lacks is appended;
 *   - each track's content row exists once on the target. */
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

#define PL_TGT_NAME_MAX 96
#define PL_ATTR_LIST 0
#define PL_ATTR_FOLDER 1
static long long drag_now_ms(void) { return 123456; }
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

static void trk(struct cp_trk *t, const char *path, const char *title) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < CPF_N; i++) t->isnull[i] = 1;
    snprintf(t->f[CPF_PATH], sizeof t->f[CPF_PATH], "%s", path);
    t->isnull[CPF_PATH] = 0;
    snprintf(t->f[CPF_TITLE], sizeof t->f[CPF_TITLE], "%s", title);
    t->isnull[CPF_TITLE] = 0;
}
static sqlite3 *fresh(void) {
    sqlite3 *db = 0;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK || sqlite3_exec(db, SCHEMA, 0, 0, 0) != SQLITE_OK) exit(2);
    return db;
}
static int apply(sqlite3 *db) {
    static char buf[1 << 20];
    int n = cp_build_spool(buf, sizeof buf);
    if (n <= 0) return -1;
    char *err = 0;
    int rc = sqlite3_exec(db, buf, 0, 0, &err);
    if (rc != SQLITE_OK) {
        printf("      sql error: %s\n", err ? err : "?");
        sqlite3_free(err);
    }
    return rc;
}
static void titles(sqlite3 *db, const char *where, char *out, size_t cap) {
    char q[300];
    out[0] = 0;
    snprintf(q, sizeof q, "SELECT c.title FROM playlist_content pc JOIN content c ON c.content_id=pc.content_id "
                          "WHERE pc.playlist_id=(%s) ORDER BY pc.sequenceNo",
             where);
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db, q, -1, &st, 0) != SQLITE_OK) return;
    while (sqlite3_step(st) == SQLITE_ROW) {
        strncat(out, (const char *) sqlite3_column_text(st, 0), cap - strlen(out) - 1);
        strncat(out, ",", cap - strlen(out) - 1);
    }
    sqlite3_finalize(st);
}
static int count(sqlite3 *db, const char *q) {
    sqlite3_stmt *st = 0;
    int n = -1;
    if (sqlite3_prepare_v2(db, q, -1, &st, 0) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

int main(void) {
    ut_suite = "cross-stick spool";
    struct cp_trk t[4];
    char got[400];

    /* a playlist A, B, A pasted next to the target's top-level rows: a new list */
    sqlite3 *db = fresh();
    trk(&t[0], "/Contents/a.mp3", "A");
    trk(&t[1], "/Contents/b.mp3", "B");
    trk(&t[2], "/Contents/a.mp3", "A");
    memset(&g_cp, 0, sizeof g_cp);
    g_cp.dst_attr = PL_ATTR_FOLDER;
    g_cp.dst_pl = 0;
    snprintf(g_cp.src_pl_name, sizeof g_cp.src_pl_name, "Set 1");
    snprintf(g_cp.src_mount, sizeof g_cp.src_mount, "/media/usb/sda");
    g_cp.trk = t;
    g_cp.ntrk = 3;
    CHECK("the spool applies", apply(db) == SQLITE_OK);
    titles(db, "SELECT playlist_id FROM playlist WHERE name='Set 1'", got, sizeof got);
    CHECK("the new list holds every entry of the source in order, the repeat included (A,B,A)", !strcmp(got, "A,B,A,"));
    CHECK("each track's content row once", count(db, "SELECT COUNT(*) FROM content") == 2);
    CHECK("the new list's image_id mark is reset", count(db, "SELECT COUNT(*) FROM playlist WHERE image_id<>0") == 0);
    sqlite3_close(db);

    /* Control: tracks pasted onto an existing list that already holds A */
    db = fresh();
    sqlite3_exec(db, "INSERT INTO content(content_id,title,path) VALUES(1,'A','/Contents/a.mp3');"
                     "INSERT INTO playlist VALUES(5,1,'Mine',0,0,0);"
                     "INSERT INTO playlist_content VALUES(5,1,1);",
                 0, 0, 0);
    trk(&t[0], "/Contents/a.mp3", "A");
    trk(&t[1], "/Contents/b.mp3", "B");
    memset(&g_cp, 0, sizeof g_cp);
    g_cp.dst_attr = PL_ATTR_LIST;
    g_cp.dst_pl = 5;
    snprintf(g_cp.dst_name, sizeof g_cp.dst_name, "Mine");
    snprintf(g_cp.src_mount, sizeof g_cp.src_mount, "/media/usb/sda");
    g_cp.trk = t;
    g_cp.ntrk = 2;
    CHECK("CONTROL: the spool applies onto an existing list", apply(db) == SQLITE_OK);
    titles(db, "SELECT 5", got, sizeof got);
    CHECK("CONTROL: an existing list gains only the track it lacked (A,B)", !strcmp(got, "A,B,"));
    sqlite3_close(db);

    /* a source title holding control bytes: the spool is plain lines (the applier rejects a
     * line holding 0x01, its own line mark, so one passed through would refuse the paste) */
    db = fresh();
    trk(&t[0], "/Contents/c.mp3", "C\x01"
                                  "D\x02"
                                  "E\tF");
    memset(&g_cp, 0, sizeof g_cp);
    g_cp.dst_attr = PL_ATTR_FOLDER;
    snprintf(g_cp.src_pl_name, sizeof g_cp.src_pl_name, "Set\x01"
                                                        "2");
    snprintf(g_cp.src_mount, sizeof g_cp.src_mount, "/media/usb/sda");
    g_cp.trk = t;
    g_cp.ntrk = 1;
    {
        static char sp[1 << 16];
        int n = cp_build_spool(sp, sizeof sp), ctl = 0;
        for (int i = 0; i < n; i++)
            ctl += (unsigned char) sp[i] < 0x20 && sp[i] != '\n' && sp[i] != '\t';
        CHECK("control bytes in a title or a list name become spaces in the spool", n > 0 && ctl == 0);
    }
    CHECK("that spool applies", apply(db) == SQLITE_OK);
    CHECK("the title arrives with spaces, a tab kept",
          count(db, "SELECT COUNT(*) FROM content WHERE title='C D E\tF'") == 1);
    CHECK("the list name arrives with a space", count(db, "SELECT COUNT(*) FROM playlist WHERE name='Set 2'") == 1);
    sqlite3_close(db);
    return ut_summary();
}
