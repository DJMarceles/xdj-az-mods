#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Test suite for the loader's TRUST rule (az-usbload, "TRUST" block): root code from a
# stick (deck/ scripts, deck/lib helpers, the preloaded .so files) runs only when its
# sha256 is pinned by deck/trust.pins and trust.pins carries a genuine minisign
# signature by the release key; /home/root/settings/azmods-unsigned.on is the
# developer override. Run by test/run.sh in its alpine container (`test/run.sh trust`).
#
# The suite signs with a throwaway key (openssl, minisign's format byte for byte) and
# points the loader at it with AZ_TRUST_PUB. t0 checks the verifier itself against a
# real release signature with the real embedded key, under the deck's own busybox and
# openssl (the extracted rootfs at /lower), with a flipped-byte control.
#
# Every refusal case has its control in the same round: t1 is the same stick signed and
# untouched (everything runs), t8 is the same tampered stick with the override (it runs).
set -u
apk add -q dosfstools openssl python3 >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH AZ_ACTIVE_WAIT=2
. /stub/fake-ep147
rm -f /home/root/settings/azmods-unsigned.on     # this suite tests the rule, not the override
for f in az-usbload az-usbpick; do cp /p/usr/sbin/$f /usr/sbin/$f; done; chmod +x /usr/sbin/az-usbload
mkdir -p /home/root/scripts; cp /p/home/root/scripts/usb-renotify.sh /home/root/scripts/
P7=/home/root/settings/az-usbload; ENVF=$P7/az-preload.env; DEV=/home/root/settings/azmods-unsigned.on
mkdir -p /m /media/usb/sda1 /home/root/settings
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
rm -f /img; dd if=/dev/zero of=/img bs=1M count=24 2>/dev/null; mkfs.vfat /img >/dev/null
lo=$(losetup -f); losetup "$lo" /img; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
st() { umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m; ( cd /m || exit 1; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
unplug() { umount /media/usb/sda1 2>/dev/null; mv /dev/sda1 /dev.sda1.off; }
replug() { mv /dev.sda1.off /dev/sda1; mount -o ro /dev/sda1 /media/usb/sda1; }
boot() { rm -rf /tmp/systemctl.calls /tmp/ran /tmp/az-usbload.log.hooks; mkdir -p /tmp/ran; /usr/sbin/az-usbload
         grep -hE "trust:|chain|desired|same as|env written|restart EP147" /tmp/az-usbload.log | sed 's/^/    /'; }
restarted() { grep -q "restart EP147" /tmp/systemctl.calls 2>/dev/null; }
env_() { cat $ENVF 2>/dev/null; }
lg() { grep -q "$1" /tmp/az-usbload.log; }
ran() { [ -e "/tmp/ran/$1" ]; }

# ---- a throwaway minisign key, and minisign-format signatures made with openssl ----
K=/tmp/key; mkdir -p $K
openssl genpkey -algorithm ed25519 -out $K/sk.pem 2>/dev/null
head -c 8 /dev/urandom > $K/id
{ printf 'Ed'; cat $K/id; openssl pkey -in $K/sk.pem -pubout -outform DER 2>/dev/null | tail -c 32; } > $K/pub.blob
TESTPUB=$(openssl base64 -A -in $K/pub.blob)
# $1 file -> $1.minisig (prehashed "ED", trusted comment, global signature); $2 = key dir
msign() {
    kd=${2:-$K}
    openssl dgst -blake2b512 -binary "$1" > /tmp/s.h
    openssl pkeyutl -sign -inkey "$kd"/sk.pem -rawin -in /tmp/s.h -out /tmp/s.sig 2>/dev/null
    tc="timestamp:1700000000	file:${1##*/}	hashed"
    { cat /tmp/s.sig; printf '%s' "$tc"; } > /tmp/s.g
    openssl pkeyutl -sign -inkey "$kd"/sk.pem -rawin -in /tmp/s.g -out /tmp/s.gsig 2>/dev/null
    { echo "untrusted comment: signature from a test key"
      { printf 'ED'; cat "$kd"/id; cat /tmp/s.sig; } | openssl base64 -A; echo
      echo "trusted comment: $tc"
      openssl base64 -A -in /tmp/s.gsig; echo; } > "$1.minisig"
}
# pins for a MODS dir: the release tool itself (tools/release/make_pins.py, mounted at /mk)
mkpins() { python3 /mk/make_pins.py "$1" >/dev/null || echo "FAIL make_pins.py on $1"; }
export AZ_TRUST_PUB="$TESTPUB"

# a hook that leaves a marker when it runs
mkhook() { printf '#!/bin/sh\ntouch /tmp/ran/%s\n' "$2" > "$1"; }
# the set: the real staged MODS/deck (what ships), its hooks swapped for markers, and two .so
SET=/tmp/set; rm -rf $SET; mkdir -p $SET/deck
cp -r /d/. $SET/deck/; rm -f $SET/deck/boot.d/*
mkhook $SET/deck/boot.d/10-a.sh a; mkhook $SET/deck/boot.d/20-b.sh b
echo core-v1 > $SET/azmod.so; echo stem-v1 > $SET/azstem.so
mkpins $SET; msign $SET/deck/trust.pins
put() { st "rm -rf MODS; mkdir MODS; cp -r $1/. MODS/"; }

echo "== t0 the verifier under the deck's own busybox + openssl (bash and busybox ash), with flipped-byte controls"
# t0a/b: a file signed here with the suite's key. t0r: a real release signature checked with
# the real embedded key, when this tree carries that fixture (a released set under
# test/fixtures/); a tree without it reports the check as absent.
F=/t/fixtures/real-mods-v1.2.2
echo "a set the deck must accept" > /tmp/signed.txt; msign /tmp/signed.txt
if [ -x /lower/bin/busybox ]; then
    mkdir -p /ov /deck && mount -t tmpfs ov /ov && mkdir -p /ov/u /ov/w
    mount -t overlay overlay -o lowerdir=/lower,upperdir=/ov/u,workdir=/ov/w /deck && mount --bind /dev /deck/dev && mount -t proc proc /deck/proc && mount -t tmpfs tmp /deck/tmp
    sed -n '/^TRUST_PUB=/,/^cached_ok()/p' /usr/sbin/az-usbload > /deck/tmp/trustfns
    flip() { cp "$1" "/deck/tmp/$2"; printf 'X' | dd of="/deck/tmp/$2" bs=1 seek=3 conv=notrunc 2>/dev/null; cp "$1.minisig" "/deck/tmp/$2.minisig"; }
    cp /tmp/signed.txt /tmp/signed.txt.minisig /deck/tmp/; flip /tmp/signed.txt flipped.txt
    # $1 shell, $2 key ("" = the embedded release key), $3 file
    # shellcheck disable=SC2086 # \$1 is a command and its argument ("/bin/busybox sh")
    dv() { chroot /deck /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin ${2:+AZ_TRUST_PUB=$2} $1 -c ". /tmp/trustfns; log(){ :; }; minisig_ok /tmp/$3 /tmp/$3.minisig && echo GENUINE || echo REFUSED"; }
    for SH in /bin/sh "/bin/busybox sh"; do
        chk "t0a deck userland ($SH): a genuine signature verifies" '[ "$(dv "$SH" "$TESTPUB" signed.txt)" = GENUINE ]'
        chk "t0b CONTROL deck userland ($SH): one flipped byte is REFUSED" '[ "$(dv "$SH" "$TESTPUB" flipped.txt)" = REFUSED ]'
        chk "t0c deck userland ($SH): the same signature against the embedded release key is REFUSED" '[ "$(dv "$SH" "" signed.txt)" = REFUSED ]'
    done
    if [ -f $F/manifest.json.minisig ]; then
        cp $F/manifest.json $F/manifest.json.minisig /deck/tmp/; flip $F/manifest.json flipped.json
        for SH in /bin/sh "/bin/busybox sh"; do
            chk "t0r deck userland ($SH): the real mods-v1.2.2 manifest verifies with the embedded key" '[ "$(dv "$SH" "" manifest.json)" = GENUINE ]'
            chk "t0r CONTROL deck userland ($SH): one flipped byte is REFUSED" '[ "$(dv "$SH" "" flipped.json)" = REFUSED ]'
        done
    else
        echo "== t0r no real release fixture in this tree: the embedded key is not checked against a real release here"
    fi
else
    echo "FAIL t0 no deck rootfs at /lower (set AZ_DECK_ROOTFS for run.sh): the verifier is untested on the deck's userland"
fi

echo "== t1 CONTROL: a genuinely signed set: chained, both hooks run, both .so staged"
put $SET; boot
chk "t1 verified logged" 'lg "trust: deck/ verified against the signed pins"'
chk "t1 chained to the stick loader" 'lg "start: CHAINED"'
chk "t1 hooks ran" 'ran a && ran b'
chk "t1 both .so preloaded" '[ "$(env_)" = "LD_PRELOAD=$P7/azmod.so:$P7/azstem.so" ] && restarted'
chk "t1 cache holds the hooks" '[ -f $P7/deck/boot.d/10-a.sh ] && [ -f $P7/deck/trust.pins.minisig ]'

echo "== t2 one byte changed in a signed hook: THAT hook does not run, the rest does"
cp -r $SET /tmp/t2; printf '#x\n' >> /tmp/t2/deck/boot.d/10-a.sh; put /tmp/t2; boot
chk "t2 tampered hook did NOT run" '! ran a'
chk "t2 untouched hook ran" 'ran b'
chk "t2 logged by name" 'lg "trust: deck/boot.d/10-a.sh is not pinned by the signed set -> removed"'
chk "t2 tampered hook never reached the p7 cache" '[ ! -e $P7/deck/boot.d/10-a.sh ] && [ -f $P7/deck/boot.d/20-b.sh ]'

echo "== t3 an EXTRA hook the set does not name: not run"
cp -r $SET /tmp/t3; mkhook /tmp/t3/deck/boot.d/15-evil.sh evil; put /tmp/t3; boot
chk "t3 extra hook did NOT run" '! ran evil'; chk "t3 set hooks ran" 'ran a && ran b'

echo "== t4 a tampered azmod.so: not staged, the deck comes up STOCK"
cp -r $SET /tmp/t4; echo evil > /tmp/t4/azmod.so; put /tmp/t4; boot
chk "t4 azmod.so refused" 'lg "trust: azmod.so is not pinned"'
chk "t4 no core -> desired none, env stock" 'lg "desired: none" && [ -z "$(env_)" ]'
chk "t4 the tampered bytes never reached p7" '! grep -q evil $P7/azmod.so 2>/dev/null'
chk "t4 hooks still ran (they verified)" 'ran a && ran b'

echo "== t5 pins edited to cover a tampered hook, NOT re-signed: signature fails, nothing runs"
cp -r $SET /tmp/t5; printf '#x\n' >> /tmp/t5/deck/boot.d/10-a.sh; mkpins /tmp/t5; put /tmp/t5; boot
chk "t5 refused whole" 'lg "trust: deck/ has no genuinely signed trust.pins"'
chk "t5 no hook ran" '! ran a && ! ran b'
chk "t5 not chained" '! lg "start: CHAINED"'
chk "t5 stock" '[ -z "$(env_)" ]'
chk "t5 the p7 cache is gone (nothing verified to cache)" '[ ! -e $P7/deck ]'

echo "== t6 an old-style stick: no pins at all -> nothing runs, stock"
cp -r $SET /tmp/t6; rm -f /tmp/t6/deck/trust.pins /tmp/t6/deck/trust.pins.minisig; put /tmp/t6; boot
chk "t6 refused" 'lg "trust: deck/ has no genuinely signed trust.pins"'
chk "t6 no hook ran, stock" '! ran a && ! ran b && [ -z "$(env_)" ]'

echo "== t6b a stick with only .so files (no deck/): stock; the SAME stick with the override loads (control)"
st 'rm -rf MODS; mkdir MODS; echo c > MODS/azmod.so'; boot
chk "t6b no deck/: .so refused, stock" 'lg "trust: azmod.so is not pinned" && [ -z "$(env_)" ]'
touch $DEV; boot; rm -f $DEV
chk "t6b CONTROL with the override: staged" '[ "$(env_)" = "LD_PRELOAD=$P7/azmod.so" ]'

echo "== t7 genuinely signed, but by ANOTHER key: refused"
K2=/tmp/key2; mkdir -p $K2; openssl genpkey -algorithm ed25519 -out $K2/sk.pem 2>/dev/null; head -c 8 /dev/urandom > $K2/id
cp -r $SET /tmp/t7; msign /tmp/t7/deck/trust.pins $K2; put /tmp/t7; boot
chk "t7 refused" 'lg "trust: deck/ has no genuinely signed trust.pins"'
chk "t7 nothing ran, stock" '! ran a && ! ran b && [ -z "$(env_)" ]'

echo "== t8 CONTROL for t2/t4: the same tampered stick WITH the developer override runs everything"
cp -r $SET /tmp/t8; printf '#x\n' >> /tmp/t8/deck/boot.d/10-a.sh; echo devbuild > /tmp/t8/azmod.so; touch $DEV; put /tmp/t8; boot
chk "t8 override logged" 'lg "trust: DEVELOPER OVERRIDE"'
chk "t8 tampered hook ran" 'ran a && ran b'
chk "t8 dev azmod.so staged" 'grep -q devbuild $P7/azmod.so && [ "$(env_)" = "LD_PRELOAD=$P7/azmod.so:$P7/azstem.so" ]'
rm -f $DEV
st 'touch MODS/azmods-unsigned.on'; boot
chk "t8b MODS/azmods-unsigned.on on the stick does NOT override" '! lg "DEVELOPER OVERRIDE" && ! ran a && [ -z "$(env_)" ]'

echo "== t9 no stick: the cache runs only what verifies (an old unpruned cache is pruned)"
put $SET; boot >/dev/null                       # a verified cache
mkhook $P7/deck/boot.d/12-planted.sh planted    # a file planted in the cache by older code
unplug; boot; replug
chk "t9 planted cache hook did NOT run" '! ran planted'
chk "t9 verified cached hooks ran" 'ran a && ran b'
chk "t9 the cache was cleaned" '[ ! -e $P7/deck/boot.d/12-planted.sh ]'

echo "== t10 a tampered deck/az-usbload: never chained; the firmware copy keeps the boot (control: t1 chained)"
cp -r $SET /tmp/t10; printf '\necho PWNED > /tmp/pwned\n' >> /tmp/t10/deck/az-usbload; rm -f /tmp/pwned; put /tmp/t10; boot
chk "t10 not chained" '! lg "start: CHAINED"'
chk "t10 the appended line never ran" '[ ! -e /tmp/pwned ]'
chk "t10 the boot still applied the verified set" 'ran a && [ "$(env_)" = "LD_PRELOAD=$P7/azmod.so:$P7/azstem.so" ]'

echo "== t11 --post-start: a cached loader that does not verify is not run"
put $SET; boot >/dev/null; touch /run/az-usbload.booted
printf '\necho PWNED > /tmp/pwned\n' >> $P7/deck/az-usbload; rm -f /tmp/pwned
AZ_ACTIVE_WAIT=1 AZ_RENOTIFY_DELAY1=0 AZ_RENOTIFY_DELAY2=0 /usr/sbin/az-usbload --post-start
chk "t11 refused logged" 'grep -q "cached deck/az-usbload is not pinned" /tmp/az-usbload-poststart.log'
chk "t11 not run" '[ ! -e /tmp/pwned ]'
put $SET; boot >/dev/null; rm -f /tmp/pwned
AZ_ACTIVE_WAIT=1 AZ_RENOTIFY_DELAY1=0 AZ_RENOTIFY_DELAY2=0 /usr/sbin/az-usbload --post-start
chk "t11b CONTROL: the verified cached loader IS chained" 'grep -q "handing over to the cached deck/az-usbload" /tmp/az-usbload-poststart.log'

echo "== t12 firmware 1.32 (verifies nothing) chains the new stick loader: IT prunes before any hook runs"
# exactly what the 1.32 firmware does: copy MODS/deck to /tmp/az-deck and the p7 cache, chain
cat > /tmp/fw132 <<'F'
#!/bin/sh
rm -rf /tmp/az-deck; cp -a /media/usb/sda1/MODS/deck /tmp/az-deck
rm -rf /home/root/settings/az-usbload/deck; mkdir -p /home/root/settings/az-usbload; cp -a /tmp/az-deck /home/root/settings/az-usbload/deck
: > /tmp/az-usbload.log; AZ_CHAINED=1 sh /tmp/az-deck/az-usbload
F
put /tmp/t2; rm -rf /tmp/ran; mkdir -p /tmp/ran; rm -f /tmp/systemctl.calls; sh /tmp/fw132
chk "t12 chained loader ran" 'lg "start: CHAINED"'
chk "t12 tampered hook did NOT run" '! ran a'; chk "t12 verified hook ran" 'ran b'
chk "t12 the chained loader repaired the 1.32-written cache" '[ ! -e $P7/deck/boot.d/10-a.sh ] && [ -f $P7/deck/boot.d/20-b.sh ]'
put /tmp/t5; rm -rf /tmp/ran; mkdir -p /tmp/ran; sh /tmp/fw132
chk "t12b unsigned pins under 1.32: the chained loader runs no hook and stages nothing" '! ran a && ! ran b && lg "no verified mods" '

echo "== t13 the loader sources deck/lib/az-logcap.sh (root) only from a copy that verifies"
# a fresh boot with no stick: /tmp is empty and the only copy is the p7 cache, where older
# code (or a developer-override boot) left a helper the signed set does not pin
put $SET; boot >/dev/null
printf 'touch /tmp/ran/logcap-planted\nazlog_cap() { return 0; }\n' > $P7/deck/lib/az-logcap.sh
rm -rf /tmp/az-deck; unplug; boot >/dev/null
chk "t13 an unpinned cached az-logcap.sh is NOT sourced" '! ran logcap-planted'
printf 'touch /tmp/ran/logcap-planted\nazlog_cap() { return 0; }\n' > $P7/deck/lib/az-logcap.sh
rm -rf /tmp/az-deck; touch $DEV; boot >/dev/null; rm -f $DEV
chk "t13 CONTROL: the same planted helper under the developer override IS sourced" 'ran logcap-planted'
replug
