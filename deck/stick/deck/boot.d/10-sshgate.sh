#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# az-sshgate: key-only root SSH is on only while the picked USB stick
# carries MODS/ssh.enable and a public key (MODS/authorized_keys, or inline in
# ssh.enable) that this deck has approved. Every other outcome turns it off: stops the
# dropbear this hook started and removes the keys it installed. Fail-safe: any failure
# leaves dropbear stopped. Pubkey only: root stays locked, dropbear runs -s -g.
# A MODS/deck/boot.d hook: az-usbload runs it with `sh` on every boot (and again
# on a mods-update re-run); see the hook contract below. This is revision 8 of the hook;
# test/fixtures/sshgate-rev7.sh, the revision before key approval, is a test control.
#
# Switch: ssh.enable turns SSH on (AZ-MODS writes and removes it with the key), and
# its absence turns SSH off as an action: a re-run on the same boot stops dropbear and
# removes the keys, so unticking SSH in the app takes effect.
#
# Stick: never create a mount of its own on a DJ's stick, and never touch the stick at
# all unless MODS/ exists. Reads go through the stock device-mount@ mount (/media/usb/sdX1),
# and the breadcrumb log is written into that same mount (remount rw only if it was ro,
# restored after).
#
# The release rootfs has no persistent overlay, so this hook itself:
#   - installs the keys to both /home/root/.ssh and /root/.ssh,
#   - keeps dropbear accepting while EP147 starts (5b),
#   - drops a boot log onto the stick (MODS/az-sshgate-boot.log) for debugging
#     when SSH itself is the thing that's broken.
#
# Priority: dropbear runs at normal priority. At a permanent RR 50 every SSH session, and
# everything typed in one, would outrank X and EP147's normal threads (apl_start.sh turns RT
# throttling off). Only while EP147 starts does the listener get RR 50, and sessions never
# keep it (deck/lib/az-sshprio, run by az-sshwindow.service from an EP147 drop-in).
#
# Host key: this deck's own, on both firmwares. 1.30's own dropbear setup uses the
# dropbear_ecdsa_host_key baked into the firmware image: the same key on every unit (and
# Pioneer's GPL source tree commits a dropbear_ecdsa_host_key too), so pinning it proves
# nothing. /etc/dropbear is a link to settings/az-dropbear (p7) on every firmware and the
# image's key is never copied there; dropbear -R creates the deck's own keys on first use and
# keeps them. A 1.30 deck that served the image's key shows ssh's "REMOTE HOST IDENTIFICATION
# HAS CHANGED" warning once (the DJ removes the old line with `ssh-keygen -R <deck address>`).
#
# Listening: dropbear listens on every interface, Wi-Fi included: Wi-Fi is how users reach the
# deck, its address comes from DHCP after this hook runs, and dropbear binds addresses, not
# interfaces. SSH is off unless the stick asks for it, root logs in by key only, and the host
# key identifies the unit.
#
# Approval: a key from a stick logs in only once it is approved on this deck.
#   - Stick keys this deck has not approved are held as pending (/run/az-ssh/pending, one
#     "SHA256:<fingerprint> <type> <base64>" line each) and never installed. The Mod Menu
#     asks "Allow remote access ..." with the fingerprint (azmod ssh_approve.c), Cancel
#     focused; Allow writes the fingerprint to /run/az-ssh/approve, and a path unit runs this
#     hook again as `approve`, which records the key and lets it log in.
#   - Approved keys live on p7 (settings/az-ssh-approved, "<type> <base64>" per line), so an
#     approval survives reboots. A key also has to be on the stick: a stick without it (or
#     without ssh.enable) turns SSH off.
#   - Existing setups: the first time this revision runs on a deck that already has its own
#     SSH host key directory (settings/az-dropbear, made the first time SSH was turned on by
#     an earlier revision), the keys on the stick that turned it on are approved as they are.
#     A deck that never had SSH on starts with no approved keys.
#   - dropbear starts only while at least one approved key is installed.
set -u
# Hook contract (az-usbload boot.d, firmware >= 1.32): this script rides the
# stick in MODS/deck/boot.d/ and az-usbload runs it after it has picked the
# stick (az-usbpick's port rule) and staged the mod set, with the pick in the
# environment: AZ_MNT (the stock mount, "" = no mods stick this boot), AZ_PORT
# (1|2), AZ_MODS ($AZ_MNT/MODS), AZ_MODS_OFF (1 when mods = off),
# AZ_STAGE (the p7 cache), AZ_DECK (the /tmp copy of MODS/deck this run is
# executing from). It never picks or mounts anything itself.
LOG=/tmp/az-sshgate.log
[ "${1:-}" = approve ] || : > "$LOG" 2>/dev/null   # an approval appends to the boot's log
azlog_cap() { return 0; }   # replaced below by deck/lib/az-logcap.sh, the shared cap
for _azl_f in "${0%/*}/az-logcap.sh" "${0%/*}/../lib/az-logcap.sh" "${0%/*}/lib/az-logcap.sh" \
    /tmp/az-deck/lib/az-logcap.sh; do
    # shellcheck source=../lib/az-logcap.sh
    [ -f "$_azl_f" ] && { . "$_azl_f"; break; }
done
log(){ azlog_cap "$LOG"; echo "[sshgate $(date '+%H:%M:%S' 2>/dev/null || echo -n)] $*" >> "$LOG" 2>/dev/null; }

APPROVED=/home/root/settings/az-ssh-approved
RS=/run/az-ssh
# A key's fingerprint as ssh-keygen -l prints it: SHA256 of the decoded key, base64, no '='.
# Without openssl on the image the base64 text's own tail is shown instead (still unique).
key_fp() {
    if command -v openssl >/dev/null 2>&1; then
        f=$(printf '%s' "$1" | openssl base64 -d -A 2>/dev/null | openssl dgst -sha256 -binary 2>/dev/null \
            | openssl base64 -A 2>/dev/null | tr -d '=')
        [ -n "$f" ] && { echo "SHA256:$f"; return; }
    fi
    echo "KEY:$(printf '%s' "$1" | tail -c 24)"
}
# install the keys in $1 (a file, maybe empty) as the only ones that may log in
install_keys() {
    for H in /home/root /root; do
        if [ -s "$1" ]; then
            mkdir -p "$H/.ssh" 2>/dev/null
            if cp -f "$1" "$H/.ssh/authorized_keys" 2>>"$LOG"; then
                chmod 700 "$H/.ssh" 2>/dev/null; chmod 600 "$H/.ssh/authorized_keys" 2>/dev/null
                log "installed -> $H/.ssh/authorized_keys"
            fi
        else
            rm -f "$H/.ssh/authorized_keys" 2>/dev/null
        fi
    done
}

# approve: run by az-sshapprove.service when the Mod Menu's Allow wrote a fingerprint.
if [ "${1:-}" = approve ]; then
    log "approve: start"
    FP=$(head -n 1 "$RS/approve" 2>/dev/null)
    rm -f "$RS/approve"
    LINE=$(awk -v f="$FP" '$1 == f { print $2 " " $3; exit }' "$RS/pending" 2>/dev/null)
    if [ -z "$FP" ] || [ -z "$LINE" ]; then log "approve: '$FP' is not a pending key; nothing done"; exit 0; fi
    { cat "$APPROVED" 2>/dev/null; echo "$LINE"; } > "$APPROVED.new" && mv -f "$APPROVED.new" "$APPROVED" \
        || { rm -f "$APPROVED.new"; log "approve: could not save $APPROVED"; exit 0; }
    awk -v f="$FP" '$1 != f' "$RS/pending" > "$RS/pending.new" 2>/dev/null; mv -f "$RS/pending.new" "$RS/pending"
    [ -s "$RS/pending" ] || rm -f "$RS/pending"
    echo "$LINE" >> "$RS/active"
    install_keys "$RS/active"
    log "approve: $FP approved on the deck; it may log in"
    systemctl restart az-dropbear.service 2>>"$LOG" && log "dropbear (re)started"
    exit 0
fi
log "start; root home in passwd: $(grep '^root:' /etc/passwd 2>/dev/null | cut -d: -f6)"

# off: stop this hook's dropbear unit, drop it, and remove the keys it installs. Both homes
# are RAM on the release rootfs (gone at power-off anyway); this makes a same-boot
# re-run (the mods-update watcher) turn SSH off too, as well as the next boot.
WINDOW_DROPIN=/run/systemd/system/EP147.service.d/50-az-sshwindow.conf
ssh_off() {
    log "SSH off: $*"
    if [ -f /run/systemd/system/az-dropbear.service ]; then
        systemctl stop az-dropbear.service 2>>"$LOG"
        rm -f /run/systemd/system/az-dropbear.service
        systemctl daemon-reload 2>>"$LOG"
        systemctl reset-failed az-dropbear.service 2>/dev/null
        log "stopped + removed az-dropbear.service"
    fi
    if [ -f /run/systemd/system/az-sshwindow.service ] || [ -f "$WINDOW_DROPIN" ]; then
        systemctl stop az-sshwindow.service 2>>"$LOG"   # its ExecStopPost demotes everything
        rm -f /run/systemd/system/az-sshwindow.service "$WINDOW_DROPIN" /run/az-sshprio
        systemctl daemon-reload 2>>"$LOG"
        log "removed the EP147 start window (az-sshwindow.service + drop-in)"
    fi
    if [ -f /run/systemd/system/az-sshapprove.path ]; then
        systemctl stop az-sshapprove.path 2>>"$LOG"
        rm -f /run/systemd/system/az-sshapprove.path /run/systemd/system/az-sshapprove.service
        systemctl daemon-reload 2>>"$LOG"
    fi
    rm -rf "$RS" /run/az-sshgate 2>/dev/null
    rm -f /home/root/.ssh/authorized_keys /root/.ssh/authorized_keys /tmp/az_authkeys 2>/dev/null
}

# 1. pick one stick by physical port (USB1 first, else USB2): stock-mounted, with
# MODS/. The key is read only from that stick. No pick -> SSH off, nothing touched.
MNT=${AZ_MNT:-}; PORT=${AZ_PORT:-}
[ -n "$MNT" ] || { ssh_off "no mods stick (AZ_MNT empty)"; exit 0; }
log "using USB$PORT stock mount $MNT (from az-usbload)"

# breadcrumb into MODS/ via the stock mount (best-effort; MODS/ exists here)
_stick_log() {
    ro=$(awk -v m="$MNT" '$2==m{print ($4 ~ /(^|,)ro(,|$)/)?1:0; exit}' /proc/mounts 2>/dev/null)
    [ "$ro" = 1 ] && { mount -o remount,rw "$MNT" 2>/dev/null || return 0; }
    { cp -f "$LOG" "$MNT/MODS/az-sshgate-boot.log.new" && mv -f "$MNT/MODS/az-sshgate-boot.log.new" "$MNT/MODS/az-sshgate-boot.log"; } 2>/dev/null \
        || rm -f "$MNT/MODS/az-sshgate-boot.log.new" 2>/dev/null
    sync
    [ "$ro" = 1 ] && mount -o remount,ro "$MNT" 2>/dev/null
    return 0
}

# 3. the switch, then the key source (copied to tmp)
[ -f "$MNT/MODS/ssh.enable" ] || { ssh_off "no MODS/ssh.enable"; _stick_log; exit 0; }
rm -f /tmp/az_authkeys
SRC=""
[ -f "$MNT/MODS/authorized_keys" ] && SRC="$MNT/MODS/authorized_keys"
[ -z "$SRC" ] && grep -qE 'AAAA' "$MNT/MODS/ssh.enable" 2>/dev/null && SRC="$MNT/MODS/ssh.enable"
[ -n "$SRC" ] && cp -f "$SRC" /tmp/az_authkeys 2>>"$LOG" && log "copied key from $SRC"

[ -f /tmp/az_authkeys ] || { ssh_off "ssh.enable but no key (MODS/authorized_keys)"; _stick_log; exit 0; }
if ! grep -qE '^(ssh-|ecdsa-|sk-)' /tmp/az_authkeys 2>/dev/null; then ssh_off "staged key file has no pubkey line"; _stick_log; exit 0; fi
log "key type: $(cut -d' ' -f1 /tmp/az_authkeys 2>/dev/null)"

# 3b. which of the stick's keys this deck has approved. The first run on a deck that had
# SSH on before (its own host key dir exists) approves the stick's keys as they are.
if [ ! -f "$APPROVED" ]; then
    if [ -d /home/root/settings/az-dropbear ]; then
        grep -E '^(ssh-|ecdsa-|sk-)' /tmp/az_authkeys | awk '{ print $1 " " $2 }' > "$APPROVED.new" \
            && mv -f "$APPROVED.new" "$APPROVED" && log "first run on a deck that had SSH on: the stick's keys are approved"
    else
        : > "$APPROVED.new" && mv -f "$APPROVED.new" "$APPROVED" && log "no approved keys yet"
    fi
fi
mkdir -p "$RS" 2>/dev/null
: > "$RS/active"; : > "$RS/pending.new"
grep -E '^(ssh-|ecdsa-|sk-)' /tmp/az_authkeys | while read -r KT KB KR; do
    if grep -qxF "$KT $KB" "$APPROVED" 2>/dev/null; then
        echo "$KT $KB${KR:+ $KR}" >> "$RS/active"
    else
        echo "$(key_fp "$KB") $KT $KB" >> "$RS/pending.new"
    fi
done
if [ -s "$RS/pending.new" ]; then
    mv -f "$RS/pending.new" "$RS/pending"
    log "waiting for approval on the deck: $(cut -d' ' -f1 "$RS/pending" | tr '\n' ' ')"
else
    rm -f "$RS/pending.new" "$RS/pending"
fi

# 4. install the approved keys to both candidate root homes (none = no login yet)
install_keys "$RS/active"

# 4b. Which dropbear. 1.30 ships /usr/sbin/dropbear, which is used unchanged. 2.00 has
# no dropbear, so the stick carries its own build (MODS/deck/lib/dropbear: upstream
# 2024.86, keys only, no PAM; built by dropbear/build-dropbear.sh). It is copied out of
# AZ_DECK (a per-run /tmp
# copy) to /run, which outlives the loader; /run is tmpfs without noexec (on 1.30 and
# 2.00). Neither binary -> SSH stays off (fail-safe).
DB=/usr/sbin/dropbear
if [ ! -x "$DB" ]; then
    DB=/run/az-dropbear
    if [ -f "${AZ_DECK:-/nonexistent}/lib/dropbear" ] && cp -f "$AZ_DECK/lib/dropbear" "$DB.new" 2>>"$LOG" \
       && chmod 755 "$DB.new" && mv -f "$DB.new" "$DB" && "$DB" -V >/dev/null 2>&1; then
        log "no /usr/sbin/dropbear on this firmware: using the stick's ($DB, $("$DB" -V 2>&1 | head -1))"
    else
        rm -f "$DB.new" 2>/dev/null
        ssh_off "no dropbear on this firmware and no usable MODS/deck/lib/dropbear"; _stick_log; exit 0
    fi
fi

# 4c. The host key: this deck's own, kept on p7, on every firmware. /etc is RAM, so without
# this 2.00 would make a new key every boot (every ssh client warns "REMOTE HOST IDENTIFICATION
# HAS CHANGED", a warning users then learn to ignore), and 1.30 would serve the key baked
# into the firmware image: identical on every unit. So
# /etc/dropbear -> settings/az-dropbear, and nothing under the RAM /etc/dropbear is ever copied
# there (on 1.30 that is the image's key; on 2.00 it does not exist). dropbear -R creates each
# key type the first time a client asks for it and writes it through the link. 30-uninstall
# removes az-dropbear.
KD=/home/root/settings/az-dropbear
mkdir -p "$KD" 2>/dev/null; chmod 700 "$KD" 2>/dev/null
if [ "$(readlink /etc/dropbear 2>/dev/null)" != "$KD" ]; then
    rm -rf /etc/dropbear 2>/dev/null
    ln -sfn "$KD" /etc/dropbear 2>>"$LOG"
fi
if [ -d /etc/dropbear ] && [ "$(readlink /etc/dropbear)" = "$KD" ]; then
    # the file names, for the log only
    # shellcheck disable=SC2012
    log "host key: this deck's own, on p7 ($KD: $(ls "$KD" 2>/dev/null | tr '\n' ' '))"
else
    # no link, no SSH: falling back to the image's key would serve the shared one again
    ssh_off "could not link /etc/dropbear to $KD"; _stick_log; exit 0
fi

# 5. start dropbear key-only, in its own unit. As a hook this script is a child of
# az-usbload.service; a dropbear started directly here would die the moment the loader
# exits (Type=simple + KillMode=control-group stops the whole cgroup), and every
# connection would be refused. A transient unit under /run/systemd/system outlives the
# loader, survives EP147 restarts (not in its cgroup), and runs at normal priority (5b
# covers EP147's start). daemon-reload right after writing it, never later (a pending
# reload deadlocks EP147 restarts).
mkdir -p /run/systemd/system 2>/dev/null
# (printf, not a heredoc: strip_payload keeps heredoc-free scripts only)
printf '%s\n' '[Unit]' 'Description=AZ key-only SSH (dropbear; started by the MODS/deck sshgate hook)' \
    '[Service]' 'Type=simple' "ExecStart=$DB -F -E -s -g -R" \
    'Restart=on-failure' 'RestartSec=2' > /run/systemd/system/az-dropbear.service
# 5b. EP147's start window. EP147's RT threads (throttling off) can starve a normal-priority
# dropbear while EP147 starts. az-sshwindow.service lifts the listener to RR 50 for 120 s and
# keeps every session at normal priority (deck/lib/az-sshprio); it runs at RR 51 itself so the
# burst cannot starve it. An EP147 drop-in starts it before every EP147 start (any restart:
# the loader's, the Mod Menu's, a crash restart), and this hook starts it once below for the
# boot's own start. Without az-sshprio the window is skipped: SSH still comes back once EP147
# has started, only later.
WINDOW=0
if [ -f "${AZ_DECK:-/nonexistent}/lib/az-sshprio" ] && cp -f "$AZ_DECK/lib/az-sshprio" /run/az-sshprio.new 2>>"$LOG" \
   && mv -f /run/az-sshprio.new /run/az-sshprio; then
    mkdir -p "${WINDOW_DROPIN%/*}" 2>/dev/null
    printf '%s\n' '[Unit]' 'Description=AZ SSH: dropbear listener at RR 50 while EP147 starts (sshgate hook)' \
        '[Service]' 'Type=simple' 'ExecStart=/bin/sh /run/az-sshprio window 120' \
        'ExecStopPost=-/bin/sh /run/az-sshprio normal' \
        'CPUSchedulingPolicy=rr' 'CPUSchedulingPriority=51' > /run/systemd/system/az-sshwindow.service
    printf '%s\n' '[Service]' \
        'ExecStartPre=-/bin/systemctl --no-block restart az-sshwindow.service' > "$WINDOW_DROPIN"
    WINDOW=1
else
    rm -f /run/az-sshprio.new 2>/dev/null
    # a same-boot re-run from a set without the helper: drop the window an earlier run made
    if [ -f /run/systemd/system/az-sshwindow.service ] || [ -f "$WINDOW_DROPIN" ]; then
        systemctl stop az-sshwindow.service 2>>"$LOG"
        rm -f /run/systemd/system/az-sshwindow.service "$WINDOW_DROPIN" /run/az-sshprio
    fi
    log "no deck/lib/az-sshprio: no EP147 start window (dropbear stays at normal priority)"
fi
# 5c. the approval path: Allow in the Mod Menu writes $RS/approve, which runs this hook
# again as `approve` (from its /run copy: the boot-time copy in AZ_DECK is gone by then).
cp -f "$0" /run/az-sshgate.new 2>>"$LOG" && mv -f /run/az-sshgate.new /run/az-sshgate
printf '%s\n' '[Unit]' 'Description=AZ SSH: a key approved on the deck (sshgate hook)' \
    '[Path]' "PathExists=$RS/approve" > /run/systemd/system/az-sshapprove.path
printf '%s\n' '[Unit]' 'Description=AZ SSH: record a key approved on the deck (sshgate hook)' \
    '[Service]' 'Type=oneshot' 'ExecStart=/bin/sh /run/az-sshgate approve' > /run/systemd/system/az-sshapprove.service
systemctl daemon-reload 2>>"$LOG"
systemctl restart az-sshapprove.path 2>>"$LOG"
if [ ! -s "$RS/active" ]; then
    systemctl stop az-dropbear.service 2>>"$LOG"
    log "no approved key on this stick: dropbear not started (approve the key in the Mod Menu)"
elif systemctl restart az-dropbear.service 2>>"$LOG"; then
    sleep 1.5
    DBPID=$(systemctl show az-dropbear.service -p MainPID --value 2>/dev/null)
    log "dropbear started as az-dropbear.service (pid ${DBPID:-?}, normal priority)"
    # the boot's own EP147 start is under way (or just over): one window for it
    [ "$WINDOW" = 1 ] && systemctl --no-block restart az-sshwindow.service 2>>"$LOG"
else
    log "dropbear unit failed to start"
fi
log "done"
_stick_log
