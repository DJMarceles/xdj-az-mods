# Logs and bug reports

| Part | Source |
|---|---|
| azmod's diagnostics switch and cap | `azlog_open()` in `src/azmod/azmod.c` |
| Size caps for appended files | `src/common/azlogcap.h` (`azlog_rotate`, `azlog_fopen_capped`) |
| Crash reports | `src/azmod/crash_guards.c` |
| Community mod logs, load results, crash blame | `src/azmod/community.c`; launched programs: `src/azmod/sdk_proc.c` |
| azstem's diagnostics and status files | `logln()`, `probestat_write()`, `ph_profile_dump()` in `src/azstem/azstem.c` |
| Deck-script log cap | `deck/stick/deck/lib/az-logcap.sh` |
| Save Logs to USB | the System Info screen: `src/azmod/screen_sysinfo.c`, `src/azmod/savelogs.c`; the copy: `deck/stick/deck/lib/az-savelogs` |

Logs live in three places on the deck:

| Place | Survives an EP147 restart | Survives a reboot or power-off |
|---|---|---|
| `/tmp` (RAM) | yes | no |
| `/home/root/settings` (p7, the deck's 61 MB settings partition) | yes | yes |
| the mods USB (`MODS/`) | yes | yes |

Almost everything is in `/tmp`, so a deck that crashed has to be read **before** it is
power-cycled. An EP147 restart (a crash restart, or the Mod Menu's **Reload**) keeps them.

## Always written

These files exist on every deck running AZ-MODS, with no flag. Each is bounded, and none is
written per frame.

### Crashes

| Path | Written | Limit |
|---|---|---|
| `/tmp/azmod-crash.log` | one block when EP147 takes `SIGSEGV`, `SIGBUS`, `SIGABRT`, `SIGILL` or `SIGFPE`, by the faulting thread's signal handler (async-signal-safe writes only), then the signal goes on to EP147's own handler | 256 KB. At start, a file within 32 KB of the cap moves to `azmod-crash.log.1`; a record that would pass the cap is cut with one `=== crash log full` line |
| `/tmp/azmod-crash.txt` | a shorter `SIGSEGV` record (fault address, pc, lr, frame chain), only when the reporter above is not armed | 256 KB, then `.1` |

A block starts with `=== azmod crash: signal` and ends with `=== end`. It holds the thread name,
the registers, the frame-pointer chain and the `maps` lines of EP147, azmod, azstem and any
community mod, so every address resolves to a library and an offset. A crash the core can pin
on a community mod says `community mod: <id>`. Count crashes over a test with
`grep -c '=== azmod crash' /tmp/azmod-crash.log`.

### Status files

Overwritten whole, so they never grow.

| Path | Written | Contents |
|---|---|---|
| `/tmp/azstem-build.log` | one line per EP147 start (16 KB, then `.1`) | the EP147 build azstem identified, ending in `stems ON` or `INERT` |
| `/tmp/azmod-hooktable` | at start, and again after community mods claim hooks | one summary line, then one line per installed hook and the path it took |
| `/tmp/azstem.probestat` | once a second, by azstem's worker thread | hook hit counters and streaming state |
| `/tmp/azstem.phprof` | every 10 seconds, by the same thread | time spent per step of that thread's loop |
| `/tmp/azstem.stall` | once a second while one step of that loop is held past 1.5 seconds | the step and how long it has been held |
| `/tmp/azmod-community/loaded` | when community mods load | `fw=`, `suspended=`, `off=`, then one `mod=<id>\|<state>\|<version>\|<reason>` line per mod |

### Community mods

| Path | Written | Limit |
|---|---|---|
| `/tmp/azmod-community/<id>.log` | the core's own lines about the mod (its load result, refused hooks and refused service calls), always; the mod's `log()` lines only with diagnostics on ([below](#with-diagnostics-on)) | 256 KB per file. At the first line of each start, a file over 128 KB moves to `<id>.log.1`; lines past the cap are dropped until the next start |
| `<stick>/MODS/community-mods/<id>/data/...` | stdout and stderr of a program the mod starts with `launch`, at the mod's `log_path` | 1 MB, then `.1` |
| systemd's journal, unit `azmod-handoff` | output of a program the mod hands the deck to with `handoff`, and of the handoff guard | the journal's |

The mod-developer view of these, with the emulator: the SDK's
[Logs](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/09-logs.md) page.

### Deck scripts

Each starts fresh at boot unless the table says otherwise. Every deck-script log under `/tmp`
except the Remote Stems log is capped by `deck/lib/az-logcap.sh`: past 64 KB it moves to
`<log>.1`, replacing the previous one, so each holds at most about 128 KB with the newest lines
in `<log>`.

| Log | Where | Kept |
|---|---|---|
| loader, this boot | `/tmp/az-usbload.log`; hooks in `/tmp/az-usbload.log.hooks` | until reboot |
| loader, across boots | `/home/root/settings/az-usbload/usbload.log` | about two boots, at most 32 KB; written only on a boot that changed something |
| post-start | `/tmp/az-usbload-poststart.log` | the latest EP147 start |
| USB and computer-link re-announce | `/tmp/az-renotify.log` | until reboot |
| updater | `/tmp/az-modupdate/log` | until reboot |
| Community Mods fetcher | `/tmp/az-community/log` | until reboot |
| Save Logs to USB | `/tmp/az-savelogs/log` | until reboot |
| Remote Stems | `/tmp/azrs/log` | until reboot; checked every 32 lines, past 256 KB it moves to `log.1` |
| SSH hook | `/tmp/az-sshgate.log`, copied to `MODS/az-sshgate-boot.log` on the USB | this boot |
| uninstall hook | `/tmp/az-uninstall.log`; `/home/root/settings/az-uninstall.log` | the settings copy only after an incomplete run |
| boot logo hook | `/tmp/az-bootlogo.log`, `/home/root/settings/az-bootlogo.log` | the settings copy keeps the last 40 lines |

### On the settings partition

Small state files that also tell what happened before a reboot:

| Path | Holds |
|---|---|
| `/home/root/settings/azmod-community/.blame` | the id of the community mod whose code crashed EP147 |
| `/home/root/settings/azmod-community/.initing` | the id of the mod whose `init()` was running; left behind when EP147 froze or died there |
| `/home/root/settings/azmod-community/.starting` | removed after 90 healthy seconds; present at start means the previous start ended early |
| `/home/root/settings/azmod-community/.crashes` | one epoch time per crash no mod could be blamed for, while mods were loaded; pruned to the last 30 minutes at every start |
| `MODS/.mods-disabled-crashloop` (on the USB) | the mod set the [crash-loop guard](USB-STICK.md#the-crash-loop-guard) turned off |

systemd's journal holds each EP147 start and exit, including the signal that ended a crashed
run: `journalctl -u EP147`. On the deck it does not survive a reboot.

## With diagnostics on

An empty flag file turns on the libraries' diagnostic logs:

```sh
touch /home/root/settings/azmod-logs.on
systemctl restart EP147
```

| Writer | Reads the flag | Writes | Limit |
|---|---|---|---|
| azmod | once, at EP147 start | `/tmp/azmod-<subsystem>.log`, one file per subsystem, and `/tmp/azmod-build.log` (the build it identified, ending in `mods ON`) | 256 KB per file; past the cap the file starts over with a `--- log truncated at <n> bytes ---` line |
| azstem | once, at EP147 start | `/tmp/azstem.log` | 4 MB; starts over the same way |
| the community core | once a second | each mod's own `log()` lines into `/tmp/azmod-community/<id>.log` | as above |

Remove the flag and restart EP147 to turn them off. Decks in normal use run with it absent.

**What it costs.** The diagnostics are change-only, and each line is an open, a write and a
close (flushed per line, so a file read during a test is never behind). The line is written on
the thread that logs, and for several subsystems that is one of EP147's threads, the
control-bus readers among them (`src/azmod/bus_interpose.c`). Logs on is for reproducing a
problem and reading the result, not for playing a set.

## Bug reports

What goes into an issue: [CONTRIBUTING.md](../CONTRIBUTING.md#bug-reports). The files, in order
of use:

| File | Answers |
|---|---|
| `/tmp/azmod-crash.log` | where EP147 crashed and which library the address belongs to |
| `/tmp/az-usbload.log`, `/home/root/settings/az-usbload/usbload.log` | what the loader staged, whether it restarted EP147, the crash-loop guard's decision |
| `/tmp/azmod-community/loaded` and `<id>.log` | which community mods loaded, and why one did not |
| `/tmp/azstem-build.log`, `/tmp/azmod-hooktable` | whether the libraries recognized the firmware and how each hook went in |
| `journalctl -u EP147` | when EP147 restarted, and on which signal |

Without SSH, the Mod Menu shows the versions (**System Info** has `AZ-MODS v<version>` and
**Updates** the installed set), and **Save Logs to USB** ([below](#save-logs-to-usb)) puts the
logs on the USB, where a computer can read them.

### Save Logs to USB

**Mod Menu > System Info > Save Logs to USB** copies the logs to the mods USB. The deck
software only hands over the request: the loader's request watcher runs
`deck/lib/az-savelogs`, which does the copying.

| | |
|---|---|
| Writes | `MODS/logs/<yyyymmdd-HHMMSS>/` on the USB the mods run from, named by the deck's clock (UTC) |
| `summary.txt` | the mods set version, the firmware, the EP147 build tag, uptime, EP147's restart count and state, the md5 of each staged library, free space, what the copy cut or left out, and the last 200 lines of `journalctl -u EP147` |
| `files/` | each log under its path on the deck (`files/tmp/azmod-crash.log`, ...): the files of the table above first, then the other deck-script logs and status files, and with diagnostics on the libraries' logs |
| Limits | each file: its newest 1 MB; all files together: 4 MB |
| Refused when | a mods update or a community change is running; the USB has under 8 MB free; no mods USB is in |
| While playing | it saves with tracks loaded or playing; the copy runs at the lowest CPU priority |
| Result | on the System Info screen, `Saved to MODS/logs/<name>` or the reason; also `/tmp/az-savelogs/status` |
| Partial copies | the copy is written as `<name>.tmp` and renamed when complete, so a folder without `.tmp` is whole. A failed copy removes its `.tmp`; one left by a power-off is removed by the next save |

### Collecting them over SSH

With SSH access set up ([the SDK's testing page](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/04-testing.md#ssh-access)),
one command copies everything above into one text file on the computer. It reads files and
runs `systemctl` and `journalctl`; it writes nothing on the deck.

```sh
ssh -i ~/.azmods/ssh/xdjaz_ecdsa root@DECK-IP '
  echo "mods set: $(cat /tmp/az-deck/version 2>/dev/null)"
  systemctl show EP147 -p NRestarts -p ActiveState
  md5sum /home/root/settings/az-usbload/*.so
  for f in /tmp/azmod-crash.log* /tmp/azmod-crash.txt* /tmp/az-usbload.log* \
           /tmp/az-usbload-poststart.log /home/root/settings/az-usbload/usbload.log \
           /tmp/azstem-build.log* /tmp/azmod-hooktable /tmp/azmod-community/* \
           /tmp/az-modupdate/log* /tmp/az-community/log* /tmp/azrs/log* /tmp/az-sshgate.log*; do
    [ -f "$f" ] && { echo "==== $f"; cat "$f"; }
  done
  echo "==== journal"; journalctl -u EP147 -n 200 --no-pager' > az-logs.txt
```

With diagnostics on, add `/tmp/azmod-*.log* /tmp/azstem.log` to the list.

On the [developer emulator](TESTING.md), `tools/az-ssh '<the same commands>' > az-logs.txt`
from the emulator repository reads the emulated deck the same way.

Logs, saved to the USB or collected over SSH, can name files and folders on the USB, the
deck's network address and SSH key fingerprints. Read them before attaching them to a public
issue.

## Adding a log

Rules for code in this tree, enforced by the unit tests' lints where the table says so:

| Rule | How |
|---|---|
| A release deck writes no diagnostics unless `azmod-logs.on` exists | azmod: open through `azlog_open()`; azstem: `logln()` |
| Every append-mode file is capped | `azlog_fopen_capped()` or `azlog_rotate()` (`src/common/azlogcap.h`); a writer that caps itself carries `logcap-ok: <why>` on the line (log-cap lint) |
| Deck scripts cap before each append | `azlog_cap "$LOG"` from `deck/lib/az-logcap.sh` |
| Nothing on the control-bus path makes a system call when diagnostics are off | `busread-lint.sh`, `busthread-lint.sh` |
| Log on a change, never per frame or per audio block | review |
| Flush per line | an open and close per line, or `fflush` |
| Logs go to `/tmp`, never `/home/root/settings` | p7 holds the deck's own settings; a full p7 breaks the deck's BPM display |

[TESTING.md](TESTING.md#logs) has the test-side detail.
