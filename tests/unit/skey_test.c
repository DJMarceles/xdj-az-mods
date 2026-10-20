/* SPDX-License-Identifier: GPL-3.0-only */
/* Streaming keys in Camelot (guest/azmod/streaming_key.c), against a model of EP147's key text
 * function with its calling convention (the key in x0, the notation flag in w1, the string
 * returned through x8): with the Camelot Keys row off, the caller's flag passes through as
 * stock; with it on, every valid key id comes back in Camelot whatever the caller asked, into
 * the caller's own string; id 0 and ids past the table keep the caller's flag; the key pointer
 * reaches the original unchanged; the kill switch installs nothing; the self-test reports every
 * id, again after the row changes; and calls on several threads while the row flips all get a
 * whole result in one notation.
 * SK_SRC = the file under test. */
#include "check.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    AZF_CAMELOT_KEYS = 7
};
static int g_row; /* the Camelot Keys row (__atomic only) */
static int az_value(int f) { return f == AZF_CAMELOT_KEYS && __atomic_load_n(&g_row, __ATOMIC_RELAXED); }
static void set_row(int on) { __atomic_store_n(&g_row, on, __ATOMIC_RELAXED); }

static char g_off[64];
#define SK_OFF_PATH g_off
static uintptr_t g_entry; /* EP147's entry as callers reach it: the hook once installed */
#define EP_d34790 g_entry

static char g_logpath[64];
static int g_log_on;
static FILE *azlog_open(const char *p, const char *m) {
    (void) p;
    return g_log_on ? fopen(g_logpath, m) : 0;
}
static int g_installs;
static uintptr_t g_install_at;
static void *g_install_hook;
static void *g_fake_entry;
static int azm_install_hook(uintptr_t f, void *h, void **o) {
    g_installs++;
    g_install_at = f;
    g_install_hook = h;
    *o = g_fake_entry;
    return 1;
}

#include SK_SRC

#ifndef SK_HAVE_HOOK
/* the hook is AArch64 assembly: on another host this suite has nothing to run */
int main(void) {
    printf("SKIP  streaming key: AArch64 only\n");
    return 0;
}
#else

/* EP147's key text function, modeled: the table's Camelot and classic texts */
static const char *const k_camelot[SK_IDS] = {"1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B",
                                              "5A", "5B", "6A", "6B", "7A", "7B", "8A", "8B",
                                              "9A", "9B", "10A", "10B", "11A", "11B", "12A", "12B"};
static const char *const k_classic[SK_IDS] = {"Abm", "B", "Ebm", "F#", "Bbm", "Db", "Fm", "Ab",
                                              "Cm", "Eb", "Gm", "Bb", "Dm", "F", "Am", "C",
                                              "Em", "G", "Bm", "D", "F#m", "A", "Dbm", "E"};
static __thread unsigned t_flag_seen;
static __thread const uint8_t *t_key_seen;
__attribute__((visibility("hidden"))) void fake_impl(struct sk_u16s *out, const uint8_t *key, unsigned flag) {
    t_flag_seen = flag;
    t_key_seen = key;
    const char *t = "";
    if (*key >= 1 && *key <= SK_IDS)
        t = flag == 1 ? k_camelot[*key - 1] : k_classic[*key - 1];
    else if (*key > SK_IDS)
        t = "??"; /* past the table: what EP147 would read there is not ours to define */
    out->p = out->buf;
    out->n = strlen(t);
    for (size_t i = 0; i <= out->n; i++)
        out->buf[i] = (uint16_t) (unsigned char) t[i];
}
/* the calling convention: x0 key, w1 flag, x8 the result */
__attribute__((visibility("hidden"))) void fake_keytext(void);
__asm__(".text\n"
        ".p2align 2\n" SK_FN(fake_keytext) "    and w2, w1, #0xff\n"
                                           "    mov x1, x0\n"
                                           "    mov x0, x8\n"
#ifdef __APPLE__
                                           "    b _fake_impl\n"
#else
                                           "    b fake_impl\n"
#endif
        SK_END(fake_keytext));

/* a call as EP147's callers make it, through whatever the entry is now */
static int call(unsigned id, unsigned flag, char *text, size_t cap, struct sk_u16s *res) {
    uint8_t key = (uint8_t) id;
    struct sk_u16s r;
    memset(&r, 0x5a, sizeof r);
    sk_call((void *) g_entry, &r, &key, flag);
    if (res)
        *res = r;
    sk_ascii(&r, text, cap);
    return r.p == r.buf && t_key_seen == &key; /* written in place, key passed through */
}

static void *caller(void *arg) {
    unsigned base = (unsigned) (uintptr_t) arg;
    int ok = 1;
    for (int k = 0; k < 20000; k++) {
        unsigned id = 1 + (base + (unsigned) k) % SK_IDS;
        char t[8];
        ok &= call(id, 0, t, sizeof t, 0) &&
              (strcmp(t, k_camelot[id - 1]) == 0 || strcmp(t, k_classic[id - 1]) == 0);
    }
    return (void *) (uintptr_t) ok;
}

int main(void) {
    ut_suite = "streaming key";
    snprintf(g_off, sizeof g_off, "/tmp/skey-test-%d.off", (int) getpid());
    snprintf(g_logpath, sizeof g_logpath, "/tmp/skey-test-%d.log", (int) getpid());
    g_fake_entry = (void *) fake_keytext;
    char t[8];

    /* the unhooked function, as a control for the model itself */
    g_entry = (uintptr_t) fake_keytext;
    call(15, 0, t, sizeof t, 0);
    CHECK("unhooked, a classic request reads classic", strcmp(t, "Am") == 0);

    unlink(g_off);
    install_streaming_key();
    CHECK("installed once, on EP147's key text function",
          g_installs == 1 && g_install_at == g_entry && g_install_hook == (void *) sk_keytext_hook &&
              g_sk_orig == (void *) fake_keytext);
    g_entry = (uintptr_t) sk_keytext_hook;

    /* row off (the default): EP147's flag passes through */
    int all = 1, inplace = 1;
    for (unsigned id = 1; id <= SK_IDS; id++) {
        inplace &= call(id, 0, t, sizeof t, 0);
        all &= strcmp(t, k_classic[id - 1]) == 0 && t_flag_seen == 0;
    }
    CHECK("row off: a classic request stays classic for every key id", all && inplace);
    call(15, 1, t, sizeof t, 0);
    CHECK("row off: a Camelot request stays Camelot", strcmp(t, "8A") == 0 && t_flag_seen == 1);

    set_row(1);
    all = 1;
    for (unsigned id = 1; id <= SK_IDS; id++) {
        inplace &= call(id, 0, t, sizeof t, 0);
        all &= strcmp(t, k_camelot[id - 1]) == 0 && t_flag_seen == 1;
    }
    CHECK("row on: a classic request reads Camelot for every key id", all);
    CHECK("the string is written in the caller's place and the key reaches the original", inplace);
    all = 1;
    for (unsigned id = 1; id <= SK_IDS; id++) {
        call(id, 1, t, sizeof t, 0);
        all &= strcmp(t, k_camelot[id - 1]) == 0;
    }
    CHECK("a Camelot request still reads Camelot", all);
    call(0, 0, t, sizeof t, 0);
    CHECK("id 0 keeps the caller's flag (and stays empty)", t_flag_seen == 0 && t[0] == 0);
    call(25, 0, t, sizeof t, 0);
    int past0 = t_flag_seen == 0;
    call(255, 1, t, sizeof t, 0);
    CHECK("an id past the table keeps the caller's flag", past0 && t_flag_seen == 1);

    /* the self-test, logs on: every id, all showing their Camelot text; again when the row
     * goes off, all showing their classic text */
    g_log_on = 1;
    unlink(g_logpath);
    sk_tick();
    sk_tick(); /* once per row value */
    set_row(0);
    sk_tick();
    sk_tick();
    FILE *f = fopen(g_logpath, "r");
    int lines = 0, on = 0, off = 0, am = 0, amoff = 0;
    char line[160];
    while (f && fgets(line, sizeof line, f)) {
        lines += strstr(line, "key ") != 0;
        on += strstr(line, "self-test, row on: 25/25 ids show their Camelot text") != 0;
        off += strstr(line, "self-test, row off: 25/25 ids show their classic text") != 0;
        am += strstr(line, "shown \"8A\"  classic \"Am\"  camelot \"8A\"") != 0;
        amoff += strstr(line, "shown \"Am\"  classic \"Am\"  camelot \"8A\"") != 0;
    }
    if (f)
        fclose(f);
    CHECK("the self-test writes every id, once per row value", lines == 50 && on == 1 && off == 1);
    CHECK("the self-test's lines name the shown, classic and Camelot text", am == 1 && amoff == 1);
    g_log_on = 0;

    pthread_t th[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&th[i], 0, caller, (void *) (uintptr_t) (i * 5));
    for (int k = 0; k < 2000; k++)
        set_row(k & 1); /* the row flipped from the menu while lists are built */
    int ok = 1;
    for (int i = 0; i < 4; i++) {
        void *r;
        pthread_join(th[i], &r);
        ok &= r != 0;
    }
    CHECK("calls on four threads while the row flips each get a whole key text", ok);

    /* the kill switch: no hook at all */
    FILE *o = fopen(g_off, "w");
    if (o)
        fclose(o);
    g_installs = 0;
    install_streaming_key();
    CHECK("with the kill switch nothing is installed", g_installs == 0);

    unlink(g_off);
    unlink(g_logpath);
    return ut_summary();
}
#endif
