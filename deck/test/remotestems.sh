# SPDX-License-Identifier: GPL-3.0-only
set -u
# deck/lib/azrs.sh (the Remote Stems helper, REMOTE-STEMS.md) against a fake server
# (remotestems_fixture.py) that loses responses, times out, refuses and rejects, run by
# the deck's own userland: the 2.01 rootfs (busybox, curl, bash as /bin/sh) mounted
# read-only at /lower and overlaid writable at /deck. Every case runs under the deck's
# `busybox ash` and its /bin/sh (bash).
#
# The helper under test is the staged copy from MODS/deck (/d/lib/azrs.sh), i.e. what
# ships; AZRS_UNDER_TEST=<path inside the container> tests another file instead (an
# older helper, to see which cases it fails). CONTROLS: each fix is reverted in a copy
# of the helper (one mutation per control, refused if it does not apply) and the cases
# for that fix must then FAIL, in the same run. A passing control fails the suite.
#
# Discovery: avahi-browse is a stub that lists the fixture at 127.0.0.1:8766 with the
# pair file's id (the deck's avahi daemon is not running here).
[ -x /lower/bin/busybox ] || { echo "FAIL no deck rootfs mounted at /lower (set AZ_DECK_ROOTFS for run.sh)"; exit 1; }
apk add -q python3 >/dev/null 2>&1 || { echo "FAIL apk add python3"; exit 1; }
SRC=${AZRS_UNDER_TEST:-/d/lib/azrs.sh}
[ -f "$SRC" ] || { echo "FAIL no helper at $SRC"; exit 1; }

PASS=0; FAILN=0; CTLOK=0; CTLBAD=0

# ---- the deck userland ----------------------------------------------------------
mkdir -p /ov /deck && mount -t tmpfs ov /ov && mkdir -p /ov/u /ov/w
mount -t overlay overlay -o lowerdir=/lower,upperdir=/ov/u,workdir=/ov/w /deck || { echo "FAIL overlay mount"; exit 1; }
mount --bind /dev /deck/dev; mount -t proc proc /deck/proc
mount -t tmpfs tmp /deck/tmp; mount -t tmpfs run /deck/run
mkdir -p /deck/media/usb && mount -t tmpfs usb /deck/media/usb
ID=3fa2c41b9e07; TOK=6c0d1f2e3a4b5c6d7e8f901a2b3c4d5e; ID2=91be0a55c3d2; TOK2=e47a20aa00bb11cc22dd33ee44ff5566
cat > /deck/usr/bin/avahi-browse <<EOF
#!/bin/sh
echo '=;lo;IPv4;AZ-MODS on fixture 3fa2;_azstems._tcp;local;azmods-$ID.local;127.0.0.1;8766;"v=1" "id=$ID" "ips=127.0.0.1"'
EOF
chmod +x /deck/usr/bin/avahi-browse

# ---- the fixture ------------------------------------------------------------------
FX=/srv/rs; mkdir -p $FX
python3 /t/remotestems_fixture.py serve $FX 8766 & SRV=$!
HPID=
cleanup() { [ -n "$HPID" ] && kill "$HPID" 2>/dev/null; kill "$SRV" 2>/dev/null; }
trap cleanup EXIT
for _ in 1 2 3 4 5 6 7 8 9 10; do
    python3 -c 'import socket; socket.create_connection(("127.0.0.1", 8766), 1)' 2>/dev/null && break; sleep 0.5
done
cnt() { python3 /t/remotestems_fixture.py count $FX "$1"; }
EPOCH=0
# The fixture resets its counters on the first request of a new epoch; until then
# counts.json still holds the last case's, so it goes too (a wait on a counter would
# otherwise end at once on a stale value and could hide a control's failure).
setfx() { EPOCH=$((EPOCH + 1)); rm -f $FX/ctl.json $FX/counts.json $FX/post.last; python3 /t/remotestems_fixture.py ctl $FX epoch=$EPOCH "$@"; }

# ---- the variants: the shipped helper, and one control per fix -----------------------
mkdir -p /v
cp "$SRC" /v/real.sh
mutate() {   # <name> <python: s = s.replace(...)>, refused when nothing changed
    python3 - "$SRC" "/v/$1.sh" "$2" <<'PY' || { echo "FAIL mutation $1 did not apply (did azrs.sh change?)"; exit 1; }
import sys
s = open(sys.argv[1]).read(); o = s
exec(sys.argv[3])
if s == o: sys.exit(1)
open(sys.argv[2], "w").write(s)
PY
}
if [ -z "${AZRS_UNDER_TEST:-}" ]; then
# fix 1 reverted: no state check before the PUT, and a 409 is retried like a timeout
mutate ctl-409 's = s.replace("if [ \"$(kv \"$R\" up)\" != 1 ] && [ -z \"$fresh\" ]; then", "if false; then").replace("            409) st=$(job_state", "            409x) st=$(job_state")'
# fix 2 reverted: any status but 200/201 drops the request
mutate ctl-post 's = s.replace("            200|201) ;;\n            # the computer no longer knows", "            200|201) ;;\n            *) alert \"Stems failed on $pc: $(jget \"$r\" error)\"; return 1 ;;\n            # the computer no longer knows")'
# fix 3 reverted: no banner marker (an older azmod judged the wording, which these miss)
mutate ctl-mark 's = s.replace("printf '"'"'%s\\nbanner=1\\n'"'"'", "printf '"'"'%s\\nbanner=0\\n'"'"'")'
# fix 4 reverted: a 401 from /v2/hello is not told apart from no answer
mutate ctl-401 's = s.replace("if [ \"$(jget \"$h\" v)\" = 2 ]; then : > \"$D/rejected.$1\"; else : > \"$D/oldapp.$1\"; fi; return", "return")'
# wire version 2 reverted, part 1: the app's proof is not checked (any device that
# advertises the id is believed)
mutate ctl-noproof 's = s.replace("if [ -z \"$want\" ] || [ \"$want\" != \"$pf\" ]; then", "if false; then")'
# wire version 2 reverted, part 2: the token goes out with the hello, as version 1 sent it
# with every request (the "token never crosses the network" check must see it)
mutate ctl-bearer 's = s.replace("h=$(curl -sS $4 -m 4 -w", "h=$(curl -sS $4 -m 4 -H \"Authorization: Bearer $2\" -w")'
# the source.json check reverted: each folder's file pasted into the body as it is
mutate ctl-rawsrc 's = s.replace("src=$(json_obj \"$e/source.json\")", "src=$(tr -d '"'"'\\n\\r'"'"' < \"$e/source.json\")")'
fi

# ---- one case ---------------------------------------------------------------------
M=/deck/media/usb/sda1; Q=$M/MODS/azstems-queue; REQ=$Q/1767225600123004-812.req
STATUS=/deck/tmp/azrs/status
stick() {   # [pair: 1 key | 2 keys | none]
    rm -rf $M; mkdir -p "$M/Contents/Artist" $Q
    head -c 50000 /dev/urandom > "$M/Contents/Artist/Artist - Song.mp3"
    printf 'file=/Contents/Artist/Artist - Song.mp3\ntitle=Song\n' > $REQ
    case "${1:-1}" in
        1) printf '{"id": "%s", "token": "%s", "keys": [{"id": "%s", "token": "%s", "at": 1}]}' $ID $TOK $ID $TOK > $M/MODS/azstems.pair ;;
        2) printf '{"id": "%s", "token": "%s", "keys": [{"id": "%s", "token": "%s", "at": 2}, {"id": "%s", "token": "%s", "at": 1}]}' \
               $ID2 $TOK2 $ID2 $TOK2 $ID $TOK > $M/MODS/azstems.pair ;;
    esac
}
start() {   # $1 variant: the helper runs as on the deck, from a clean /tmp/azrs
    stop; rm -rf /deck/tmp/azrs; cp /v/"$1".sh /deck/tmp/azrs.sh
    # shellcheck disable=SC2086 # the shell is a command and its argument ("/bin/busybox ash")
    chroot /deck /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin AZRS_IDLE=60 $SHC /tmp/azrs.sh & HPID=$!
}
stop() {
    [ -n "$HPID" ] || return 0
    kill "$HPID" 2>/dev/null; wait "$HPID" 2>/dev/null; HPID=
    for p in $(pgrep -f /tmp/azrs.sh); do kill "$p" 2>/dev/null; done
}
until_() {   # <seconds> <condition>: 0 once it holds, 1 when time runs out
    n=$(($1 * 2)); while [ $n -gt 0 ]; do eval "$2" && return 0; sleep 0.5; n=$((n - 1)); done; return 1
}
ready() { [ ! -f $REQ ] && [ -f "$M/STEMS/Artist - Song/vocals.ogg" ] && grep -q "^Stems ready: Song" $STATUS 2>/dev/null; }
line1() { head -n1 $STATUS 2>/dev/null; }
marked() { [ "$(sed -n 2p $STATUS 2>/dev/null)" = banner=1 ]; }
log_has() { grep -q "$1" /deck/tmp/azrs/log 2>/dev/null; }

# Each case: set the fixture, run variant $V, check. R collects this case's result for the
# controls: chk records PASS/FAIL normally, and under a control a FAIL is what we want.
chk() {
    if eval "$2"; then r=PASS; else r=FAIL; fi
    if [ "$V" = real ]; then
        echo "$r [$SHN] $1"; [ $r = PASS ] && PASS=$((PASS+1)) || FAILN=$((FAILN+1))
    else
        [ $r = FAIL ] && CFAIL=$((CFAIL+1))
    fi
}

case_lost_put() {
    echo "== [$SHN/$V] fix 1: the PUT reaches the computer but its response is lost"
    stick 1; setfx put=lost; start $V
    chk "the track finishes: STEMS/<folder> complete, request removed, 'Stems ready'" 'until_ 40 ready'
    chk "the audio was sent once, then the job state was read (no PUT retry loop)" '[ "$(cnt puts)" = 1 ]'
}
case_dedupe() {
    echo "== [$SHN/$V] fix 1: POST returns an existing job already past waiting_upload, no up=1 in the .req"
    stick 1; setfx "seed=Contents/Artist/Artist - Song.mp3:queued"; start $V
    chk "the track finishes without sending the audio again" 'until_ 40 ready && [ "$(cnt puts)" = 0 ]'
    chk "no 409 from the server" '[ "$(cnt put409)" = 0 ]'
}
case_post_transient() {   # $1 close | 503
    echo "== [$SHN/$V] fix 2: POST answers $1 (close: no response at all, what a timeout gives curl: code 000)"
    stick 1; setfx post="$1"; start $V
    chk "the request survives two failed POSTs" 'until_ 30 "[ \$(cnt posts) -ge 2 ]" && [ -f $REQ ] && ! grep -q "^job=" $REQ'
    chk "nothing in the banner while it retries" '! marked'
    python3 /t/remotestems_fixture.py ctl $FX post=ok
    chk "once the computer answers, the track finishes" 'until_ 40 ready'
}
case_post_401() {
    echo "== [$SHN/$V] fix 2: POST answers 401 (the computer forgot this stick's token)"
    stick 1; setfx post=401; start $V
    chk "'Not paired with Fixture PC' in the banner" 'until_ 20 "line1 | grep -q \"^Not paired with Fixture PC: plug this USB into AZ-MODS once\"" && marked'
    chk "the request is kept (for after pairing again)" '[ -f $REQ ]'
}
case_post_400() {
    echo "== [$SHN/$V] fix 2: POST answers 400 (a definite rejection of this track)"
    stick 1; setfx post=400; start $V
    chk "the request is dropped" 'until_ 20 "[ ! -f $REQ ]"'
    chk "with the server's reason in the banner" 'until_ 5 "line1 | grep -q \"^Stems failed on Fixture PC: fixture says 400\"" && marked'
}
case_existing() {
    echo "== [$SHN/$V] existing STEMS folders: one with a valid source.json, one with a cut-off one"
    stick 1; mkdir -p "$M/STEMS/Artist - Song" "$M/STEMS/Artist - Song_"
    printf '{"file": "Contents/Other/Artist - Song.mp3", "size": 7, "mtime": 1, "fp": "fnv1a64:0123456789abcdef"}\n' \
        > "$M/STEMS/Artist - Song/source.json"
    printf '{"file": "Contents/Artist/Artist - Song.mp3", "si' > "$M/STEMS/Artist - Song_/source.json"
    setfx; start $V
    chk "the POST body is valid JSON (the computer accepts it)" 'until_ 20 "python3 /t/remotestems_fixture.py existing $FX >/dev/null 2>&1"'
    chk "both folders listed: the valid one with its source, the cut-off one with none" \
        '[ "$(python3 /t/remotestems_fixture.py existing $FX | sort)" = "$(printf "Artist - Song_|-\nArtist - Song|Contents/Other/Artist - Song.mp3" | sort)" ]'
    chk "the track finishes" 'until_ 40 ready'
}
case_health_401() {
    echo "== [$SHN/$V] fix 4: /v2/hello answers 401 (reachable, this USB unknown)"
    stick 1; setfx hello=401; start $V
    chk "'Not paired with ...' in the banner, not 'not found'" 'until_ 20 "[ -s $STATUS ]" && line1 | grep -q "^Not paired with AZ-MODS: plug this USB into AZ-MODS once" && marked'
    chk "nothing was sent" '[ "$(cnt posts)" = 0 ] && [ -f $REQ ]'
}
# wire version 2 -------------------------------------------------------------------
case_impostor() {
    echo "== [$SHN/$V] v2: a device advertises the pairing id but does not hold the token"
    stick 1; setfx hello=impostor; start $V
    chk "it is refused: the deck logs that it cannot prove the key" 'until_ 20 "log_has \"cannot prove it holds this USB.s key\""'
    chk "nothing was sent to it, the request is kept" '[ "$(cnt posts)" = 0 ] && [ "$(cnt puts)" = 0 ] && [ -f $REQ ]'
    chk "the banner says the computer is not found (the impostor is not it)" 'until_ 25 "line1 | grep -q \"not found on the network\$\""'
}
case_replay() {
    echo "== [$SHN/$V] v2: a recorded genuine answer is sent again (a replay)"
    stick 1; setfx; start $V
    until_ 40 ready >/dev/null                       # a genuine exchange to record
    stop; stick 1; setfx hello=replay; start $V
    chk "the replayed answer is refused (the deck's nonce is new each time)" 'until_ 20 "log_has \"cannot prove it holds this USB.s key\""'
    chk "nothing was sent" '[ "$(cnt posts)" = 0 ] && [ -f $REQ ]'
}
case_oldapp() {
    echo "== [$SHN/$V] v2: an AZ-MODS from before version 2 (no hello)"
    stick 1; setfx hello=old; start $V
    chk "'Update AZ-MODS ... to use Remote Stems with this deck', in the banner" 'until_ 25 "line1 | grep -q \"^Update AZ-MODS.* to use Remote Stems with this deck\$\"" && marked'
    chk "nothing was sent, the request is kept" '[ "$(cnt posts)" = 0 ] && [ -f $REQ ]'
}
case_signed() {
    echo "== [$SHN/$V] v2: every request after the hello is signed and accepted"
    stick 1; setfx; start $V
    chk "the track finishes" 'until_ 40 ready'
    chk "no request was refused for its signature" '[ "$(cnt badauth)" = 0 ]'
}
case_unreachable() {   # the same round's other answer: no computer answers at all
    echo "== [$SHN/$V] fix 3+4: no computer on the network (one key, then two keys)"
    kill -STOP $SRV
    stick 1; start $V
    chk "one key: '<computer> not found on the network', in the banner" 'until_ 25 "line1 | grep -q \"^AZ-MODS not found on the network\$\"" && marked'
    stick 2; start $V
    chk "two keys: 'None of this USB's computers are on the network', in the banner" 'until_ 40 "line1 | grep -q \"^None of this USB.s computers are on the network\$\"" && marked'
    stop; kill -CONT $SRV
}
case_unpaired() {
    echo "== [$SHN/$V] fix 3: a queued request on a stick with no pair file"
    stick none; start $V
    chk "'Plug this USB into AZ-MODS once', in the banner" 'until_ 15 "line1 | grep -q \"^Plug this USB into AZ-MODS once, with Remote Stems on\$\"" && marked'
}
case_happy() {
    echo "== [$SHN/$V] the plain path (CONTROL for the fault cases: the fixture and harness work)"
    stick 1; setfx; start $V
    chk "the track finishes" 'until_ 40 ready'
    chk "one POST, one PUT, the job deleted at the end" '[ "$(cnt posts)" = 1 ] && [ "$(cnt puts)" = 1 ] && [ "$(cnt deletes)" = 1 ]'
    chk "progress lines carry banner=0" 'grep -q "^Stems ready: Song" $STATUS && [ "$(sed -n 2p $STATUS)" = banner=0 ]'
}

case_token_wire() {   # the global "never on the wire" check, as one case for the control
    stick 1; setfx; start "$V"
    until_ 40 ready >/dev/null                   # a whole exchange: hello, POST, PUT, polls, files
    chk "the pairing token never crossed the network" '[ "$(python3 /t/remotestems_fixture.py wire $FX $TOK)" = 0 ]'
}

# control <variant> <cases...>: the cases run against the mutated helper; it must fail one
control() {
    v=$1; shift; V=$v; CFAIL=0
    for c in "$@"; do $c >/dev/null; done
    stop
    if [ $CFAIL -gt 0 ]; then echo "PASS [$SHN] CONTROL $v fails as required ($CFAIL checks failed)"; CTLOK=$((CTLOK+1))
    else echo "FAIL [$SHN] CONTROL $v PASSED: the cases cannot see this bug"; CTLBAD=$((CTLBAD+1)); fi
}

for SHC in "/bin/busybox ash" "/bin/sh"; do
    SHN=${SHC##*/}; SHN=${SHN%% *}; [ "$SHN" = busybox ] && SHN=ash
    V=real
    case_happy; case_lost_put; case_dedupe; case_post_transient close; case_post_transient 503
    case_post_401; case_post_400; case_health_401; case_unreachable; case_unpaired
    case_signed; case_impostor; case_replay; case_oldapp; case_existing
    stop
    # over every exchange above, impostor and replay included
    chk "the pairing tokens never crossed the network" '[ "$(python3 /t/remotestems_fixture.py wire $FX $TOK)" = 0 ] && [ "$(python3 /t/remotestems_fixture.py wire $FX $TOK2)" = 0 ]'
    chk "...and the wire log is real (it holds the signed requests)" '[ "$(python3 /t/remotestems_fixture.py wire $FX X-AZ-Auth)" -gt 0 ]' 
    if [ -z "${AZRS_UNDER_TEST:-}" ]; then
        control ctl-409  case_lost_put
        control ctl-409  case_dedupe
        control ctl-post "case_post_transient close"
        control ctl-post "case_post_transient 503"
        control ctl-post case_post_401
        control ctl-401  case_health_401
        control ctl-mark case_unreachable
        control ctl-mark case_unpaired
        control ctl-noproof case_impostor
        control ctl-noproof case_replay
        control ctl-rawsrc  case_existing
        rm -f $FX/wire.log; control ctl-bearer case_token_wire; rm -f $FX/wire.log   # the control put the token there
    fi
done

echo "== totals: $PASS passed, $FAILN failed; controls: $CTLOK failed as required, $CTLBAD passed"
[ $FAILN = 0 ] && [ $CTLBAD = 0 ]
