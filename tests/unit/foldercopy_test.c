/* SPDX-License-Identifier: GPL-3.0-only */
/* The FOLDER-view copy plan (guest/azmod/folder_copy.h) on a fake file system: names checked
 * before they become paths, litter never copied, nothing overwritten (a taken folder name gets
 * " (n)", a same-size file is skipped, another size gets " (n)" before the extension), folders
 * listed before their contents, and the caps. */
#include <stdio.h>
#include <string.h>
#include "folder_copy.h"
#include "check.h"

/* a fake tree: path -> 1 file (size) / 2 folder; "list" = the direct children, in table order */
struct fent {
    const char *path;
    int type;
    uint64_t size;
};
struct ffs {
    const struct fent *e;
    int n;
};
static int f_stat(void *ctx, const char *path, uint64_t *size) {
    const struct ffs *f = ctx;
    for (int i = 0; i < f->n; i++)
        if (!strcmp(f->e[i].path, path)) {
            *size = f->e[i].size;
            return f->e[i].type;
        }
    return 0;
}
static int f_list(void *ctx, const char *path, int (*each)(void *, const char *), void *arg) {
    const struct ffs *f = ctx;
    size_t pl = strlen(path);
    for (int i = 0; i < f->n; i++) {
        const char *p = f->e[i].path;
        if (strncmp(p, path, pl) || p[pl] != '/' || strchr(p + pl + 1, '/'))
            continue;
        if (each(arg, p + pl + 1))
            return 0;
    }
    return 0;
}

static const struct fent SRC[] = {
    {"Set", 2, 0},
    {"Set/a.mp3", 1, 100},
    {"Set/._a.mp3", 1, 4},
    {"Set/.DS_Store", 1, 6},
    {"Set/b.wav", 1, 200},
    {"Set/half.mp3.azcp", 1, 50},
    {"Set/Inner", 2, 0},
    {"Set/Inner/c.flac", 1, 300},
    {"Loose.mp3", 1, 77},
};
static const struct fent DST[] = {
    {"Set", 2, 0},
    {"Set (1)", 2, 0},
    {"Loose.mp3", 1, 77},
    {"Other.mp3", 1, 10},
    {"In", 2, 0},
};

static struct fc_item g_items[64];

int main(void) {
    ut_suite = "folder copy";
    struct ffs s = {SRC, (int) (sizeof SRC / sizeof SRC[0])}, d = {DST, (int) (sizeof DST / sizeof DST[0])};
    struct fc_fs fs = {&s, f_stat, f_list}, fd = {&d, f_stat, f_list};
    char out[FC_PATH_MAX];

    CHECK("a plain name is a component", fc_name_ok("Track 01.mp3"));
    CHECK("\"..\" is refused", !fc_name_ok(".."));
    CHECK("\".\" is refused", !fc_name_ok("."));
    CHECK("an empty name is refused", !fc_name_ok(""));
    CHECK("a '/' inside is refused", !fc_name_ok("a/b"));
    CHECK("a '\\' inside is refused", !fc_name_ok("a\\b"));
    CHECK("a control character is refused", !fc_name_ok("a\nb"));
    char longn[300];
    memset(longn, 'x', sizeof longn - 1);
    longn[sizeof longn - 1] = 0;
    CHECK("a name past 255 bytes is refused", !fc_name_ok(longn));

    CHECK("AppleDouble ._ files are litter", fc_skip_name("._Track.mp3"));
    CHECK(".DS_Store is litter", fc_skip_name(".DS_Store"));
    CHECK("an unfinished .azcp is never copied", fc_skip_name("x.mp3.azcp"));
    CHECK("a track is not litter", !fc_skip_name("Track.mp3"));
    CHECK("a dotfile that is not litter is copied", !fc_skip_name(".hidden"));

    CHECK("join", !fc_join(out, sizeof out, "A/B", "c.mp3") && !strcmp(out, "A/B/c.mp3"));
    CHECK("join at the top", !fc_join(out, sizeof out, "", "c.mp3") && !strcmp(out, "c.mp3"));
    CHECK("join refuses \"..\"", fc_join(out, sizeof out, "A", "..") < 0);
    char small[8];
    CHECK("join refuses what does not fit", fc_join(small, sizeof small, "abcd", "efgh") < 0);

    CHECK("file alternative keeps the extension",
          !fc_alt_name(out, sizeof out, "Track.mp3", 1, 0) && !strcmp(out, "Track (1).mp3"));
    CHECK("folder alternative has no extension",
          !fc_alt_name(out, sizeof out, "My.Set", 2, 1) && !strcmp(out, "My.Set (2)"));
    CHECK("a dotfile's alternative",
          !fc_alt_name(out, sizeof out, ".hidden", 1, 0) && !strcmp(out, ".hidden (1)"));

    CHECK("a free name lands as is",
          fc_dest(&fd, "", "New.mp3", 0, 5, out, sizeof out) == FC_PUT && !strcmp(out, "New.mp3"));
    CHECK("the same name and size is already there: skipped",
          fc_dest(&fd, "", "Loose.mp3", 0, 77, out, sizeof out) == FC_SKIP);
    CHECK("the same name, another size: \" (1)\"",
          fc_dest(&fd, "", "Loose.mp3", 0, 78, out, sizeof out) == FC_PUT &&
              !strcmp(out, "Loose (1).mp3"));
    CHECK("a taken folder name is never merged: the next free \" (n)\"",
          fc_dest(&fd, "", "Set", 1, 0, out, sizeof out) == FC_PUT && !strcmp(out, "Set (2)"));
    CHECK("into a subfolder", fc_dest(&fd, "In", "Loose.mp3", 0, 77, out, sizeof out) == FC_PUT &&
                                  !strcmp(out, "In/Loose.mp3"));

    struct fc_plan p = {g_items, 0, 64, 0, 0, 0, 0, 0};
    CHECK("a folder plan", fc_plan_make(&fs, &fd, "Set", "", &p) == 0 && !p.err);
    CHECK("  lands as \"Set (2)\" (Set and Set (1) are taken)",
          p.n > 0 && p.items[0].is_dir && !strcmp(p.items[0].dst, "Set (2)"));
    CHECK("  3 files, 600 bytes: litter and .azcp left out", p.files == 3 && p.bytes == 600);
    CHECK("  a folder comes before what is inside it",
          p.n == 5 && p.items[3].is_dir && !strcmp(p.items[3].dst, "Set (2)/Inner") &&
              !strcmp(p.items[4].dst, "Set (2)/Inner/c.flac"));
    CHECK("  sources stay where they are",
          !strcmp(p.items[1].src, "Set/a.mp3") && !strcmp(p.items[4].src, "Set/Inner/c.flac"));

    CHECK("a single file", fc_plan_make(&fs, &fd, "Loose.mp3", "In", &p) == 0 && p.n == 1 &&
                               !strcmp(p.items[0].dst, "In/Loose.mp3") && p.bytes == 77);
    CHECK("a file already there: nothing to do",
          fc_plan_make(&fs, &fd, "Loose.mp3", "", &p) == 0 && p.n == 0 && p.skipped == 1);
    CHECK("a source that is not there is refused",
          fc_plan_make(&fs, &fd, "Gone.mp3", "", &p) < 0 && p.err == FC_E_NAME);

    struct fc_plan tiny = {g_items, 0, 2, 0, 0, 0, 0, 0};
    CHECK("a plan larger than its room stops, never overruns",
          fc_plan_make(&fs, &fd, "Set", "", &tiny) < 0 && tiny.err == FC_E_FULL && tiny.n == 2);

    static const struct fent BAD[] = {{"X", 2, 0}, {"X/..", 1, 1}};
    struct ffs b = {BAD, 2};
    struct fc_fs fb = {&b, f_stat, f_list};
    CHECK("a \"..\" from the stick stops the walk",
          fc_plan_make(&fb, &fd, "X", "", &p) < 0 && p.err == FC_E_NAME);

    static struct fent deep[FC_MAX_DEPTH + 3];
    static char dp[FC_MAX_DEPTH + 3][FC_PATH_MAX];
    for (int i = 0; i < FC_MAX_DEPTH + 3; i++) {
        snprintf(dp[i], sizeof dp[i], "%s%sD", i ? dp[i - 1] : "", i ? "/" : "");
        deep[i] = (struct fent) {dp[i], 2, 0};
    }
    struct ffs dd = {deep, FC_MAX_DEPTH + 3};
    struct fc_fs fdeep = {&dd, f_stat, f_list};
    struct fc_plan pd = {g_items, 0, 64, 0, 0, 0, 0, 0};
    CHECK("folders nested deeper than the cap stop the walk",
          fc_plan_make(&fdeep, &fd, "D", "", &pd) < 0 && pd.err == FC_E_DEPTH);

    const char *lv[] = {"AZTEST", "Sub", "Inner.mp3"};
    CHECK("the selected entry from the level names",
          !fc_rel_from_levels(lv, 3, out, sizeof out) && !strcmp(out, "AZTEST/Sub/Inner.mp3"));
    const char *lbad[] = {"AZTEST", "..", "x"};
    CHECK("a \"..\" level is refused", fc_rel_from_levels(lbad, 3, out, sizeof out) < 0);
    CHECK("no levels: refused", fc_rel_from_levels(lv, 0, out, sizeof out) < 0);
    return ut_summary();
}
