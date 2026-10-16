#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# tests/run-linux.sh: the unit tests on arm64 Linux, all three passes, on any host with Docker.
#
# Runs tests/run.sh in azmod-test:18.04 three times: plain, SAN=asan (AddressSanitizer +
# UndefinedBehaviorSanitizer) and SAN=tsan (ThreadSanitizer). Every pass runs even when an
# earlier one failed; the summary lists each pass's result line, and the exit status is nonzero
# if any pass failed. The images are built from docker/ when missing, and the test image is
# rebuilt when it lacks one of its pinned tools (clang-format 21.1.8, clang-10 with libFuzzer,
# clang-tidy-10, cppcheck 1.82); the layers it already has are reused. AZ_TEST_IMAGE names
# another tag for the test image (default azmod-test:18.04). Some suites start their
# own containers through the host's docker socket, so the tree is mounted at its own path
# (on macOS it must sit where Docker can share it, e.g. inside your home folder).
# ThreadSanitizer needs ASLR control (personality), hence seccomp=unconfined.
#
# Needs: Docker, and the asset headers that tools/extract-assets.py writes (see tests/run.sh).
set -euo pipefail
cd "$(dirname "$0")/.."
P=$PWD
docker image inspect azmod-build:18.04 >/dev/null 2>&1 ||
    docker build --platform=linux/arm64 -f docker/azmod-build.Dockerfile -t azmod-build:18.04 .
IMG=${AZ_TEST_IMAGE:-azmod-test:18.04}
has() { docker run --rm --platform=linux/arm64 "$IMG" sh -c "$1" >/dev/null 2>&1; }
if ! docker image inspect "$IMG" >/dev/null 2>&1 ||
   ! has 'clang-format --version | grep -q "clang-format version 21\.1\.8$"' ||
   ! has 'clang-10 --version && ls /usr/lib/llvm-10/lib/clang/10.0.0/lib/linux/libclang_rt.fuzzer-aarch64.a' ||
   ! has 'clang-tidy-10 --version | grep -q "LLVM version 10\.0\.0" && cppcheck --version | grep -q "^Cppcheck 1\.82$"'; then
    echo "building $IMG (missing, or lacking a pinned tool)"
    docker build --platform=linux/arm64 -t "$IMG" -f docker/azmod-test.Dockerfile docker
fi
exec docker run --rm --platform=linux/arm64 --security-opt seccomp=unconfined \
    -v /var/run/docker.sock:/var/run/docker.sock -v "$P:$P" -w "$P" "$IMG" bash -c '
        rc=0; sum=()
        for p in plain asan tsan; do
            log=$(mktemp); s=; [ "$p" = plain ] || s=$p; t0=$SECONDS
            SAN=$s bash tests/run.sh 2>&1 | tee "$log"
            st=${PIPESTATUS[0]}; [ "$st" -eq 0 ] || rc=1
            line=$(grep "^UNIT TESTS" "$log" | tail -1 || true)
            sum+=("$p ($((SECONDS - t0)) s, exit $st): ${line:-no UNIT TESTS line}"); rm -f "$log"
        done
        echo; echo "=== tests/run-linux.sh: every pass ==="; printf "  %s\n" "${sum[@]}"
        exit $rc'
