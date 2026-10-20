/* SPDX-License-Identifier: GPL-3.0-only */
/* Remember Font Size (guest/azmod/font_size.c), against a model of gui::FontManager and the
 * Browse buttons' handler: a size the font size button sets is saved, once per new size, and
 * only from that button on a real FontManager; the saved size is
 * set on the registered FontManager right after its ctor (and on no other instance), through
 * the original setter so it is not written back; a damaged or out-of-range file and the row
 * kill switch apply nothing; and sizes set on two threads at once leave a valid file.
 * FS_SRC = the file under test. */
#include "check.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char g_path[64];
#define FS_PATH g_path

static char g_off[64];
#define FS_OFF_PATH g_off
/* the azmod helpers font_size.c uses */
static FILE *azlog_open(const char *p, const char *m) {
    (void) p;
    (void) m;
    return 0;
}
static int g_writes;
static int settings_write(const char *path, const char *buf, size_t len, int durable) {
    (void) durable;
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    __atomic_add_fetch(&g_writes, 1, __ATOMIC_RELAXED);
    return 0;
}
static int own_q(uintptr_t a, uintptr_t *out) {
    *out = *(uintptr_t *) a;
    return 1;
}
static int azm_install_hook(uintptr_t f, void *h, void **o) {
    (void) f;
    (void) h;
    (void) o;
    return 1;
}

/* gui::FontManager, modeled: the setter refuses >= 3 (with an assertion) and stores at +0x44 */
static int g_sets;
static void fake_set(long fm, int size) { /* every call counts: a refused size asserts in EP147 */
    __atomic_add_fetch(&g_sets, 1, __ATOMIC_RELAXED);
    if (size > 2)
        return;
    __atomic_store_n((int *) (fm + 0x44), size, __ATOMIC_RELEASE);
}
static int fake_get(long fm) { return __atomic_load_n((int *) (fm + 0x44), __ATOMIC_ACQUIRE); }
static uintptr_t g_fm_vt[4], g_other_vt[4];
static void fake_ctor(long fm) {
    *(uintptr_t *) fm = (uintptr_t) g_fm_vt;
    *(int *) (fm + 0x44) = 1;
}
/* the Browse buttons' handler: button 1 cycles the view's FontManager to the next size */
static void fake_buttons(long view, int button) {
    if (button != 1)
        return;
    long fm = *(long *) (view + 0x1540);
    fake_set(fm, (fake_get(fm) + 1) % 3);
}
#define REG_RET 0x2012820u
#define EP_1f4ac90 ((uintptr_t) fake_ctor)
#define EP_1f4a3f0 ((uintptr_t) fake_set)
#define EP_1f4a590 ((uintptr_t) fake_get)
#define EP_2dccc70 ((uintptr_t) g_fm_vt)
#define EP_19ab418 ((uintptr_t) fake_buttons)
/* the status area controller: its font size selector, moved by FUN_01c70fd0 */
static uintptr_t g_bar_vt[4];
static int g_bar_sel = -1, g_bar_calls;
static void fake_bar_select(long bar, int size) {
    (void) bar;
    g_bar_calls++;
    g_bar_sel = size;
}
static void fake_bar_ctor(long bar, long a1, long a2, long a3) {
    (void) a1;
    (void) a2;
    (void) a3;
    *(uintptr_t *) bar = (uintptr_t) g_bar_vt;
    g_bar_sel = 1;
}
static void bar_noop(long bar, long a1, long a2, long a3) {
    (void) bar;
    (void) a1;
    (void) a2;
    (void) a3;
}
#define EP_1c71030 ((uintptr_t) fake_bar_ctor)
#define EP_2c52ff0 ((uintptr_t) g_bar_vt)
#define EP_1c70fd0 ((uintptr_t) fake_bar_select)
#define EP_201281c ((uintptr_t) REG_RET)
#include FS_SRC

static _Alignas(16) unsigned char g_fm[0x60];
static _Alignas(16) unsigned char g_view[0x1600];
static _Alignas(16) unsigned char g_bar[0x40];
static void build_bar(void) {
    g_bar_calls = 0;
    fs_bar_hook((long) g_bar, 0, 0, 0);
}
static void fresh(const char *file) {
    __atomic_store_n(&g_fs_saved, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_fs_fm, 0, __ATOMIC_RELAXED);
    g_writes = g_sets = 0;
    unlink(g_off);
    __atomic_store_n(&g_fs_off, -1, __ATOMIC_RELAXED);
    unlink(g_path);
    if (file) {
        FILE *f = fopen(g_path, "w");
        fputs(file, f);
        fclose(f);
    }
    memset(g_fm, 0, sizeof g_fm);
    fake_ctor((long) g_fm);
    *(long *) (g_view + 0x1540) = (long) g_fm;
}
static int shown(void) { return *(int *) (g_fm + 0x44); }
static void start(uintptr_t ret) { fs_ctor_apply((long) g_fm, ret); }
static void press(int button) { fs_buttons_hook((long) g_view, button); }
/* press the font size button until the size is `size` (the button only cycles) */
static void button(int size) {
    for (int k = 0; k < 3 && shown() != size; k++)
        press(1);
}
static int file_val(void) {
    FILE *f = fopen(g_path, "r");
    int n = -9;
    if (!f)
        return -1;
    if (fscanf(f, "%d", &n) != 1)
        n = -9;
    fclose(f);
    return n;
}
static void *presser(void *arg) {
    (void) arg;
    for (int k = 0; k < 2000; k++)
        fs_save(k % 3);
    return 0;
}

int main(void) {
    ut_suite = "remember font size";
    g_o_fs_buttons = fake_buttons;
    g_o_fs_bar = fake_bar_ctor;
    g_o_fs_ctor = fake_ctor;
    snprintf(g_path, sizeof g_path, "/tmp/fontsize-test-%d", (int) getpid());
    snprintf(g_off, sizeof g_off, "/tmp/fontsize-test-%d.off", (int) getpid());

    fresh("2\n");
    start(REG_RET + 0x40);
    CHECK("a FontManager built anywhere else is left alone", g_sets == 0 && shown() == 1);
    start(REG_RET);
    CHECK("the registered FontManager gets the saved size", g_sets == 1 && shown() == 2);
    CHECK("setting it does not write it back", g_writes == 0);
    fs_save(2); /* the size it already has, as a second press landing there would */
    CHECK("the same size from the button is not written again", g_writes == 0);
    button(0);
    CHECK("a new size from the button is saved", file_val() == 0 && g_writes == 1 && shown() == 0);
    button(1);
    CHECK("and the next", file_val() == 1 && g_writes == 2);

    fresh(0);
    start(REG_RET);
    CHECK("nothing saved: nothing set, the default stays", g_sets == 0 && shown() == 1);
    button(2);
    CHECK("with nothing saved, a size from the button is saved", file_val() == 2 && g_writes == 1);

    fresh("7\n");
    start(REG_RET);
    CHECK("an out-of-range saved size is ignored", g_sets == 0 && shown() == 1);
    fresh("x\n");
    start(REG_RET);
    CHECK("a damaged file is ignored", g_sets == 0);
    fresh("2\n");
    build_bar();
    CHECK("a Browse bar built before the FontManager keeps its default", g_bar_calls == 0 && g_bar_sel == 1);
    start(REG_RET);
    build_bar();
    CHECK("a Browse bar built after it shows the size in use", g_bar_calls == 1 && g_bar_sel == 2);
    *(uintptr_t *) g_fm = (uintptr_t) g_other_vt;
    build_bar();
    CHECK("a FontManager that is no longer one moves no selector", g_bar_calls == 0 && g_bar_sel == 1);
    *(uintptr_t *) g_fm = (uintptr_t) g_fm_vt;
    g_o_fs_bar = bar_noop; /* builds nothing: not a controller */
    *(uintptr_t *) g_bar = (uintptr_t) g_other_vt;
    build_bar();
    CHECK("an object that is not a status area controller is left alone", g_bar_calls == 0);
    g_o_fs_bar = fake_bar_ctor;

    fresh("0\n");
    start(REG_RET);
    press(0);
    CHECK("the PREVIEW button saves nothing", g_writes == 0);
    *(uintptr_t *) g_fm = (uintptr_t) g_other_vt;
    press(1);
    CHECK("a view whose FontManager is not one saves nothing", g_writes == 0);
    *(uintptr_t *) g_fm = (uintptr_t) g_fm_vt;
    fs_buttons_hook((long) g_view, 9);
    CHECK("another button saves nothing", g_writes == 0);
    fs_save(5);
    CHECK("a size out of range is not saved", file_val() == 0 && g_writes == 0);

    fresh("0\n");
    fclose(fopen(g_off, "w")); /* the kill switch */
    start(REG_RET);
    button(2);
    CHECK("with the kill switch nothing is set or saved", g_sets == 1 && shown() == 2 && file_val() == 0 && g_writes == 0);

    fresh("1\n");
    start(REG_RET);
    pthread_t a, b;
    pthread_create(&a, 0, presser, (void *) 0);
    pthread_create(&b, 0, presser, (void *) 1);
    pthread_join(a, 0);
    pthread_join(b, 0);
    int v = file_val();
    CHECK("sizes set on two threads at once leave a valid saved size", v >= 0 && v < 3);

    unlink(g_path);
    unlink(g_off);
    return ut_summary();
}
