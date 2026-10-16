/* SPDX-License-Identifier: GPL-3.0-only */
/* Build identification (guest/common/ep147build.h).
 *
 * The positive path (a real EP147 identified as 1.30 / 2.00) needs the real
 * binaries and is deliberately not faked here. What is tested:
 *   - the md5 implementation against the RFC 1321 appendix A.5 vectors;
 *   - every rejection path of ep147_identify: missing file, wrong size, and a
 *     file whose size matches a known build but whose md5 does not;
 *   - that a size-matched file really is hashed (md5_out equals the md5 the
 *     host's own tool computed), so the md5 comparison is what rejected it.
 *
 * argv: <file sized like 1.30> <its md5> <file sized like 2.00> <its md5> <small file> */
#include "ep147build.h"
#include "check.h"

static int md5_is(const char *msg, const char *want) {
    char got[33];
    epb_md5_hex((const uint8_t *) msg, strlen(msg), got);
    return strcmp(got, want) == 0;
}

int main(int argc, char **argv) {
    ut_suite = "ep147build";
    if (argc != 6) {
        fprintf(stderr, "usage: see header\n");
        return 2;
    }

    /* RFC 1321, A.5 test suite */
    CHECK("md5 \"\"", md5_is("", "d41d8cd98f00b204e9800998ecf8427e"));
    CHECK("md5 \"a\"", md5_is("a", "0cc175b9c0f1b6a831c399e269772661"));
    CHECK("md5 \"abc\"", md5_is("abc", "900150983cd24fb0d6963f7d28e17f72"));
    CHECK("md5 \"message digest\"", md5_is("message digest", "f96b697d7cb7938d525a2f31aaf161d0"));
    CHECK("md5 a..z", md5_is("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"));
    CHECK("md5 A..Za..z0..9 (62 bytes, two-block padding)",
          md5_is("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
                 "d174ab98d277d9f5a5611c2c9f419d9f"));
    CHECK("md5 8x\"1234567890\" (80 bytes, multi-block)",
          md5_is("1234567890123456789012345678901234567890"
                 "1234567890123456789012345678901234567890",
                 "57edf4a22be3c955ac49da2e2107b67a"));

    char h[33];
    long sz;
    int id;

    memcpy(h, "x", 2);
    sz = -1;
    id = ep147_identify("/nonexistent/ep147", h, &sz);
    CHECK("missing file -> EPB_UNKNOWN", id == EPB_UNKNOWN);
    CHECK("missing file -> md5_out cleared", h[0] == 0);

    memcpy(h, "x", 2);
    sz = -1;
    id = ep147_identify(argv[5], h, &sz);
    CHECK("wrong-size file -> EPB_UNKNOWN", id == EPB_UNKNOWN);
    CHECK("wrong-size file is not hashed (md5_out empty)", h[0] == 0);
    CHECK("wrong-size file reports its size", sz > 0);

    for (int k = 0; k < 2; k++) {
        const char *path = argv[1 + 2 * k], *want = argv[2 + 2 * k];
        long expect_size = EPB_KNOWN[k].size;
        char name[96];
        h[0] = 0;
        sz = -1;
        id = ep147_identify(path, h, &sz);
        snprintf(name, sizeof name, "size of %s build, wrong md5 -> EPB_UNKNOWN", EPB_KNOWN[k].name);
        CHECK(name, id == EPB_UNKNOWN);
        snprintf(name, sizeof name, "size of %s build is hashed, md5 matches host tool", EPB_KNOWN[k].name);
        CHECK(name, strcmp(h, want) == 0);
        snprintf(name, sizeof name, "size of %s build reported", EPB_KNOWN[k].name);
        CHECK(name, sz == expect_size);
    }

    id = ep147_identify(argv[1], NULL, NULL);
    CHECK("NULL out-params accepted, still EPB_UNKNOWN", id == EPB_UNKNOWN);

    CHECK("build name of EPB_UNKNOWN is \"UNKNOWN\"", !strcmp(ep147_build_name(EPB_UNKNOWN), "UNKNOWN"));
    CHECK("build name of an id with no table is \"UNKNOWN\"", !strcmp(ep147_build_name(999), "UNKNOWN"));
    CHECK("no table entry carries id EPB_UNKNOWN", EPB_KNOWN[0].id != EPB_UNKNOWN && EPB_KNOWN[1].id != EPB_UNKNOWN);
    return ut_summary();
}
