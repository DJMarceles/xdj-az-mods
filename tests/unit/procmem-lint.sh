#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# procmem-lint.sh [AZMOD.so AZSTEM.so]: a release build reads EP147's memory with plain loads
# only (guest/azmod/ep_mem.h): inside EP147's own calls, or under a lifetime guarantee. So the
# release azmod.so / azstem.so (default guest/out, built by build-check.sh):
#   1. contain no "/proc/self/mem" string;
#   2. define none of the fault-free readers safe_q / safe_qv / safe_str / srcmem_fd;
#   3. call process_vm_readv only from the crash reporter's frame walk (crash_peek8 inlined into
#      cr_read8 and cm_blame_fault), and azstem not at all.
# Rules 2 and 3 read the full symbol table, which a release build does not ship (guest/Makefile
# strips it). So each library is scanned through <lib>.full, the same link with its symbols, and
# the shipped file must be exactly that file less symbols; a stripped library without its .full is a
# failure, never a pass (the scan could not see the functions it checks).
# Control, in the same run: a planted library that breaks each rule must be reported for each,
# and a stripped planted library with no .full must be reported as unscannable.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd); fail=0
AZMOD=${1:-$root/guest/out/azmod.so}; AZSTEM=${2:-$root/guest/out/azstem.so}
for f in "$AZMOD" "$AZSTEM"; do
    [ -s "$f" ] || { echo "FAIL  procmem: $f missing (build first)"; exit 1; }
done
# the symbol-bearing copy of each library: <lib>.full if the build made one, else the lib itself
full() { if [ -s "$1.full" ]; then echo "$1.full"; else echo "$1"; fi; }
mkdir -p "$root/guest/out"; d=$(mktemp -d "$root/guest/out/procmem.XXXXXX"); trap 'rm -rf "$d"' EXIT
cp "$AZMOD" "$d/azmod.ship"; cp "$AZSTEM" "$d/azstem.ship"
cp "$(full "$AZMOD")" "$d/azmod.so"; cp "$(full "$AZSTEM")" "$d/azstem.so"
# allowed callers of process_vm_readv: the crash reporter only
ALLOW='^(cr_read8|cm_blame_fault|crash_peek8)(\.|$)'
docker run --rm --platform=linux/arm64 -v "$d":/s azmod-build:18.04 sh -c '
    cat > /tmp/p.c <<EOF
#include <sys/uio.h>
#include <unistd.h>
const char *planted_path = "/proc/self/mem";
int safe_q(unsigned long a, unsigned long *o) { (void) a; *o = 0; return 0; }
int planted_reader(void *a, void *o) {
    struct iovec l = {o, 8}, r = {a, 8};
    return (int) process_vm_readv(getpid(), &l, 1, &r, 1, 0);
}
EOF
    gcc -shared -fPIC -O1 -o /s/planted.so /tmp/p.c
    strip --strip-unneeded -o /s/stripped.so /s/planted.so
    # the shipped file must be the scanned build, stripped: anything else was not scanned
    for f in azmod azstem; do
        cmp -s /s/$f.so /s/$f.ship && continue   # no .full: the file itself was scanned
        strip --strip-all -o /tmp/$f.a /s/$f.so; strip --strip-all -o /tmp/$f.b /s/$f.ship
        cmp -s /tmp/$f.a /tmp/$f.b || echo "NOTSAME $f.so"
    done
    for f in azmod.so azstem.so planted.so stripped.so; do
        [ -s /s/$f ] || echo "MISSING $f"
        readelf -S /s/$f 2>/dev/null | grep -q "\.symtab" || echo "NOSYMS $f"
        grep -a -q -F /proc/self/mem /s/$f && echo "STR $f"
        nm /s/$f 2>/dev/null | awk "{print \$NF}" | sed "s|^|SYM $f |"
        objdump -d --no-show-raw-insn /s/$f | awk -v F=$f "
            /^[0-9a-f]+ <.*>:\$/ { fn = \$2; gsub(/[<>:]/, \"\", fn) }
            /bl[ \t]+[0-9a-f]+ <process_vm_readv@plt>/ { print \"PVR\", F, fn }"
    done' > "$d/scan.txt" 2>"$d/err.txt" || { echo "FAIL  procmem: scan failed"; cat "$d/err.txt"; exit 1; }
grep -q '^MISSING' "$d/scan.txt" && { echo "FAIL  procmem: the scan could not see $(grep '^MISSING' "$d/scan.txt")"; exit 1; }
check() { # check <lib> -> prints one line per broken rule
    local lib=$1
    grep -q "^NOSYMS $lib\$" "$d/scan.txt" && echo "no symbol table to scan (stripped, and no $lib.full)"
    grep -q "^NOTSAME $lib\$" "$d/scan.txt" && echo "the shipped file is not $lib.full less its symbols"
    grep -q "^STR $lib\$" "$d/scan.txt" && echo "string /proc/self/mem"
    awk -v L="$lib" '$1 == "SYM" && $2 == L && $3 ~ /^(safe_q|safe_qv|safe_str|srcmem_fd)$/ {print "reader " $3}' "$d/scan.txt"
    awk -v L="$lib" -v A="$ALLOW" '$1 == "PVR" && $2 == L && (L != "azmod.so" || $3 !~ A) {print "process_vm_readv in " $3}' "$d/scan.txt" | sort -u
}
for lib in azmod.so azstem.so; do
    bad=$(check $lib)
    if [ -z "$bad" ]; then echo "PASS  procmem: $lib has no /proc/self/mem, no fault-free reader, no other process_vm_readv"
    else echo "FAIL  procmem: $lib:"; printf '%s\n' "$bad" | sed 's/^/      /'; fail=1; fi
done
grep -q '^PVR azmod.so ' "$d/scan.txt" && echo "      (process_vm_readv callers in azmod.so: $(awk '$1=="PVR" && $2=="azmod.so" {print $3}' "$d/scan.txt" | sort -u | tr '\n' ' '))"
ctl=$(check planted.so)
if [[ "$(check stripped.so)" == *"no symbol table to scan"* ]]; then
    echo "CONTROL FAILED AS REQUIRED  procmem: a stripped library with no .full is reported as unscannable"
else echo "CONTROL DID NOT FAIL  procmem: a stripped library passed (instrument blind)"; fail=1; fi
for rule in "string /proc/self/mem" "reader safe_q" "process_vm_readv in planted_reader"; do
    if printf '%s\n' "$ctl" | grep -q -F "$rule"; then echo "CONTROL FAILED AS REQUIRED  procmem: planted '$rule' reported"
    else echo "CONTROL DID NOT FAIL  procmem: planted '$rule' not seen (instrument broken)"; fail=1; fi
done
[ $fail = 0 ] && echo "PROCMEM: OK" || echo "PROCMEM: FAILED"; exit $fail
