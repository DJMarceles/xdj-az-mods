#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# build-dropbear.sh: the dropbear for decks whose firmware has none (XDJ-AZ 2.00 ships
# without it). Built from the pinned upstream release, never lifted from vendor firmware: known provenance,
# a known library set, patchable. Keys only: no PAM, no zlib; the sshgate hook runs it
# -s -g (no password logins at all). Output rides the stick as MODS/deck/lib/dropbear + LICENSE.
# Run by hand on a host with docker:
#   dropbear/build-dropbear.sh [workdir]   (the tarball, the logs and the binary land in workdir)
#
# Toolchain: the azmod-build:18.04 arm64 image the mods build in (docker/azmod-build.Dockerfile;
# glibc 2.27 < the deck's 2.29).
# The built binary's highest GLIBC_ symbol is read from the binary and must be <= 2.29.
# syslog stays compiled in: without it dropbear rejects -E ("Invalid option -E"), and the
# sshgate hook passes -F -E -s -g -R on every firmware. The script also runs the binary
# with exactly those options before accepting it.
set -euo pipefail
VER=2024.86
SHA=e78936dffc395f2e0db099321d6be659190966b99712b55c530dd0a1822e0a5e   # upstream SHA256SUM.asc
W=${1:-$HOME/dropbear-build}; mkdir -p "$W"; cd "$W"
T=dropbear-$VER.tar.bz2
[ -f "$T" ] || curl -sfLO "https://matt.ucc.asn.au/dropbear/releases/$T"
echo "$SHA  $T" | shasum -a 256 -c -
docker run --rm --platform=linux/arm64 -v "$W":/w azmod-build:18.04 bash -c "
  set -e; cd /tmp && tar xjf /w/$T && cd dropbear-$VER
  ./configure --disable-pam --disable-zlib --disable-lastlog --disable-utmp --disable-utmpx \
              --disable-wtmp --disable-wtmpx >/w/configure.log 2>&1
  make -j4 PROGRAMS=dropbear >/w/make.log 2>&1
  strip dropbear && cp dropbear /w/dropbear && cp LICENSE /w/LICENSE
  objdump -p /w/dropbear | grep NEEDED > /w/needed.txt
  objdump -T /w/dropbear | grep -o 'GLIBC_[0-9.]*' | sort -V | uniq | tail -1 > /w/glibc-max.txt"
G=$(cat glibc-max.txt); echo "max $G; NEEDED: $(awk '{print $2}' needed.txt | tr '\n' ' ')"
python3 -c "import sys; v=tuple(int(x) for x in '$G'.split('_')[1].split('.')); sys.exit(0 if v<=(2,29) else 1)" \
  || { echo "REFUSED: $G is newer than the deck's glibc 2.29"; exit 1; }
docker run --rm --platform=linux/arm64 -v "$W":/w azmod-build:18.04 bash -c \
  "timeout 3 /w/dropbear -F -E -s -g -R -p 2299 -r /tmp/hk >/w/optcheck.txt 2>&1; true"
grep -qiE "invalid option|usage" optcheck.txt && { echo "REFUSED: the hook's options are rejected:"; cat optcheck.txt; exit 1; }
echo "options -F -E -s -g -R accepted: $(head -1 optcheck.txt)"
file dropbear 2>/dev/null | cut -c1-120
echo "dropbear $VER sha256 $(shasum -a 256 dropbear | cut -d' ' -f1)"
