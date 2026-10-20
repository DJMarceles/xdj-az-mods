/* SPDX-License-Identifier: GPL-3.0-only */
/* A FOLDER pasted onto another stick (cp_build_spool / cp_folder_sql, extracted from
 * guest/azmod/library_edits.c by run.sh), applied to a real SQLite library:
 *   - the new folder holds the same tree as the source: names, kinds, order, nesting, and
 *     every list's tracks in order, empty lists and a track in two lists included;
 *   - the new folder takes " (1)" when its name is taken at the destination;
 *   - new playlist ids start at 16385, and no image_id mark is left behind;
 *   - Control: a destination folder that is gone writes nothing at all.
 * And the PASTE router's "onto itself" test (pl_paste_is_self, from playlist_actions.c):
 * the same id on the other stick is a different row. */
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
#include PASTE_INC

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

static void trk(struct cp_trk *t, const char *path, const char *title, int node) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < CPF_N; i++) t->isnull[i] = 1;
    snprintf(t->f[CPF_PATH], sizeof t->f[CPF_PATH], "%s", path);
    t->isnull[CPF_PATH] = 0;
    snprintf(t->f[CPF_TITLE], sizeof t->f[CPF_TITLE], "%s", title);
    t->isnull[CPF_TITLE] = 0;
    t->node = node;
}
static void node(struct cp_node *n, const char *name, int parent, int folder, int seq) {
    memset(n, 0, sizeof *n);
    snprintf(n->name, sizeof n->name, "%s", name);
    n->parent = parent;
    n->folder = folder;
    n->seq = seq;
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
static int count(sqlite3 *db, const char *q) {
    sqlite3_stmt *st = 0;
    int n = -1;
    if (sqlite3_prepare_v2(db, q, -1, &st, 0) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}
/* the tree under folder `root`, depth-first in display order: "name/kind[titles]" per row */
static void tree(sqlite3 *db, int root, char *out, size_t cap) {
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db, "SELECT playlist_id, name, attribute FROM playlist WHERE playlist_id_parent=?1 "
                               "ORDER BY sequenceNo, playlist_id",
                           -1, &st, 0) != SQLITE_OK)
        return;
    sqlite3_bind_int(st, 1, root);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0), folder = sqlite3_column_int(st, 2);
        char row[300];
        snprintf(row, sizeof row, "%s/%s[", (const char *) sqlite3_column_text(st, 1), folder ? "F" : "L");
        strncat(out, row, cap - strlen(out) - 1);
        if (!folder) {
            sqlite3_stmt *t = 0;
            sqlite3_prepare_v2(db, "SELECT c.title FROM playlist_content pc JOIN content c USING (content_id) "
                                   "WHERE pc.playlist_id=?1 ORDER BY pc.sequenceNo",
                               -1, &t, 0);
            sqlite3_bind_int(t, 1, id);
            while (sqlite3_step(t) == SQLITE_ROW) strncat(out, (const char *) sqlite3_column_text(t, 0), cap - strlen(out) - 1);
            sqlite3_finalize(t);
        }
        strncat(out, "]", cap - strlen(out) - 1);
        if (folder) {
            strncat(out, "{", cap - strlen(out) - 1);
            tree(db, id, out, cap);
            strncat(out, "}", cap - strlen(out) - 1);
        }
        strncat(out, " ", cap - strlen(out) - 1);
    }
    sqlite3_finalize(st);
}
/* Set {Warmup[A B], Sub{Deep[B C]}, Empty[]}: entry 0 the folder, then breadth-first */
static struct cp_node N[5];
static struct cp_trk T[4];
static void source(void) {
    node(&N[0], "Set", -1, 1, 0);
    node(&N[1], "Warmup", 0, 0, 1);
    node(&N[2], "Sub", 0, 1, 2);
    node(&N[3], "Empty", 0, 0, 3);
    node(&N[4], "Deep", 2, 0, 1);
    trk(&T[0], "/Contents/a.mp3", "A", 1);
    trk(&T[1], "/Contents/b.mp3", "B", 1);
    trk(&T[2], "/Contents/b.mp3", "B", 4);
    trk(&T[3], "/Contents/c.mp3", "C", 4);
    memset(&g_cp, 0, sizeof g_cp);
    g_cp.kind = CPK_FOLDER;
    g_cp.dst_attr = PL_ATTR_FOLDER;
    snprintf(g_cp.src_pl_name, sizeof g_cp.src_pl_name, "Set");
    snprintf(g_cp.src_mount, sizeof g_cp.src_mount, "/media/usb/sda");
    g_cp.node = N;
    g_cp.nnode = 5;
    g_cp.trk = T;
    g_cp.ntrk = 4;
}
#define WANT "Warmup/L[AB] Sub/F[]{Deep/L[BC] } Empty/L[] "

int main(void) {
    ut_suite = "cross-stick folder copy";
    char got[2000];

    /* onto the top level of a stick that already has a list "Set" and small ids */
    sqlite3 *db = fresh();
    sqlite3_exec(db, "INSERT INTO playlist VALUES(1,1,'Set',0,0,0);", 0, 0, 0);
    source();
    g_cp.dst_pl = 0;
    CHECK("the spool applies", apply(db) == SQLITE_OK);
    int root = count(db, "SELECT playlist_id FROM playlist WHERE name='Set (1)' AND attribute=1 AND playlist_id_parent=0");
    CHECK("a new top-level folder named \"Set (1)\" (the name is taken)", root > 0);
    CHECK("it comes after the existing top-level row", count(db, "SELECT sequenceNo FROM playlist WHERE name='Set (1)'") == 2);
    got[0] = 0;
    tree(db, root, got, sizeof got);
    if (strcmp(got, WANT)) printf("      tree: %s\n", got);
    CHECK("the same tree: names, kinds, order, nesting, every list's tracks in order", !strcmp(got, WANT));
    CHECK("each track's content row once", count(db, "SELECT COUNT(*) FROM content") == 3);
    CHECK("every new playlist id is 16385 or more", count(db, "SELECT COUNT(*) FROM playlist WHERE playlist_id<>1 AND playlist_id<16385") == 0);
    CHECK("no image_id mark is left", count(db, "SELECT COUNT(*) FROM playlist WHERE image_id<>0") == 0);
    CHECK("sibling numbers are 1..N inside the copy",
          count(db, "SELECT COUNT(*) FROM playlist p WHERE p.playlist_id_parent>1 AND p.sequenceNo NOT BETWEEN 1 AND "
                    "(SELECT COUNT(*) FROM playlist q WHERE q.playlist_id_parent=p.playlist_id_parent)") == 0);
    sqlite3_close(db);

    /* into an existing folder, picked by id and name */
    db = fresh();
    sqlite3_exec(db, "INSERT INTO playlist VALUES(20000,1,'Gigs',0,1,0);", 0, 0, 0);
    source();
    g_cp.dst_pl = 20000;
    snprintf(g_cp.dst_name, sizeof g_cp.dst_name, "Gigs");
    CHECK("the spool applies into a folder", apply(db) == SQLITE_OK);
    root = count(db, "SELECT playlist_id FROM playlist WHERE name='Set' AND playlist_id_parent=20000 AND attribute=1");
    CHECK("the copy is a folder inside it, under its own name", root > 20000);
    got[0] = 0;
    tree(db, root, got, sizeof got);
    CHECK("with the same tree", !strcmp(got, WANT));
    sqlite3_close(db);

    /* Control: the destination folder was renamed since PASTE: nothing may be written */
    db = fresh();
    sqlite3_exec(db, "INSERT INTO playlist VALUES(20000,1,'Renamed',0,1,0);", 0, 0, 0);
    source();
    g_cp.dst_pl = 20000;
    snprintf(g_cp.dst_name, sizeof g_cp.dst_name, "Gigs");
    CHECK("CONTROL: the spool still applies (as a no-op)", apply(db) == SQLITE_OK);
    CHECK("CONTROL: no playlist row is written", count(db, "SELECT COUNT(*) FROM playlist") == 1);
    CHECK("CONTROL: no playlist_content row is written", count(db, "SELECT COUNT(*) FROM playlist_content") == 0);
    sqlite3_close(db);

    CHECK("PASTE onto the copied row on the same stick is onto itself", pl_paste_is_self(1, 16420, 16420));
    CHECK("the same id on the other stick is a different row", !pl_paste_is_self(0, 16420, 16420));
    CHECK("another row on the same stick is not itself", !pl_paste_is_self(1, 16421, 16420));
    return ut_summary();
}
