#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Deterministic unit test of the crash-loop guard in az-usbload. Run directly, no
# container: `sh test/clguard.sh [staged-az-usbload]` (default: the payload source).
# Stubs systemctl (configurable NRestarts / is-failed), EP147 (pidof+HuiProcessor),
# the stick (a temp MODS dir), and ENVF; sources only the guard functions from the
# loader; drives the guard's three scenarios: (a) a healthy set never trips it, (b) a
# crash-looping set trips it and stays stock, (c) a changed set loads again. Fully
# deterministic.
set -u
A="${1:-$(cd "$(dirname "$0")/../payload/usr/sbin" && pwd)/az-usbload}"
T=$(mktemp -d); MODS="$T/MODS"; ENVF="$T/az-preload.env"; LOG=/dev/null; CLMARK=.mods-disabled-crashloop
mkdir -p "$MODS"; export T MODS ENVF LOG
pass=0; fail=0
ok(){ if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

# --- stubs the guard calls ---
log(){ :; }
# stick_write $1 $2 : write/remove MODS/$1 (matches the loader's helper contract)
stick_write(){ if [ -n "${2:-}" ]; then printf '%s\n' "$2" > "$MODS/$1"; else rm -f "$MODS/$1"; fi; }
# systemctl stub driven by env: SC_FAILED (is-failed rc), SC_NREST (NRestarts), records calls
SCCALLS="$T/sc.calls"; : > "$SCCALLS"
systemctl(){
  case "$1 $2" in
    "is-failed --quiet") return "${SC_FAILED:-1}";;
    "show EP147.service") echo "${SC_NREST:-0}";;
    "reset-failed EP147.service") echo reset-failed >> "$SCCALLS";;
    "restart EP147.service") echo restart >> "$SCCALLS";;
  esac; return 0
}
pidof(){ echo 1234; }
# source the guard functions (mod_sig, ep_nrestarts, ep_hui, clguard_trip) from the loader
eval "$(grep -E '^(mod_sig|ep_nrestarts|ep_hui)\(\) \{' "$A"; sed -n '/^clguard_trip() {/,/^}/p' "$A")"
ep_hui(){ [ "${EP_HUI:-0}" = 1 ]; }   # replaces the /proc one: EP_HUI=1 means HuiProcessor is up

echo "== mod_sig discriminates sets (scenario (c) basis) =="
echo aaa > "$MODS/azmod.so"; s1=$(mod_sig)
echo bbb > "$MODS/azmod.so"; s2=$(mod_sig)
ok "sig changes when the .so changes" '[ "$s1" != "$s2" ] && [ -n "$s1" ] && [ -n "$s2" ]'
echo aaa > "$MODS/azmod.so"; s3=$(mod_sig)
ok "sig stable for identical bytes" '[ "$s1" = "$s3" ]'

echo "== (b) clguard_trip clears the preload, marks the set, restarts stock =="
printf 'LD_PRELOAD=/x/azmod.so\n' > "$ENVF"; rm -f "$MODS/$CLMARK" 2>/dev/null; : > "$SCCALLS"
clguard_trip "test crash loop"
ok "env cleared to stock" '[ ! -s "$ENVF" ]'
ok "marker written = current sig" '[ "$(cat "$MODS/.mods-disabled-crashloop")" = "$(mod_sig)" ]'
ok "EP147 reset-failed + restarted" 'grep -q reset-failed "$SCCALLS" && grep -q restart "$SCCALLS"'

echo "== pre-emptive check: marked set with matching sig -> WANT stock =="
# replicate the loader's pre-emptive snippet
WANT="LD_PRELOAD=/x/azmod.so"
if [ -n "$WANT" ] && [ -f "$MODS/$CLMARK" ] && [ "$(cat "$MODS/$CLMARK" 2>/dev/null)" = "$(mod_sig)" ]; then WANT=""; fi
ok "(b) boot 2: matching mark -> WANT emptied (stock)" '[ -z "$WANT" ]'

echo "== (c) set changed (new .so bytes) -> mark no longer matches -> mods load =="
echo ccc > "$MODS/azmod.so"      # new build: sig changes, old marker stays
WANT="LD_PRELOAD=/x/azmod.so"
if [ -n "$WANT" ] && [ -f "$MODS/$CLMARK" ] && [ "$(cat "$MODS/$CLMARK" 2>/dev/null)" = "$(mod_sig)" ]; then WANT=""; fi
ok "(c) new set -> WANT kept (mods load, guard does not fire)" '[ -n "$WANT" ]'

echo "== (a) CONTROL: healthy set, is-failed false -> is-failed branch does NOT trip =="
: > "$SCCALLS"; SC_FAILED=1   # is-failed returns 1 (NOT failed)
WANT="LD_PRELOAD=/x/azmod.so"
if systemctl is-failed --quiet EP147.service 2>/dev/null; then
  [ -n "$WANT" ] && { clguard_trip "should not happen"; }
fi
ok "(a) healthy: no trip (no reset-failed/restart from guard)" '[ ! -s "$SCCALLS" ]'

echo "== is-failed backstop: failed + non-stock WANT -> trips; failed + stock -> does not =="
: > "$SCCALLS"; SC_FAILED=0; WANT="LD_PRELOAD=/x/azmod.so"; printf 'LD_PRELOAD=/x\n' > "$ENVF"
if systemctl is-failed --quiet EP147.service 2>/dev/null; then [ -n "$WANT" ] && clguard_trip "failed+nonstock"; fi
ok "backstop trips on failed+non-stock" 'grep -q restart "$SCCALLS"'
: > "$SCCALLS"; SC_FAILED=0; WANT=""
if systemctl is-failed --quiet EP147.service 2>/dev/null; then [ -n "$WANT" ] && clguard_trip "should not"; fi
ok "backstop does NOT trip when WANT is stock" '[ ! -s "$SCCALLS" ]'

echo "== early watch: NRestarts climbs >=N without HuiProcessor -> trip; healthy -> no trip =="
# crash: NRestarts rises, no HuiProcessor
: > "$SCCALLS"; EP_HUI=0; CLGUARD_N=3
base=5
tripped=no; n=9  # n-base = 4 >= 3
if [ "$((n - base))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "watch crash"; tripped=yes; fi
ok "watch trips on >=N restarts, no HuiProcessor" '[ "$tripped" = yes ] && grep -q restart "$SCCALLS"'
# healthy: no new restarts, HuiProcessor up
: > "$SCCALLS"; EP_HUI=1; n=5
if [ "$((n - base))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "should not"; fi
ok "watch does NOT trip when healthy" '[ ! -s "$SCCALLS" ]'
# edge: restarts climbing but HuiProcessor IS up (slow healthy restart) -> no trip
: > "$SCCALLS"; EP_HUI=1; n=9
if [ "$((n - base))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "should not"; fi
ok "watch does NOT trip if HuiProcessor is up (even with restarts)" '[ ! -s "$SCCALLS" ]'

echo "== clear-on-healthy: a non-stock set that comes up healthy clears a stale mark =="
echo zzz > "$MODS/azmod.so"; stick_write "$CLMARK" "oldsig"; WANT="LD_PRELOAD=/x"
[ -n "$WANT" ] && stick_write "$CLMARK" ""
ok "healthy non-stock boot clears the marker" '[ ! -f "$MODS/$CLMARK" ]'

echo "== in-wait trip (the placement fix): crashes >=N before HuiProcessor -> trip DURING the wait =="
# models the post-restart wait-loop decision: WANT non-stock, _crb baseline, climb to _crb+N, no HuiProcessor
echo zzz > "$MODS/azmod.so"; printf 'LD_PRELOAD=/x\n' > "$ENVF"; rm -f "$MODS/$CLMARK"; : > "$SCCALLS"
WANT="LD_PRELOAD=/x/azmod.so"; _crb=5; EP_HUI=0; TRIPPED=""
_cn=8                                  # 8-5 = 3 crashes >= N(3)
if [ -n "$WANT" ] && [ "$(( _cn - _crb ))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "in-wait crash loop"; TRIPPED=1; fi
ok "in-wait trips after N crashes, no HuiProcessor" '[ -n "$TRIPPED" ] && [ ! -s "$ENVF" ] && grep -q restart "$SCCALLS"'
ok "in-wait trip wrote the marker" '[ "$(cat "$MODS/$CLMARK" 2>/dev/null)" = "$(mod_sig)" ]'
: > "$SCCALLS"; EP_HUI=1; _cn=8; TRIPPED=""   # HuiProcessor up -> no trip
if [ -n "$WANT" ] && [ "$(( _cn - _crb ))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "should not"; TRIPPED=1; fi
ok "in-wait does NOT trip once HuiProcessor is up" '[ -z "$TRIPPED" ] && [ ! -s "$SCCALLS" ]'
: > "$SCCALLS"; EP_HUI=0; _cn=7; TRIPPED=""   # 2 < N -> keep waiting
if [ -n "$WANT" ] && [ "$(( _cn - _crb ))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "should not"; TRIPPED=1; fi
ok "in-wait does NOT trip below N crashes" '[ -z "$TRIPPED" ] && [ ! -s "$SCCALLS" ]'
: > "$SCCALLS"; EP_HUI=0; _cn=20; WANT=""; TRIPPED=""   # stock set -> never
if [ -n "$WANT" ] && [ "$(( _cn - _crb ))" -ge "$CLGUARD_N" ] && ! ep_hui; then clguard_trip "should not"; TRIPPED=1; fi
ok "in-wait does NOT trip for a stock set" '[ -z "$TRIPPED" ] && [ ! -s "$SCCALLS" ]'

rm -rf "$T"
echo "=== clguard logic: PASS=$pass FAIL=$fail ==="
[ "$fail" = 0 ]
