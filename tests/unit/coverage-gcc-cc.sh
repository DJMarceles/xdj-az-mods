#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# coverage-gcc-cc.sh (the gcc coverage pass's compiler, tools/unit-tests/coverage-gcc.sh): gcc with --coverage -O0, compiling each .c to "<output>.cov<N>.o" first so every
# binary gets its own .gcno/.gcda next to its output (gcc 7 otherwise names them after the
# source in the working directory, where same-named sources collide). Control variants
# (output name matching $COVCC_SKIP, default "ctl") and shared objects are built unchanged.
REAL=${COVCC_REAL:-gcc}
args=("$@"); out=; srcs=(); rest=(); conly=0; shared=0; i=0
while [ $i -lt ${#args[@]} ]; do
    a=${args[$i]}
    case $a in
        -o) i=$((i+1)); out=${args[$i]} ;;
        -o*) out=${a#-o} ;;
        -c|-S|-E) conly=1; rest+=("$a") ;;
        -shared) shared=1; rest+=("$a") ;;
        *.c) srcs+=("$a") ;;
        *) rest+=("$a") ;;
    esac
    i=$((i+1))
done
re=${COVCC_SKIP:-ctl}
[[ $(basename "${out:-x}") =~ $re ]] && exec "$REAL" "$@"
if [ $shared = 1 ] || [ -z "$out" ] || [ ${#srcs[@]} -eq 0 ]; then exec "$REAL" "$@"; fi
CF=(--coverage -O0 -fprofile-update=atomic)
if [ $conly = 1 ]; then exec "$REAL" "$@" "${CF[@]}"; fi
objs=(); n=0
for s in "${srcs[@]}"; do
    o="$out.cov$n.o"; n=$((n+1))
    "$REAL" "${rest[@]}" "${CF[@]}" -c "$s" -o "$o" 2> >(grep -v 'linker input file unused' >&2) || exit $?
    objs+=("$o")
done
# A process that dies by a fatal signal never writes its .gcda. The crash-path tests end that way
# (a child resets SIGSEGV to SIG_DFL and re-faults), so sigaction is wrapped at link time: setting
# SIG_DFL for a fatal signal first flushes the counters. COVCC_NOFLUSH=1 leaves the wrap out
# (the pass's control: the crash-path lines then read as never run). Forked children flush the parent's
# pre-fork counts too, so counts can be inflated; covered / not covered is unaffected.
printf '%s\n' '#include <signal.h>' 'void __gcov_flush(void);' \
    'int __real_sigaction(int, const struct sigaction *, struct sigaction *);' \
    'int __wrap_sigaction(int s, const struct sigaction *a, struct sigaction *o) {' \
    '    if (a && !(a->sa_flags & SA_SIGINFO) && a->sa_handler == SIG_DFL &&' \
    '        (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGABRT || s == SIGFPE || s == SIGTRAP))' \
    '        __gcov_flush();' \
    '    return __real_sigaction(s, a, o);' '}' > "$out.covflush.c"
"$REAL" -O0 -c "$out.covflush.c" -o "$out.covflush.o" || exit $?
[ "${COVCC_NOFLUSH:-0}" = 1 ] && exec "$REAL" "${objs[@]}" "${rest[@]}" "${CF[@]}" -o "$out"
exec "$REAL" "${objs[@]}" "$out.covflush.o" "${rest[@]}" "${CF[@]}" -Wl,--wrap=sigaction -o "$out"
