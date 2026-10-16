#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# azrs: the Remote Stems helper (deck side). Runs as its own systemd unit (azrs),
# outside EP147, so an EP147 restart never kills a transfer. azmod starts it when a
# MENU > MAKE STEMS request is queued (and at EP147 start if a queue is not
# empty); it exits after AZRS_IDLE seconds with nothing to do.
#
# Queue: <stick>/MODS/azstems-queue/<n>.req, written by azmod, one per track:
#   file=<path relative to the stick, leading />   (from the library row)
# and extended here with job=<id> / up=1 as the job advances. The stick is
# the source of truth: the queue travels with the USB and survives reboots.
#
# Pairing: <stick>/MODS/azstems.pair {"id", "token", "keys": [{"id", "token", "at"}...]},
# written by every AZ-MODS that sees the USB with Remote Stems on: one key per
# computer, newest first (the computer the USB was plugged into last), the rest
# fallbacks. An older file has only the top-level id/token: a list of one. Each app
# is found by mDNS (_azstems._tcp, TXT id=<id>) and must prove it holds this stick's
# token before anything is sent (wire version 2): the deck asks GET /v2/hello with the
# token's key id and a fresh nonce, checks the app's HMAC proof with its own token, and
# signs every request with a session key both sides derive from the two nonces. The
# token itself never leaves the stick, so a device that advertises the pairing id (it
# is public in the TXT record) learns nothing from it. A new request goes to the newest
# key whose app is on the network; a request already sent stays with its app (srv= in
# the .req) while that app is there, and is sent again to another one when it is not.
#
# Stems land in <stick>/STEMS/.<folder>.part and appear with one rename; an
# existing target is swapped (<folder> -> .<folder>.old), never merged.
# Names beginning with "." never match a stems key, so azstem cannot load a
# half-written folder. Leftover .part/.old folders are swept at start.
#
# Status for azmod's banner: /tmp/azrs/status, replaced atomically: line 1 the message,
# line 2 "banner=1" when it reports a problem the rows cannot show (azmod puts it in
# EP147's top strip) or "banner=0".

D=/tmp/azrs; LOG=$D/log; mkdir -p $D
AZRS_IDLE=${AZRS_IDLE:-120}
# /tmp is RAM: past AZRS_LOG_MAX bytes the log moves to log.1 (the previous .1 is
# dropped), checked every 32 lines so a line costs no extra process
AZRS_LOG_MAX=${AZRS_LOG_MAX:-262144}; AZRS_LOG_N=0
log(){ AZRS_LOG_N=$((AZRS_LOG_N + 1))
       if [ $((AZRS_LOG_N % 32)) = 1 ] && [ -f $LOG ] && [ "$(wc -c < $LOG)" -gt "$AZRS_LOG_MAX" ]; then mv -f $LOG $LOG.1; fi
       echo "$(date +%H:%M:%S) $*" >> $LOG; }
# only on a change: azmod shows the line in EP147's top strip whenever the file
# changes, so rewriting the same text every poll would re-pop the banner every 3 s
status(){ [ "$(printf '%s\nbanner=0' "$1")" = "$(cat $D/status 2>/dev/null)" ] && return
          printf '%s\nbanner=0\n' "$1" > $D/status.tmp && mv $D/status.tmp $D/status; log "STATUS $1"; }
# a problem the rows cannot show: azmod puts it in the banner strip
alert(){ [ "$(printf '%s\nbanner=1' "$1")" = "$(cat $D/status 2>/dev/null)" ] && return
         printf '%s\nbanner=1\n' "$1" > $D/status.tmp && mv $D/status.tmp $D/status; log "ALERT $1"; }
# the computer answered but does not know this stick's token (401)
not_paired(){ alert "Not paired with $1: plug this USB into AZ-MODS once, with Remote Stems on"; }
# one flat JSON value (string or bare token) for key $2 out of text $1
jget(){ printf '%s' "$1" | tr -d '\n' | sed -n -e "s/.*\"$2\"[ ]*:[ ]*\"\([^\"]*\)\".*/\1/p" -e t \
        -e "s/.*\"$2\"[ ]*:[ ]*\([-0-9a-z.]*\).*/\1/p" | head -1; }
jesc(){ printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }
kv(){ sed -n "s/^$2=//p" "$1" | head -1; }                 # a field of a .req file
# the pair file's keys, newest first, one "id token" per line (the top level repeats the first)
pair_keys(){ printf '%s' "$1" | grep -o '"id": *"[^"]*", *"token": *"[^"]*"' |
             sed 's/"id": *"\([^"]*\)", *"token": *"\([^"]*\)"/\1 \2/' | awk '!s[$1]++'; }
# never recreates a request that is gone: MENU > CANCEL STEMS renames it to .cancel
# while a step may still hold its old name (an upload can take minutes)
setkv(){ [ -f "$1" ] || return 1; grep -v "^$2=" "$1" > "$1.tmp"; echo "$2=$3" >> "$1.tmp"; mv "$1.tmp" "$1"; }

# ---- the stick's STEMS folder (FAT is case-insensitive: take the existing one)
stems_dir(){
    local e
    for e in "$1"/*; do
        [ -d "$e" ] || continue
        [ "$(basename "$e" | tr a-z A-Z)" = STEMS ] && { echo "$e"; return; }
    done
    echo "$1/STEMS"
}

# ---- one status per pass: with several jobs, each writing its own line would make
# the deck's banner flip between them every 3 s. Ongoing states are
# candidates (the highest wins: running > sending > waiting); one-off events
# (ready, failed) still go out at once through status().
PST=; PPRIO=0
cand(){ [ "$1" -gt "$PPRIO" ] && { PPRIO=$1; PST=$2; }; }

# ---- the job in flight, for azmod's row animation: "cid=<content_id> pct=<n>"
# ph: up (sending the track), st (on the computer: waiting or being stemmed), dn (the
# stems coming back). azmod animates each differently.
progress(){ printf 'cid=%s pct=%s ph=%s\n' "$1" "${2:-0}" "${3:-st}" > $D/job.tmp && mv $D/job.tmp $D/job; jobs_set "$@"; }
# $D/jobs: one line per track in flight, "<cid> <pct> <ph>" (ph up | wt | st | dn). The
# single job file above names only the track stepped last, and a playlist steps several
# (one uploads while another is stemmed), so it would flip between them and every other
# track would read as queued. Each track's phase comes from its own step, so it only
# moves forward.
jobs_set(){ { [ -f $D/jobs ] && awk -v c="$1" '$1 != c' $D/jobs; echo "$1 ${2:-0} ${3:-st}"; } > $D/jobs.tmp && mv $D/jobs.tmp $D/jobs; }
jobs_rm(){ [ -f $D/jobs ] || return 0; awk -v c="$1" '$1 != c' $D/jobs > $D/jobs.tmp && mv $D/jobs.tmp $D/jobs; }

# ---- the computer's name for the banners (/v1/health "name", optional)
remember_name(){ local n; n=$(jget "$2" name); [ -n "$n" ] && [ "$n" != null ] && printf '%s' "$n" | cut -c1-40 > "$D/name.$1"; }
pc_name(){ cat "$D/name.$1" 2>/dev/null || echo AZ-MODS; }

# ---- authentication (wire version 2): proof of the token, never the token
# HMAC-SHA-256 of stdin, hex; $1 is "key:<text>" or "hexkey:<hex>" (openssl 1.1 and 3 print
# "...= <hex>")
hm(){ openssl dgst -sha256 -mac HMAC -macopt "$1" 2>/dev/null | sed 's/^.*= *//'; }
hex32(){ case "$1" in *[!0-9a-f]*|"") return 1 ;; esac; [ ${#1} = "$2" ]; }
# GET /v2/hello: the app's answer body, or nothing. $D/rejected.<id>: the app is there but
# does not know this USB (401 from a version-2 app). $D/oldapp.<id>: an app that predates
# version 2 answered. $D/sess.<id>: "<nonce_s> <session key>", $D/ctr.<id>: the last counter.
hello(){   # $1 id  $2 token  $3 host:port  $4 extra curl options
    local h code nd ns pf want
    nd=$(openssl rand -hex 16 2>/dev/null); hex32 "$nd" 32 || { log "no openssl rand: cannot authenticate"; return; }
    h=$(curl -sS $4 -m 4 -w '\n%{http_code}' \
          "http://$3/v2/hello?kid=$(printf '%s' "$2" | sha256sum | cut -c1-16)&nd=$nd" 2>/dev/null)
    code=$(printf '%s' "$h" | tail -n1); h=$(printf '%s' "$h" | sed '$d')
    case "$code" in
        200) ;;
        401|404) if [ "$(jget "$h" v)" = 2 ]; then : > "$D/rejected.$1"; else : > "$D/oldapp.$1"; fi; return ;;
        *) return ;;
    esac
    [ "$(jget "$h" id)" = "$1" ] || return
    ns=$(jget "$h" ns); pf=$(jget "$h" proof)
    hex32 "$ns" 32 && hex32 "$pf" 64 || { log "REFUSED $3: a malformed answer"; return; }
    want=$(printf 'azrs-srv\n%s\n%s\n%s' "$1" "$nd" "$ns" | hm "key:$2")
    if [ -z "$want" ] || [ "$want" != "$pf" ]; then
        log "REFUSED $3: it claims to be $1 but cannot prove it holds this USB's key (not the paired AZ-MODS)"
        return
    fi
    printf '%s %s\n' "$ns" "$(printf 'azrs-ses\n%s\n%s' "$nd" "$ns" | hm "key:$2")" > "$D/sess.$1"
    echo 0 > "$D/ctr.$1"
    printf '%s' "$h"
}
# the X-AZ-Auth header for one request: each call uses the next counter, so it is built
# right before its curl. $1 id  $2 method  $3 path (as in the URL)  $4 Content-Length ("" if none)
az(){
    local ns k c s
    read -r ns k < "$D/sess.$1" 2>/dev/null || return 1
    c=$(( $(cat "$D/ctr.$1" 2>/dev/null || echo 0) + 1 )); echo $c > "$D/ctr.$1"
    s=$(printf '%s\n%s\n%s\n%s' "$2" "$3" "$c" "$4" | hm "hexkey:$k")
    printf 'X-AZ-Auth: ns=%s ctr=%s sig=%s' "$ns" "$c" "$s"
}
server_for(){   # $1 id  $2 token -> "host:port" or nothing
    local c hp h tried=
    c="$D/server.$1"; rm -f "$D/rejected.$1" "$D/oldapp.$1"
    if [ -f "$c" ]; then
        hp=$(cat "$c")
        h=$(hello "$1" "$2" "$hp")
        [ "$(jget "$h" id)" = "$1" ] && { remember_name "$1" "$h"; echo "$hp"; return; }
        rm -f "$c"
    fi
    # The record resolves to one address per interface, and on a PC with WSL/Hyper-V
    # that can be an unreachable vEthernet one (172.x). AZ-MODS also lists every
    # address in TXT "ips="; try the resolved one, then each of those.
    avahi-browse -rtp _azstems._tcp 2>/dev/null | grep '^=' | grep -F "\"id=$1\"" |
    while IFS=';' read -r _ _ proto _ _ _ _ addr port txt; do
        [ "$proto" = IPv4 ] || continue
        ips=$(printf '%s' "$txt" | sed -n 's/.*"ips=\([0-9.,]*\)".*/\1/p' | tr ',' ' ')
        hit=
        for a in $addr $ips; do
            case " $tried " in *" $a "*) continue ;; esac
            tried="$tried $a"
            h=$(hello "$1" "$2" "$a:$port" "--connect-timeout 2")
            if [ "$(jget "$h" id)" = "$1" ]; then hit="$a:$port"; break; fi
        done
        if [ -n "$hit" ]; then remember_name "$1" "$h"; echo "$hit" > "$c"; echo "$hit"; break; fi
    done
}

# ---- file $1 on one line when it holds one valid JSON object, else nothing.
# Strings must be printable ASCII (the app writes ASCII-escaped JSON); at most 64 KiB
# and 32 levels of nesting.
json_obj(){ awk '
function ws() { while (substr(s, p, 1) == " " || substr(s, p, 1) == "\t") p++ }
function str(   c) {
    if (substr(s, p, 1) != "\"") return 0
    for (p++; p <= n; p++) {
        c = substr(s, p, 1)
        if (c == "\"") { p++; return 1 }
        if (c == "\\") {
            c = substr(s, ++p, 1)
            if (c == "u") {
                if (substr(s, p + 1, 4) !~ /^[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]$/) return 0
                p += 4
            } else if (c == "" || index("\"\\/bfnrt", c) == 0) return 0
        } else if (c < " " || c > "~") return 0
    }
    return 0
}
function num() {
    if (!match(substr(s, p), /^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)?/)) return 0
    p += RLENGTH; return 1
}
function val(d,   c) {
    if (d > 32) return 0
    ws(); c = substr(s, p, 1)
    if (c == "{") return obj(d)
    if (c == "[") return arr(d)
    if (c == "\"") return str()
    if (substr(s, p, 4) == "true" || substr(s, p, 4) == "null") { p += 4; return 1 }
    if (substr(s, p, 5) == "false") { p += 5; return 1 }
    return num()
}
function obj(d,   c) {
    p++; ws(); if (substr(s, p, 1) == "}") { p++; return 1 }
    for (;;) {
        ws(); if (!str()) return 0
        ws(); if (substr(s, p++, 1) != ":") return 0
        if (!val(d + 1)) return 0
        ws(); c = substr(s, p++, 1)
        if (c == "}") return 1
        if (c != ",") return 0
    }
}
function arr(d,   c) {
    p++; ws(); if (substr(s, p, 1) == "]") { p++; return 1 }
    for (;;) {
        if (!val(d + 1)) return 0
        ws(); c = substr(s, p++, 1)
        if (c == "]") return 1
        if (c != ",") return 0
    }
}
{ sub(/\r$/, ""); s = s sep $0; sep = " "; if (length(s) > 65536) { big = 1; exit } }
END { if (big) exit
      n = length(s); p = 1; ws()
      if (substr(s, p, 1) == "{" && val(0)) { ws(); if (p > n) print s } }' "$1" 2>/dev/null; }

# ---- existing STEMS folders that could belong to this key (the server decides)
existing_json(){   # $1 stems dir  $2 key
    # One listing filtered by one awk, then the per-folder work only for the matches
    # (forking basename + tr for every folder is far too slow on a stick with hundreds of
    # STEMS folders). Rules: the name, name_, or name-<8 hex>, compared lowercase (ASCII).
    local k first e n src fl x
    k=$(printf '%s' "$2" | tr A-Z a-z); first=1; printf '['
    ls -1 "$1" 2>/dev/null | awk -v k="$k" '{
        l = tolower($0); n = length(k)
        if (l == k || l == k "_") { print; next }
        if (length(l) == n + 9 && substr(l, 1, n + 1) == k "-" && substr(l, n + 2) ~ /^[0-9a-f]+$/) print
    }' > $D/ej.match
    while IFS= read -r n; do
        e="$1/$n"
        [ -d "$e" ] || continue
        # pasted into the body as-is, so only a valid object: one invalid file would make
        # the whole POST invalid JSON (400) and the track could never be stemmed remotely
        src=null; [ -f "$e/source.json" ] && src=$(json_obj "$e/source.json")
        [ -n "$src" ] || src=null
        [ $first = 1 ] || printf ','; first=0
        fl=''; for x in "$e"/*; do [ -f "$x" ] && fl="$fl${fl:+,}\"$(jesc "$(basename "$x")")\""; done
        printf '{"name":"%s","source":%s,"files":[%s]}' "$(jesc "$n")" "$src" "$fl"
    done < $D/ej.match
    printf ']'
}

# ---- a job's state on the computer, or the HTTP code when it is not 200 (000: no answer)
job_state(){   # $1 server  $2 its id  $3 job
    local r code
    r=$(curl -sS -m 10 -H "$(az "$2" GET "/v1/jobs/$3" "")" -w '\n%{http_code}' "http://$1/v1/jobs/$3" 2>>$LOG)
    code=$(printf '%s' "$r" | tail -n1)
    if [ "$code" = 200 ]; then jget "$(printf '%s' "$r" | sed '$d')" state; else echo "$code"; fi
}
# the audio is on the computer: up=1. A request canceled meanwhile deletes the job (1)
uploaded(){   # $1 req  $2 server  $3 its id  $4 job
    setkv "$1" up 1 && return 0
    curl -sS -m 10 -X DELETE -H "$(az "$3" DELETE "/v1/jobs/$4" "")" "http://$2/v1/jobs/$4" >/dev/null 2>&1
    rm -f $D/job; log "canceled during upload: $4"; return 1
}

# ---- one request: advance it as far as it can go now. 0 = keep, 1 = finished
step(){   # $1 stick  $2 req  $3 server  $4 token  $5 computer name  $6 its id
    # sh variables are global unless declared: a name used here (such as r, the response)
    # overwrites the caller's loop variable of the same name
    local M R S f src key SD job size mt body r code st folder P ok n sz t pc cid sid e fresh=
    M=$1; R=$2; S=$3; pc=$5; sid=$6                  # $4, the token, only ever goes into an HMAC
    f=$(kv "$R" file); [ -n "$f" ] || { log "drop $R: no file="; return 1; }
    src="$M$f"; key=$(basename "$f"); key=${key%.*}
    t=$(kv "$R" title); [ -n "$t" ] || t=$key          # the library title; the file name can be an id
    cid=$(basename "$R" .req); cid=${cid#*-}             # <ms>-<content_id>.req, for azmod's row animation
    [ -f "$src" ] || { alert "Stems: the file is missing - $t"; log "drop $R: $src not found"; return 1; }
    SD=$(stems_dir "$M"); job=$(kv "$R" job)
    if [ -z "$job" ]; then
        size=$(wc -c < "$src" | tr -d ' '); mt=$(date -r "$src" +%s 2>/dev/null || echo 0)
        body=$(printf '{"file":"%s","size":%s,"mtime":%s,"existing":%s}' "$(jesc "${f#/}")" "$size" "$mt" "$(existing_json "$SD" "$key")")
        r=$(printf '%s' "$body" | curl -sS -m 20 -H "$(az "$sid" POST /v1/jobs "$(printf '%s' "$body" | wc -c | tr -d ' ')")" -H 'Content-Type: application/json; charset=utf-8' \
              --data-binary @- -w '\n%{http_code}' "http://$S/v1/jobs" 2>>$LOG)
        code=$(printf '%s' "$r" | tail -n1); r=$(printf '%s' "$r" | sed '$d')
        case "$code" in
            200|201) ;;
            # the computer no longer knows this stick: keep the request for after pairing
            401) not_paired "$pc"; log "POST 401: not paired with $sid"; return 0 ;;
            409) log "POST 409 $r: retry next pass"; return 0 ;;
            # a definite rejection of this track: drop it, and say why
            4??) e=$(jget "$r" error); [ -n "$e" ] && [ "$e" != null ] || e="HTTP $code"
                 alert "Stems failed on $pc: $e"; log "POST $code $r: dropped"; return 1 ;;
            # no answer (a timeout or a refused connection is 000), a 5xx: transient
            *) log "POST $code $r: retry next pass"; return 0 ;;
        esac
        if [ "$(jget "$r" skip)" = true ]; then alert "Stems already on this USB: $t"; return 1; fi
        job=$(jget "$r" id); [ "$job" = null ] && job=
        [ -n "$job" ] || { alert "Stems failed on $pc: no job id"; log "POST $code without id: $r"; return 1; }
        [ "$code" = 201 ] && fresh=1                 # a new job: it is waiting for its audio
        setkv "$R" job "$job"; setkv "$R" srv "$sid"; log "job $job on $sid for $f (provisional folder $(jget "$r" folder))"
        [ -f "$R" ] || { curl -sS -m 10 -X DELETE -H "$(az "$sid" DELETE "/v1/jobs/$job" "")" "http://$S/v1/jobs/$job" >/dev/null 2>&1; log "canceled during submit: $job"; return 1; }
    fi
    # A job that already has its audio must not be sent again: the server answers 409
    # for good, so a request that retried would never finish. That happens when a PUT's
    # response is lost after the computer has the file, or when the POST returns an
    # existing job of the same file (200) that is past waiting_upload.
    if [ "$(kv "$R" up)" != 1 ] && [ -z "$fresh" ]; then
        st=$(job_state "$S" "$sid" "$job")
        case "$st" in
            404) setkv "$R" job ""; log "job $job gone before upload: resubmit"; return 0 ;;
            waiting_upload) ;;
            queued|running|done|error) log "job $job already has its audio ($st)"; uploaded "$R" "$S" "$sid" "$job" || return 1 ;;
            *) log "state of $job unknown ($st): retry next pass"; return 0 ;;
        esac
    fi
    if [ "$(kv "$R" up)" != 1 ]; then
        cand 5 "Sending to $pc: $t"; progress "$cid" 0 up
        code=$(curl -sS -m 900 --speed-limit 2000 --speed-time 30 \
                 -H "$(az "$sid" PUT "/v1/jobs/$job/audio" "$(wc -c < "$src" | tr -d ' ')")" -T "$src" -o $D/put.out \
                 -w '%{http_code}' "http://$S/v1/jobs/$job/audio" 2>>$LOG)
        case "$code" in
            200) uploaded "$R" "$S" "$sid" "$job" || return 1; log "uploaded $job" ;;
            404) setkv "$R" job ""; log "job $job gone before upload: resubmit"; return 0 ;;
            # not waiting for its audio: an earlier PUT got there. Read the job and move on
            409) st=$(job_state "$S" "$sid" "$job"); log "PUT $job -> 409, state $st"
                 case "$st" in
                     queued|running|done|error) uploaded "$R" "$S" "$sid" "$job" || return 1 ;;
                     404) setkv "$R" job ""; return 0 ;;
                     *) return 0 ;;          # still receiving an earlier upload: next pass
                 esac ;;
            *) log "PUT $job -> $code $(cat $D/put.out 2>/dev/null)"; return 0 ;;   # network: retry next pass
        esac
    fi
    r=$(curl -sS -m 10 -H "$(az "$sid" GET "/v1/jobs/$job" "")" -w '\n%{http_code}' "http://$S/v1/jobs/$job" 2>>$LOG)
    code=$(printf '%s' "$r" | tail -n1); r=$(printf '%s' "$r" | sed '$d')
    [ "$code" = 404 ] && { setkv "$R" job ""; setkv "$R" up 0; log "job $job lost on the app: resubmit"; return 0; }
    [ "$code" = 200 ] || return 0
    st=$(jget "$r" state)
    case "$st" in
        queued)  n=$(jget "$r" position); progress "$cid" -1 wt        # sent, waiting its turn on the computer
                 if [ "$n" = 0 ] || [ -z "$n" ] || [ "$n" = null ]; then cand 3 "Waiting on $pc: $t"
                 else cand 3 "Waiting on $pc ($n ahead): $t"; fi; return 0 ;;
        running) n=$(jget "$r" pct); cand 6 "Making stems on $pc: $n% - $t"; progress "$cid" "$n" st; return 0 ;;
        error)   rm -f $D/job; e=$(jget "$r" error)
                 # "cancelled on the computer": the app's error text, matched as it sends it
                 case "$e" in *cancelled*) status "Stems canceled on $pc: $t" ;; *) alert "Stems failed on $pc: $e" ;; esac
                 curl -sS -m 10 -X DELETE -H "$(az "$sid" DELETE "/v1/jobs/$job" "")" "http://$S/v1/jobs/$job" >/dev/null 2>&1; return 1 ;;
        done) ;;
        *) return 0 ;;
    esac
    progress "$cid" 100 dn                            # the stems are coming back to the stick
    folder=$(jget "$r" folder)                        # authoritative only now: the computer names the folder
    case "$folder" in ""|.*|*/*) log "bad folder '$folder' for $job"; return 1 ;; esac
    P="$SD/.$folder.part"; rm -rf "$P"; mkdir -p "$P" || { alert "Stems failed: this USB cannot be written"; return 1; }
    printf '%s' "$r" | tr -d '\n' | grep -o '"name": *"[^"]*", *"size": *[0-9]*' |
        sed 's/"name": *"\([^"]*\)", *"size": *\([0-9]*\)/\1 \2/' > $D/files
    [ -s $D/files ] || { log "done without files[]: $r"; return 0; }
    ok=1
    while read -r n sz; do
        case "$n" in ""|.*|*/*) ok=0; break ;; esac
        curl -sS -f -m 600 -H "$(az "$sid" GET "/v1/jobs/$job/files/$n" "")" -o "$P/$n.tmp" "http://$S/v1/jobs/$job/files/$n" 2>>$LOG || { ok=0; break; }
        [ "$(wc -c < "$P/$n.tmp" | tr -d ' ')" = "$sz" ] || { log "size mismatch $n"; ok=0; break; }
        mv "$P/$n.tmp" "$P/$n"
    done < $D/files
    [ $ok = 1 ] || { log "download of $job incomplete: retry"; rm -rf "$P"; return 0; }
    sync
    if [ -e "$SD/$folder" ]; then rm -rf "$SD/.$folder.old"; mv "$SD/$folder" "$SD/.$folder.old" || { rm -rf "$P"; return 0; }; fi
    mv "$P" "$SD/$folder" && rm -rf "$SD/.$folder.old"; sync
    curl -sS -m 10 -X DELETE -H "$(az "$sid" DELETE "/v1/jobs/$job" "")" "http://$S/v1/jobs/$job" >/dev/null 2>&1
    # azmod: this track has stems now; its mark goes straight to full when the request
    # leaves the queue, instead of disappearing until its re-check lands
    echo "$cid" >> $D/done
    status "Stems ready: $t"; log "done $job -> $SD/$folder"; rm -f $D/job
    return 1
}

# ---- start: sweep what a killed run left behind
for m in /media/usb/*; do
    [ -f "$m/MODS/azstems.pair" ] || continue
    sd=$(stems_dir "$m"); rm -rf "$sd"/.*.part "$sd"/.*.old 2>/dev/null
done
echo $$ > $D/pid; trap 'rm -f $D/pid $D/job $D/jobs' EXIT      # azmod: "is the helper running"
: > $D/jobs
: > $D/done                                          # finished tracks of this run (azmod reads it every second)
log "start (pid $$)"
idle=0
while [ $idle -lt $AZRS_IDLE ]; do
    work=0; PST=; PPRIO=0
    for m in /media/usb/*; do
        q="$m/MODS/azstems-queue"; pf="$m/MODS/azstems.pair"
        # MENU > CANCEL STEMS: azmod renamed the request to .cancel.
        # Delete its job on the computer it went to (if it was sent), then the file.
        for c in "$q"/*.cancel; do
            [ -f "$c" ] || continue
            cj=$(kv "$c" job); cs=$(kv "$c" srv); ct=$(kv "$c" title)
            if [ -n "$cj" ] && [ -n "$cs" ] && [ -f "$pf" ]; then
                ck=$(pair_keys "$(tr -d '\n\r' < "$pf")" | awk -v i="$cs" '$1 == i {print $2; exit}')
                cx=; [ -n "$ck" ] && cx=$(server_for "$cs" "$ck")
                [ -n "$cx" ] && curl -sS -m 10 -X DELETE -H "$(az "$cs" DELETE "/v1/jobs/$cj" "")" "http://$cx/v1/jobs/$cj" >/dev/null 2>&1
            fi
            cc=$(basename "$c" .cancel); cc=${cc#*-}
            [ -f $D/job ] && grep -q "^cid=$cc " $D/job && rm -f $D/job
            jobs_rm "$cc"
            rm -f "$c"; log "canceled $(basename "$c") (job ${cj:-not sent})"; status "Stems canceled: ${ct:-a track}"
        done
        ls "$q"/*.req >/dev/null 2>&1 || continue
        work=1
        keys=; [ -f "$pf" ] && keys=$(pair_keys "$(tr -d '\n\r' < "$pf")")
        if [ -z "$keys" ]; then alert "Plug this USB into AZ-MODS once, with Remote Stems on"; continue; fi
        s=; id=; tok=                                  # the newest key whose app is on the network
        while read -r kid ktok; do
            [ -n "$kid" ] || continue
            x=$(server_for "$kid" "$ktok"); [ -n "$x" ] && { s=$x; id=$kid; tok=$ktok; break; }
        done <<KEYS
$keys
KEYS
        if [ -z "$s" ]; then
            # a computer that answered 401 is there but no longer knows this USB
            rej=$(printf '%s\n' "$keys" | while read -r kid _; do [ -f "$D/rejected.$kid" ] && { echo "$kid"; break; }; done)
            old=$(printf '%s\n' "$keys" | while read -r kid _; do [ -f "$D/oldapp.$kid" ] && { echo "$kid"; break; }; done)
            if [ -n "$rej" ]; then not_paired "$(pc_name "$rej")"
            # an AZ-MODS from before wire version 2: this deck will not send it the token
            elif [ -n "$old" ]; then on=$(cat "$D/name.$old" 2>/dev/null); alert "Update AZ-MODS${on:+ on $on} to use Remote Stems with this deck"
            elif [ "$(printf '%s\n' "$keys" | wc -l | tr -d ' ')" = 1 ]; then alert "$(pc_name "${keys%% *}") not found on the network"
            else alert "None of this USB's computers are on the network"; fi
            sleep 27; continue
        fi
        for req in "$q"/*.req; do
            [ -f "$req" ] || continue
            rs=$(kv "$req" srv); rS=$s; rid=$id; rtok=$tok
            if [ -n "$rs" ] && [ -n "$(kv "$req" job)" ] && [ "$rs" != "$id" ]; then
                # already sent to another computer: stay with it while it is there
                rk=$(printf '%s\n' "$keys" | awk -v i="$rs" '$1 == i {print $2; exit}')
                x=; [ -n "$rk" ] && x=$(server_for "$rs" "$rk")
                if [ -n "$x" ]; then rS=$x; rid=$rs; rtok=$rk
                else log "$(pc_name "$rs") is gone: $req goes to $(pc_name "$id")"; setkv "$req" job ""; setkv "$req" up 0; setkv "$req" srv ""; fi
            fi
            step "$m" "$req" "$rS" "$rtok" "$(pc_name "$rid")" "$rid"
            if [ $? = 1 ]; then rm -f "$req"; cc=$(basename "$req" .req); jobs_rm "${cc#*-}"; fi
        done
    done
    [ -n "$PST" ] && status "$PST"
    if [ $work = 1 ]; then idle=0; else idle=$((idle + 3)); fi
    sleep 3
done
log "idle: exit"
