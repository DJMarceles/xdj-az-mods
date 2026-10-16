# Testing

Checks for a change to the AZ-MODS core, from the host tests to a real deck. The toolchain,
the build and `verify.sh` are in [BUILDING.md](../BUILDING.md).

## A check counts only if it can fail

A check that cannot produce the other answer proves nothing: a log that is never written
reads exactly like a log with nothing to report, and a crash detector that cannot see crashes
reports every boot as clean. Every check here is paired with a **control**, a step whose
result must differ, run in the same session. A control that does not fail means the check is
broken and its pass means nothing.

The suites below follow this rule (controls are named `CONTROL` in the output, or listed per
stage). The same applies to a test on a deck: show the case where the change must act, and
the case where it must not.

## Where each check lives

| Check | Where | Runs on | Needs |
|---|---|---|---|
| Reproduce a release byte for byte | `verify.sh` | your computer | Docker, an official release ([BUILDING.md](../BUILDING.md)) |
| Unit tests | `tests/run.sh`, `tests/unit/` | your computer and the build container | a C compiler, perl, python3, Docker, the asset headers |
| Unit tests, all three passes (plain, ASan+UBSan, TSan) | `tests/run-linux.sh` | the test container | Docker; builds `azmod-build:18.04` and `azmod-test:18.04` when missing |
| Fuzz harnesses of the parsers that read untrusted input | `tools/fuzz/` | corpus replay inside the unit tests; libFuzzer in the test container | as the unit tests |
| Stem lane self-check | `src/azmod/test/lanetest.c` | your computer | a C compiler |
| Community Mods host tests | `src/azmod/test/community/` | the build container | Docker, `azmod-build:18.04`, the asset headers |
| Deck script tests | `deck/test/run.sh` | privileged containers | Docker with loop devices, Python 3.9+ |
| Crash-loop guard unit test | `deck/test/clguard.sh` | your computer | a POSIX `sh` |
| Emulator test suite | `tools/az-emu-test.py` in [xdj-az-emu](https://github.com/Kyle-Hosman/xdj-az-emu) | the developer emulator | the emulator, set up from your own deck |
| Real-deck checks | [On a real deck](#5-on-a-real-deck) | your deck | SSH access |

"The asset headers" are the files `tools/extract-assets.py` writes into `src/azmod/` from an
official `azmod.so` ([BUILDING.md](../BUILDING.md)); `tests/run.sh` stops and names the
missing one.

## 1. Start from a reproduced release

Build the source of the release you start from and check it with `verify.sh`
([BUILDING.md](../BUILDING.md), "Check a build against an official release"). A `MATCH` for
both libraries proves the toolchain produces that release's exact bytes, so a later
difference in behavior comes from the change, not the compiler. After the change the hashes
differ by design: `verify.sh` is the control for the build environment, not a test of the
change.

The build is a check as well (`src/Makefile`):

| Flag | Applies to | Catches |
|---|---|---|
| `-Wall -Wextra -Werror` | azmod, azstem | any warning, including an implicit declaration |
| `-Wl,--no-undefined` | azmod | an undefined symbol. A preloaded library links against a function that does not exist and fails only when the call runs, inside EP147 on a deck. |
| `-Werror` | the handoff guard | any warning |

## 2. Host tests

No deck and no emulator. The sanitizer passes ([Sanitizers](#sanitizers)) are part of this
step.

### The unit tests

`tests/unit/` holds the unit tests of the mods' logic: 86 suites in `tests/unit/run.sh`,
with about 320 controls. They cover settings parsing and migration, the Mod Menu (every
screen drawn and compared with recorded output), the Community Mods screens, the community
mod services, the panel bus path (a seeded frame stream replayed against a recorded trace),
the drum roll, the stem loader, the library spool and `export.pdb` sync, EP147 object
lifetimes, thread hand-offs, hook installation, log caps, and lints that keep the code inside
its own rules (no `fork`, no blocking calls on EP147's threads, every file descriptor
close-on-exec, every log capped). Production functions are extracted from the sources by
text at run time, never copied, so a suite cannot test a stale duplicate.

Each suite is rebuilt against deliberately broken variants of the code, its **controls**,
and each control must fail: a passing control fails the whole run.

From the repository root, after `tools/extract-assets.py`:

```sh
tests/run.sh
```

| Variable | Effect |
|---|---|
| `HOST_CC` | the host compiler |
| `CLANG_FORMAT` | a clang-format 21.1.8 for the formatting check (or `pip install clang-format==21.1.8`) |
| `SAN=asan`, `SAN=tsan` | a sanitizer pass (Linux only; `tests/run-linux.sh` runs both) |
| `AZ_COMPILE_ONLY=1` | builds every test program and control variant with the host compiler and runs none; a program that does not compile is named with its errors (the builds that need a container are listed as SKIP) |

The script assembles the layout the suite is written for in `tests/.work/tree` (copies, not
links) and runs `tests/unit/run.sh` there. Some suites run in the build container
(`azmod-build:18.04`, created by `build.sh`), so the tree must sit where Docker can mount it
(on macOS, inside your home folder). The suites that need Linux, and the sanitizer passes,
run in the test container `azmod-test:18.04`: the build container plus sqlite, git, Docker's
client, the pinned clang-format, clang-tidy and cppcheck. Build it from the repository root
after `azmod-build:18.04`, or let `tests/run-linux.sh` build both:

```sh
docker build --platform=linux/arm64 -t azmod-test:18.04 -f docker/azmod-test.Dockerfile docker
```

The run ends with one line:

| Line | Meaning |
|---|---|
| `UNIT TESTS: OK` | every suite passed and every control failed as required |
| `UNIT TESTS: OK WITH n SKIPPED` | everything that ran passed or failed as required; the `SKIP` lines did not run |
| `UNIT TESTS: FAILED` | with the count of failed suites, controls that passed, and skips |

A `SKIP` is never a pass. It names what did not run and why: a suite that needs Linux (run
on an arm64 Linux host, or `tests/run-linux.sh`), a tool that is not installed, or an input
this tree does not carry (the development build, the git history, the address notes the
EP147 address tables are generated from, the firmware's own art).

Three checks the unit tests run can also run on their own:

| Command | Notes |
|---|---|
| `tools/static-check.sh` | clang-tidy and cppcheck over the release sources: `src/` with `src/Makefile`'s compile lines and `src/.clang-tidy`, inside `azmod-test:18.04` when the pinned tools are not on the path |
| `tools/format-check.sh FILE...`, `tools/format-check.sh --fix FILE...` | check or reformat the named files. Without file names it checks the whole tree, which in this layout only `tests/run.sh` does; run that way here, it stops and says so. |
| `tests/.work/tree/tools/fuzz/run.sh <name\|all> <seconds>` | fuzz a harness; also `--replay <name\|all>` and `--list`. Runs on the layout `tests/run.sh` assembles, so run `tests/run.sh` once first; run from the repository root, it stops and says so. |

### Sanitizers

A change passes all three unit test passes before it is sent: plain, AddressSanitizer +
UndefinedBehaviorSanitizer, and ThreadSanitizer. Each sanitizer pass rebuilds the host
suites with the sanitizer compiled in and runs them again.

| Pass | Finds |
|---|---|
| ASan (`SAN=asan`) | heap, stack and global out-of-bounds accesses, use-after-free, double free, and (LeakSanitizer) memory leaks |
| UBSan (same pass) | undefined behavior: signed integer overflow, misaligned access, invalid shifts, null pointer dereference |
| TSan (`SAN=tsan`) | data races and lock-order inversions between threads |

From the repository root, after `tools/extract-assets.py`, on any host with Docker:

```sh
tests/run-linux.sh
```

It runs `tests/run.sh` three times in `azmod-test:18.04`, rebuilding the image when it lacks
a pinned tool (clang-format 21.1.8, clang-10 with libFuzzer, clang-tidy-10, cppcheck 1.82).
`AZ_TEST_IMAGE` names another tag. Every pass runs even when an earlier one fails; the run
ends with each pass's `UNIT TESTS` line and exits nonzero if any pass failed.

Each sanitizer pass has its own controls: planted bugs (a heap overflow and a signed
overflow under ASan and UBSan, a data race and earlier races put back under TSan) must each
be reported, and the check that every test binary of the pass is instrumented has an
uninstrumented build as its control.

A sanitizer report is fixed in the code. A suppression (`tests/unit/sanitizer-*.supp`) is
only for code this repository does not own, with the reason written next to it.

### Coverage

`COVERAGE=1 tests/run.sh` runs the host suites once more with clang's source-based
coverage and reports the line, branch and function coverage of the release build
(`tests/unit/coverage-report.py` describes the method). Every line of the release
libraries counts, so code that only runs inside `EP147` (hooks, X11 drawing) shows as not
covered: the emulator suite and the deck checks below cover it. It needs clang and
`llvm-profdata` / `llvm-cov` (through `xcrun` on macOS, or named by `LLVM_PROFDATA` and
`LLVM_COV`), and Docker for the headers of the release build. The run ends with the
totals and names the directory (`COVERAGE_DIR` overrides it) that holds `summary.txt`,
`functions.tsv` (per function) and `uncovered/` (the lines not reached in each function
the suites call). The suites clang cannot build (the Linux-only suites, the aarch64 hook
tests and the Community Mods host tests) are measured in the same run with gcc's
`--coverage` in the containers (`tests/unit/coverage-gcc.sh`) and merged line by line; the
summary also gives the clang suites' numbers alone, and `gcc-summary.txt` the gcc half's per
file. That half has its own controls: built without the counter flush on a fatal signal, the
crash reporter's lines must read as never run, and left out, one suite's lines (claim_vslot's)
must disappear exactly.

### The stem lane self-check

`src/azmod/test/lanetest.c` links against the real `src/azmod/menu.c` (with the parts it
`#include`s; none depends on EP147) and stubs for everything else, renders the Mod Menu's
per-stem waveform lanes into an off-screen frame buffer and asserts pixel colors:

- the empty slot when azstem is absent;
- a stem's own color when it is on, gray when it is muted;
- muting one stem leaves its neighbors' lanes alone;
- silence collapses to a one-pixel center line.

Each assertion contrasts two states, so a lane that draws nothing, or draws the same thing
in both, fails. From `src/azmod/`:

```sh
cc -w -I. -o lanetest menu.c test/lanetest.c && ./lanetest
```

It prints `azmod stem-lane test: all checks passed`, or stops at the failing `assert`.

### The Community Mods host tests

`src/azmod/test/community/run.sh` compiles the real `src/azmod/community.c` into a harness
(`harness.c`), builds real test mods from `mods.c` (one source, variants chosen by `-D`
flags), writes real `azmod.json` manifests into a temporary stage folder, and runs the host
against them. Covered: loading a good mod, the manifest rules and every refusal reason,
`min_core`, the update-version comparison, settings and folder services, the firmware tag,
hook claims (ownership, overlap, addresses outside EP147), the start quarantine, crash
blame, and a mod that hangs or crashes in its own `init`. Refusals run in one start beside a
good mod; cases marked `CONTROL` are the inputs that must be treated differently.

The mods are aarch64 shared objects, so the suite runs in the build container.
`tests/run.sh` runs it in every pass, with the host built under that pass's sanitizer (the
mods stay plain: the host refuses a mod linked with a sanitizer runtime, as it would on a
deck), and with its own controls. To run it alone, from the repository root, after
`tools/extract-assets.py`:

```sh
docker run --rm --platform=linux/arm64 -v "$PWD/src":/g -w /g azmod-build:18.04 bash azmod/test/community/run.sh
```

It prints one `PASS` or `FAIL` line per case, then `passed N failed M`, and exits nonzero
if any case failed.

The firmware tag has an independent oracle, `tagoracle.py`, which recomputes the tag from
the harness binary's ELF segments. The build container has no Python, so `run.sh` leaves a
copy of the harness at `src/azmod/test/community/.harness.bin`; run the oracle on the host
and compare its output with the `tag` line `run.sh` printed:

```sh
python3 src/azmod/test/community/tagoracle.py src/azmod/test/community/.harness.bin
```

## 3. Deck script tests

`deck/test/run.sh` tests the loader and the USB hooks in privileged `alpine:3.20`
containers, against real FAT loop images mounted the way the deck mounts a USB, with
`systemctl`, `udevadm` and EP147 stubbed. It runs every script in its stripped form
(`deck/strip_payload.py`, exactly what ships) after `sh -n` passes on each. Requirements are
in [deck/README.md](../deck/README.md), "Tests".

| Suite | Tests |
|---|---|
| `ports` | the USB pick by physical port (`az-usbpick`) and the SSH gate hook on the picked USB |
| `mirror` | mirroring the USB's `MODS/` set into the deck's cache, boot after boot |
| `hooks` | staging `MODS/deck/`, handing over to the USB's loader, running `boot.d` hooks in order |
| `poststart` | the USB re-announcement after an EP147 restart the loader did not make |
| `gate` | the restart gate: the loader restarts EP147 only after the boot has finished and EP147 is up (WiFi associated, or settled) |
| `restart` | the restart policy: never over a loaded track, never on a deck already running mods; a mods update stages and waits for Reload |
| `bootlogo` | the boot logo restore hook, on a synthetic image |
| `uninstall` | the `MODS/uninstall` purge, checked on the settings tree, not only the log |
| `renotify` | which USB port a USB is announced on |
| `watch` | the request watcher that runs the updater and the Community Mods fetcher |
| `trust` | the trust pins: only files pinned by a genuinely signed `deck/trust.pins` run or load; the developer override |
| `modupdate` | the on-deck updater (`deck/stick/deck/lib/az-modupdate`) against a local channel: a genuine newer set installs; a rollback under a newer tag and a tampered manifest are refused |
| `community` | the Community Mods fetcher (`deck/stick/deck/lib/az-community`) against a local HTTPS fixture |
| `remotestems` | the Remote Stems helper (`deck/azrs/azrs.sh`) against a fake server that loses responses, times out, refuses and rejects; each fix reverted in a copy must fail |
| `logcap` | the deck scripts' log caps (`deck/stick/deck/lib/az-logcap.sh`), against the same scripts with the cap removed |
| `sshprio` | `deck/stick/deck/lib/az-sshprio` under real scheduling: the SSH listener is real-time only during EP147's start window and every session goes back to normal scheduling; copies with each demotion removed must fail |

```sh
deck/test/run.sh             # every suite, about 45 minutes
deck/test/run.sh hooks gate  # some suites
```

The exit status is 0 only if every suite printed at least one `PASS` and none printed `FAIL`
(or `NO_DOSFSTOOLS`). A suite that prints no `PASS` is reported as
`NO RESULTS from <suite> (instrument broken)` and fails the run, so a container that ran
nothing cannot pass.

`community`, `remotestems`, `sshprio` and the first part of `trust` run under the deck's own
shell and tools, not Alpine's. They need a copy of a deck's root file system (taken from your
own deck), named by `AZ_DECK_ROOTFS` and mounted read-only. Without one they fail rather than
pass on a userland that is not the deck's; to skip them, name the other suites.

The [crash-loop guard](USB-STICK.md#the-crash-loop-guard) has its own deterministic test
with no container. It sources the guard's functions from the loader (by default
`deck/payload/usr/sbin/az-usbload`, or the path given as its argument), stubs `systemctl`,
EP147 and the USB, and drives the trip, no-trip and retry scenarios:

```sh
sh deck/test/clguard.sh
```

## 4. The developer emulator

[xdj-az-emu](https://github.com/Kyle-Hosman/xdj-az-emu) runs the deck's own system, copied
from your own deck, in a virtual machine, with the same EP147 and the same mods as a deck.
Its display is a faithful signal: a rendering problem on a deck shows in the emulator, and a
fix proven there holds on the deck. What differs is data (the tracks and stems on the
emulated USB) and what it does not emulate, listed in its guide's "What the emulator cannot
do": real-time audio on Windows, speakers (audio goes to a silent sink), and installing from
a community repository. Setup and use:
[guide](https://github.com/Kyle-Hosman/xdj-az-emu/blob/main/docs/guide.md).

### The test suite

`tools/az-emu-test.py` boots the emulator headless and runs its stages in order. Each stage
prints `PASS` or `FAIL` with the line of evidence it is based on, and carries its own
control:

| Stage | Asserts | Control |
|---|---|---|
| `instrument` | the crash detector can see a crash | a boot without mods must show EP147's own intermittent start-up crash in one of `--control-boots` tries (default 3); if it never fires, a deliberately induced crash must be seen instead |
| `boot` | for 150 s after a cold boot, EP147 keeps one process, `NRestarts` stays 0, and the host-side serial log shows no `SIGSEGV` and no exit | the `instrument` stage |
| `mods` | the running EP147 has mapped exactly the expected `azmod.so` and `azstem.so`, then runs 150 s without exiting | the md5 of the mapped libraries, not of the files on disk |
| `controls` | panel controls change their screen region | turning the browse knob past the end of a list changes nothing |
| `stems` | on a playing track with stems, the stem pads mute and unmute the deck | with the STEMS master off, the same pads do not silence it |
| `drumroll` | SLIP LOOP + pad 8 arms the drum roll, holding the X-PAD fires hits on the beat grid, pad 8 again disarms | the same X-PAD hold after disarming fires no hits |
| `fourdeck` | the 4 DECK view shows non-blank deck 3 and deck 4 panels, and 2 DECK returns | a blank panel fails |
| `modmenu` | every Mod Menu page has its own title, and BACK closes the menu | revisiting the first page gives the identical title |
| `themes` | switching Themes darkens the menu and switching back restores it | a tap on the theme label changes nothing |

A failed `boot` stops the run. To test a build of this tree:

```sh
python3 tools/az-emu-test.py --azmod path/to/src/out/azmod.so --azstem path/to/src/out/azstem.so
python3 tools/az-emu-test.py --stages boot,controls,modmenu   # some stages only
```

| Option | Effect |
|---|---|
| `--azmod`, `--azstem` | push this pair instead of testing the installed set; the installed pair is saved first and put back when the run ends |
| `--stages LIST` | run only these stages, comma-separated |
| `--usb IMG` | the USB image to boot with (default `build/usb-test.img` if it exists, else `build/usb.img`) |
| `--control-boots N` | tries for the `instrument` stage (default 3) |
| `--take-over` | stop a running emulator first |

The `stems` and `drumroll` stages need a USB image with a specific track, its stems and a
drum roll bank; the guide's "The test USB image" lists what it holds. A full run takes about
20 minutes.

### Proving the suite can fail

The emulator repository ships deliberately broken libraries in `tools/test-fixtures/`:
`crashmod.c`, an `azmod.so` whose constructor aborts inside EP147, and `nostems.c`, an
`azstem.so` with no stems engine. Build them into `build/test-fixtures/` (the guide's
"Proving the suite can fail" has the command), then:

| Run | Must |
|---|---|
| `--azmod build/test-fixtures/crashmod.so` | fail `mods`, and the stages after it |
| `--azstem build/test-fixtures/nostems.so` | fail `stems`, and only `stems` |

Run this once on a new emulator setup, and again when a pass is unexpected.

## 5. On a real deck

The emulator does not replace a deck: timing, audio and USB behave like a virtual machine.

### Getting a build onto the deck

A shell on the deck: the SDK's
[Testing on your deck](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/04-testing.md),
section 2 (SSH is set up by the AZ-MODS app, is key-only, and a new key is allowed once at
the deck: [USB-STICK.md](USB-STICK.md#allowing-a-key)).

**From the USB.** Copy `src/out/azmod.so` and `src/out/azstem.so` into the USB's
`MODS/` folder and boot the deck with it. The loader compares each `MODS/*.so` with its cache
by md5, copies what differs, and restarts EP147 once with the new set
([USB-STICK.md](USB-STICK.md#how-the-deck-boots-with-the-usb)).

**Over SSH.** EP147 preloads the copies in the loader's cache,
`/home/root/settings/az-usbload/azmod.so` and `azstem.so`. Upload each next to the loaded one,
rename it over it, then restart:

```sh
scp -i KEY src/out/azmod.so root@DECK-IP:/home/root/settings/az-usbload/azmod.so.new
ssh -i KEY root@DECK-IP '
  cd /home/root/settings/az-usbload &&
  mv azmod.so.new azmod.so &&
  rm -f /home/root/settings/azmod-community/.starting &&
  systemctl restart EP147'
```

| Rule | Failure mode |
|---|---|
| Upload as `.new`, then `mv` | writing over a `.so` EP147 has mapped changes its code under the running program and crashes it |
| Put the final build on every mods USB in the deck | a plain `systemctl restart EP147` does not read the USB, but the next boot copies the USB's `MODS/*.so` over the cache when they differ |
| Keep backups of the previous build on the USB or in `/tmp`, under a name not ending in `.so`; run `df -h /home/root/settings` before copying | `/home/root/settings` is about 61 MB and holds the deck's own settings; filling it breaks the deck's own features. The loader mirrors any `MODS/*.so` into the cache. |

Removing `.starting` tells the Community Mods host that the restart is deliberate: two quick
restarts in a row otherwise look like a crash loop and hold every community mod back for one
start. The loader does the same before its own restarts.

A restart takes about 15 to 20 seconds, and SSH can stall while it runs; reconnect after half
a minute before concluding anything.

### Reading the result

**Did it crash?** First, every time:

```sh
systemctl show EP147 -p NRestarts
```

It counts the restarts systemd made on its own since the last requested restart: 0 right
after a `systemctl restart` (or a Mod Menu Reload); any higher number later is a crash. Over
a longer test, count crash reports: `grep -c '=== azmod crash' /tmp/azmod-crash.log`. The
crash reporter appends a report there whenever EP147 takes a fatal signal, whatever the
logging setting (`/tmp/azmod-crash.txt` is a smaller fallback, written only when the reporter
is not armed). A set that crashes EP147 repeatedly right after it is applied trips the
[crash-loop guard](USB-STICK.md#the-crash-loop-guard); `/tmp/az-usbload.log` has its
decision.

**Did the libraries switch on?** `/tmp/azstem-build.log` is always written: its last line
ends in `stems ON`. azmod writes the same line, ending in `mods ON`, to `/tmp/azmod-build.log`
when logs are on. `INERT` means the firmware was not recognized or a table entry is missing
([FIRMWARE-SUPPORT.md](FIRMWARE-SUPPORT.md)).

**Did it load the bytes you built?** A file on disk is not proof that EP147 mapped it.
`grep azmod /proc/$(pidof EP147)/maps` names the mapped file: `md5sum` it, compare with the
build, and check the map line does not say `(deleted)`.

### Logs

Diagnostic logging is off on a normal deck. An empty flag file turns it on; it is read once,
at start:

```sh
touch /home/root/settings/azmod-logs.on
systemctl restart EP147
```

With the flag, azmod writes `/tmp/azmod-*.log` (each capped at 256 KB: a log past the cap
starts over with a `--- log truncated ---` line) and azstem writes `/tmp/azstem.log`. Remove
the flag and restart to turn them off. New diagnostics in azmod go through `azlog_open()` so
they follow the same switch ([ARCHITECTURE.md](../ARCHITECTURE.md) lists the few files
written without it). Every other file the libraries append to under `/tmp` opens through
`azlog_fopen_capped()` (`src/common/azlogcap.h`), which moves a file past its cap to
`<name>.1`. An append-mode open that does neither fails the unit tests' log-cap lint, unless the
writer caps itself and carries a `logcap-ok: <why>` marker on the same line.

Every file the libraries and the deck scripts write, always or with the flag, with its limit and
how long it is kept: [LOGS.md](LOGS.md). Everything under `/tmp` is in RAM and is gone after a
reboot or power-off. Logs, captures and screenshots never go to `/home/root/settings`; the few
small, capped files there are listed in [LOGS.md](LOGS.md#on-the-settings-partition).

### What to test

- The changed feature on **every deck it applies to** (decks 2, 3 and 4, not only deck 1),
  with its Mod Menu row or flag on and off. Off behaves exactly like stock.
- The control: the case where the code must not act (another deck, another screen, a track
  without stems, the feature off).
- Every firmware build the change can run on ([FIRMWARE-SUPPORT.md](FIRMWARE-SUPPORT.md)).
- `NRestarts` still 0 at the end, and no new block in `/tmp/azmod-crash.log`.

A pull request states what was tried, on which firmware and decks, and what the control
showed.
