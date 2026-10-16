#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Test suite for deck/lib/az-modupdate's update path against a local channel (python
# http.server): what a redirected channel pointer can and cannot make a deck install.
# Sets are signed here with a throwaway key in minisign's format (openssl); the script
# is pointed at it with AZ_MODS_PUB, at the fixture with AZ_MODS_API/DL/PING/REPO.
# Run by test/run.sh in its alpine container (`test/run.sh modupdate`).
#
# m1 is the control: a genuine newer set installs. m2/m3 serve genuine signed sets under
# a tag that lies about them (the rollback a hijacked pointer could attempt): refused,
# stick untouched. m4: a tampered manifest is refused by its signature. m6: an entry-level
# "dir" places every file of that entry (m6c, the walk removed, is its control). m7: a stale
# file under deck/ whose path, read as a pattern, matches a file of the set is still removed
# (m7c, the old pattern match, is its control).
set -u
apk add -q openssl curl python3 >/dev/null 2>&1 || { echo "FAIL apk add openssl curl python3"; exit 1; }
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
K=/tmp/key; mkdir -p $K
openssl genpkey -algorithm ed25519 -out $K/sk.pem 2>/dev/null; head -c 8 /dev/urandom > $K/id
{ printf 'Ed'; cat $K/id; openssl pkey -in $K/sk.pem -pubout -outform DER 2>/dev/null | tail -c 32; } > $K/pub.blob
PUB=$(openssl base64 -A -in $K/pub.blob)
msign() {
    openssl dgst -blake2b512 -binary "$1" > /tmp/s.h
    openssl pkeyutl -sign -inkey $K/sk.pem -rawin -in /tmp/s.h -out /tmp/s.sig 2>/dev/null
    tc="timestamp:1700000000	file:${1##*/}	hashed"
    { cat /tmp/s.sig; printf '%s' "$tc"; } > /tmp/s.g
    openssl pkeyutl -sign -inkey $K/sk.pem -rawin -in /tmp/s.g -out /tmp/s.gsig 2>/dev/null
    { echo "untrusted comment: signature from a test key"
      { printf 'ED'; cat $K/id; cat /tmp/s.sig; } | openssl base64 -A; echo
      echo "trusted comment: $tc"; openssl base64 -A -in /tmp/s.gsig; echo; } > "$1.minisig"
}
# a genuine signed set of version $1, published under tag mods-v$2 (normally the same)
mkset() {
    v=$1; tag=$2; d=/tmp/set-$v; rm -rf "$d"; mkdir -p "$d/deck"
    echo "core $v" > "$d/azmod.so"; echo "$v" > "$d/deck/version"
    python3 - "$d" "$v" <<'PY'
import hashlib, json, sys
d, v = sys.argv[1], sys.argv[2]
h = lambda p: hashlib.sha256(open(f"{d}/{p}", "rb").read()).hexdigest()
m = {"version": v, "entries": [{"id": "core", "version": "1.0.0", "required": True, "files": [
     {"name": "azmod.so", "sha256": h("azmod.so")}, {"name": "version", "sha256": h("deck/version"), "dir": "deck"}]}]}
open(f"{d}/manifest.json", "w").write(json.dumps(m, indent=2) + "\n")
PY
    o=/srv/dl/mods-v$tag; mkdir -p "$o"
    cp "$d/manifest.json" "$o/"; ( cd "$d" && tar -czf "$o/mods-v$tag.tar.gz" azmod.so deck/version )
    msign "$o/manifest.json"; msign "$o/mods-v$tag.tar.gz"
}
channel() { mkdir -p /srv/api; printf '[\n  {\n    "tag_name": "mods-v%s",\n    "draft": false\n  }\n]\n' "$1" > /srv/api/releases; }
mkdir -p /srv; echo ok > /srv/ping
( cd /srv && exec python3 -m http.server 8099 --bind 127.0.0.1 >/dev/null 2>/tmp/http.log ) & SRV=$!
export PATH=/stub:$PATH; rm -f /tmp/systemctl.calls   # any systemctl call is recorded
trap 'kill $SRV 2>/dev/null' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do wget -q -O /dev/null http://127.0.0.1:8099/ping 2>/dev/null && break; sleep 0.5; done
B=http://127.0.0.1:8099
stick() { rm -rf /stick; mkdir -p /stick/MODS/deck; echo "core $1" > /stick/MODS/azmod.so; echo "$1" > /stick/MODS/deck/version; }
upd() { rm -rf /tmp/az-modupdate
        AZ_MNT=/stick AZ_MODS=/stick/MODS AZ_MODS_REPO=fixture/channel AZ_MODS_API="$B/api/releases?per_page=30" \
        AZ_MODS_DL=$B/dl AZ_MODS_PING=$B/ping AZ_MODS_PUB=$PUB sh /d/lib/az-modupdate update
        sed -n 's/^msg=//p' /tmp/az-modupdate/status | sed 's/^/    /'; }
state() { sed -n 's/^state=//p' /tmp/az-modupdate/status; }
msg() { sed -n 's/^msg=//p' /tmp/az-modupdate/status; }

echo "== m1 CONTROL: a genuine newer set (signed 1.3.0, tag 1.3.0) installs over 1.2.2"
mkset 1.3.0 1.3.0; channel 1.3.0; stick 1.2.2; upd
chk "m1 state installed, and the screen's line says Reload to apply" '[ "$(state)" = installed ] && [ "$(msg)" = "Update installed. Reload to apply." ]'
chk "m1 az-modupdate itself never restarts EP147 (no systemctl call)" '[ ! -s /tmp/systemctl.calls ]'
chk "m1 the stick now carries 1.3.0" '[ "$(cat /stick/MODS/deck/version)" = 1.3.0 ] && grep -q "core 1.3.0" /stick/MODS/azmod.so'

echo "== m2 ROLLBACK: the genuine signed 1.2.0 set published under tag mods-v9.9.9"
mkset 1.2.0 9.9.9; channel 9.9.9; stick 1.2.2; upd
chk "m2 refused" '[ "$(state)" = error ] && msg | grep -q "signed manifest is v1.2.0: REFUSED"'
chk "m2 stick untouched" '[ "$(cat /stick/MODS/deck/version)" = 1.2.2 ] && grep -q "core 1.2.2" /stick/MODS/azmod.so'

echo "== m2c CONTROL: the same script WITHOUT the signed-version check installs that rollback"
sed '/MV=\$(sed/,/^ *fi$/d' /d/lib/az-modupdate > /tmp/az-modupdate.nocheck
mkset 1.2.0 9.9.9; channel 9.9.9; stick 1.2.2; rm -rf /tmp/az-modupdate
AZ_MNT=/stick AZ_MODS=/stick/MODS AZ_MODS_REPO=fixture/channel AZ_MODS_API="$B/api/releases?per_page=30" \
    AZ_MODS_DL=$B/dl AZ_MODS_PING=$B/ping AZ_MODS_PUB=$PUB sh /tmp/az-modupdate.nocheck update
chk "m2c the mutation really removed the check" '! grep -q "no downgrade" /tmp/az-modupdate.nocheck && sh -n /tmp/az-modupdate.nocheck'
chk "m2c without it the deck DOWNGRADES to 1.2.0" '[ "$(cat /stick/MODS/deck/version)" = 1.2.0 ]'

echo "== m3 tag and signed version agree, but it is OLDER than the stick: refused (no downgrade)"
mkset 1.2.1 1.2.1; channel 1.2.1; stick 1.2.2; upd
chk "m3 nothing offered or installed" '[ "$(cat /stick/MODS/deck/version)" = 1.2.2 ]'
chk "m3 said up to date (check() compares the tag; update() would refuse the signed version too)" '[ "$(state)" = uptodate ]'

echo "== m3b a bare stick (no deck/version): a genuine set installs"
stick 1.2.2; rm -f /stick/MODS/deck/version; mkset 1.3.0 1.3.0; channel 1.3.0; upd
chk "m3b installed" '[ "$(state)" = installed ]'

echo "== m5 staged: the loader staged the set without a restart -> a final state, the same line, no network"
rm -f /srv/hits; LOCALV=$(cat /stick/MODS/deck/version)
AZ_MNT=/stick AZ_MODS=/stick/MODS AZ_MODS_POINTER=$B/pointer-must-not-be-fetched sh /d/lib/az-modupdate staged
chk "m5 state uptodate (final on every screen), msg Reload to apply, local = the stick's set" '[ "$(state)" = uptodate ] && [ "$(msg)" = "Update installed. Reload to apply." ] && grep -qx "local=$LOCALV" /tmp/az-modupdate/status'
chk "m5 the channel pointer was not fetched" '! grep -q pointer-must-not-be-fetched /tmp/http.log'
chk "m5 CONTROL: a check DOES fetch the pointer (the log can show it)" 'AZ_MNT=/stick AZ_MODS=/stick/MODS AZ_MODS_POINTER=$B/pointer-must-not-be-fetched AZ_MODS_API=$B/api/releases AZ_MODS_DL=$B/dl AZ_MODS_PING=$B/ping AZ_MODS_PUB=$PUB sh /d/lib/az-modupdate check; sleep 0.3; grep -q pointer-must-not-be-fetched /tmp/http.log'

echo "== m4 a manifest edited after signing: refused by the signature"
mkset 1.4.0 1.4.0; sed -i 's/"version": "1.4.0"/"version": "1.4.0" /' /srv/dl/mods-v1.4.0/manifest.json; channel 1.4.0; stick 1.2.2; upd
chk "m4 refused" '[ "$(state)" = error ] && msg | grep -q "manifest signature REFUSED"'
chk "m4 stick untouched" '[ "$(cat /stick/MODS/deck/version)" = 1.2.2 ]'

echo "== m6 an entry-level dir applies to every file of that entry (the app reads f.dir or e.dir)"
mkset_entrydir() {
    v=$1; d=/tmp/set-$v; rm -rf "$d"; mkdir -p "$d/deck" "$d/art"
    echo "core $v" > "$d/azmod.so"; echo "$v" > "$d/deck/version"; echo A > "$d/art/a.bin"; echo B > "$d/art/b.bin"
    python3 - "$d" "$v" <<'PY'
import hashlib, json, sys
d, v = sys.argv[1], sys.argv[2]
h = lambda p: hashlib.sha256(open(f"{d}/{p}", "rb").read()).hexdigest()
m = {"version": v, "entries": [
     {"id": "core", "version": "1.0.0", "required": True, "files": [
      {"name": "azmod.so", "sha256": h("azmod.so")}, {"name": "version", "sha256": h("deck/version"), "dir": "deck"}]},
     {"id": "art", "dir": "art", "description": "braces { } and a colon: inside a value", "files": [
      {"name": "a.bin", "sha256": h("art/a.bin")}, {"name": "b.bin", "sha256": h("art/b.bin")}]}]}
open(f"{d}/manifest.json", "w").write(json.dumps(m, indent=2) + "\n")
PY
    o=/srv/dl/mods-v$v; mkdir -p "$o"
    cp "$d/manifest.json" "$o/"; ( cd "$d" && tar -czf "$o/mods-v$v.tar.gz" azmod.so deck/version art/a.bin art/b.bin )
    msign "$o/manifest.json"; msign "$o/mods-v$v.tar.gz"
}
mkset_entrydir 1.5.0; channel 1.5.0; stick 1.2.2; upd
chk "m6 installed, both files of the entry in its dir" '[ "$(state)" = installed ] && [ "$(cat /stick/MODS/art/a.bin)" = A ] && [ "$(cat /stick/MODS/art/b.bin)" = B ]'
echo "== m6c CONTROL: the entry's dir ignored (a file's own dir only): the same set is refused"
sed '/for (i = k - 1; d == "" \&\& i > 0; i--) d = dr\[i\]/d' /d/lib/az-modupdate > /tmp/az-modupdate.nodir
mkset_entrydir 1.5.0; channel 1.5.0; stick 1.2.2; rm -rf /tmp/az-modupdate
AZ_MNT=/stick AZ_MODS=/stick/MODS AZ_MODS_REPO=fixture/channel AZ_MODS_API="$B/api/releases?per_page=30" \
    AZ_MODS_DL=$B/dl AZ_MODS_PING=$B/ping AZ_MODS_PUB=$PUB sh /tmp/az-modupdate.nodir update
chk "m6c the mutation really removed the entry-dir walk" '! grep -q "d = dr\[i\]" /tmp/az-modupdate.nodir && sh -n /tmp/az-modupdate.nodir'
chk "m6c without it the entry's files are looked up flat and the update fails" '[ "$(state)" = error ] && [ ! -e /stick/MODS/art/b.bin ]'

echo "== m7 a stale deck/ file whose path, as a pattern, matches a file of the set: removed"
mkset 1.6.0 1.6.0; channel 1.6.0; stick 1.2.2; echo stale > /stick/MODS/deck/v.rsion; upd
chk "m7 installed, the stale file removed, deck/version kept" '[ "$(state)" = installed ] && [ ! -e /stick/MODS/deck/v.rsion ] && [ "$(cat /stick/MODS/deck/version)" = 1.6.0 ]'
echo "== m7c CONTROL: the path matched as a pattern (grep \"^\$r \"): the stale file stays"
sed 's/R=$r awk .\$1 == ENVIRON\["R"\] { f = 1; exit } END { exit !f }. "$STAGE\/files" ||/grep -q "^$r " "$STAGE\/files" ||/' /d/lib/az-modupdate > /tmp/az-modupdate.pattern
mkset 1.6.0 1.6.0; channel 1.6.0; stick 1.2.2; echo stale > /stick/MODS/deck/v.rsion; rm -rf /tmp/az-modupdate
AZ_MNT=/stick AZ_MODS=/stick/MODS AZ_MODS_REPO=fixture/channel AZ_MODS_API="$B/api/releases?per_page=30" \
    AZ_MODS_DL=$B/dl AZ_MODS_PING=$B/ping AZ_MODS_PUB=$PUB sh /tmp/az-modupdate.pattern update
chk "m7c the mutation really restored the pattern match" '! grep -q ENVIRON /tmp/az-modupdate.pattern && grep -q "grep -q \"^\$r \"" /tmp/az-modupdate.pattern && sh -n /tmp/az-modupdate.pattern'
chk "m7c with it the stale file is kept" '[ "$(state)" = installed ] && [ -e /stick/MODS/deck/v.rsion ]'
