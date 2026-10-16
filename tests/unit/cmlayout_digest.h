/* SPDX-License-Identifier: GPL-3.0-only */
/* cmlayout_digest.h: force-included into cmcat_test.c and cmmod_test.c by cmlayout-golden.sh;
 * the definition is cmlayout_digest.c, appended to the test copies.
 * After every cm_layout call the tests make, CMLAYOUT_DIGEST() appends one line per layout to
 * $CMLAYOUT_OUT: the call itself, then the same fixture state laid out in every view, full screen
 * and docked, every tab, a range of catalog and page indices and two scroll positions. Each line
 * is an FNV-1a digest of the framebuffer, the controls and the text drawn. The tests' own frame,
 * controls and captured text are restored afterwards, so their checks see exactly what they would
 * without the digest. Text color is not covered: the text stub records the words only. */
static void cmd_record(void);
#define CMLAYOUT_DIGEST() cmd_record()
