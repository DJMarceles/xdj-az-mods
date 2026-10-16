# dropbear for firmware without one (XDJ-AZ 2.00)

Firmware 2.00 has no dropbear SSH server (1.30 ships one). The deck's SSH (off unless the stick
has `MODS/ssh.enable` and a key the deck has approved) therefore brings its own:
`stick/deck/lib/dropbear`, used by `stick/deck/boot.d/10-sshgate.sh` only when
`/usr/sbin/dropbear` is absent. The binary is not published with the source; build it with
`build-dropbear.sh`.

| | |
|---|---|
| upstream | dropbear **2024.86**, https://matt.ucc.asn.au/dropbear/releases/dropbear-2024.86.tar.bz2 |
| tarball sha256 | `e78936dffc395f2e0db099321d6be659190966b99712b55c530dd0a1822e0a5e` (matches upstream `SHA256SUM.asc`; the .asc's GPG signature was not verified) |
| build | `build-dropbear.sh`: azmod-build:18.04 arm64, `--disable-pam --disable-zlib` (+ no lastlog/utmp/wtmp; syslog kept: without it -E is an invalid option), `make PROGRAMS=dropbear`, stripped |
| checks | the script refuses a binary whose highest `GLIBC_` symbol is above 2.29, and one that rejects the hook's options (`-F -E -s -g -R`, run once in the same image) |
| binary sha256 | `9a24a0d481852cacb1f38d215b69d74fde701c2c0b0a0af54bfcc250baa66e2a` |
| max glibc | `GLIBC_2.25` (deck: 2.29) |
| NEEDED | libutil.so.1, libcrypt.so.1, libc.so.6, ld-linux-aarch64.so.1 (all present on 2.00) |
| license | MIT-style, shipped beside it as `dropbear.LICENSE` |

The binary is built from the pinned upstream release, never copied from vendor firmware, so its
provenance and library set are known and it can be patched. The sha256 changes only if the toolchain
or source changes.
