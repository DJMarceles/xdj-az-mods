/* SPDX-License-Identifier: GPL-3.0-only */
/* clang-atomic-compat.h: force-included (-include) into the clang-tidy compile only, never a
 * real build.
 *
 * The sources apply GCC's __atomic_* builtins to _Atomic-qualified objects. GCC accepts that;
 * clang rejects it ("address argument to atomic operation must be a pointer to integer or
 * pointer"), and a translation unit that does not compile is not analyzed at all. Each macro
 * below casts the address to a pointer to the object's unqualified type, which is what GCC
 * operates on: (0, *(p)) is an rvalue, so __typeof__ drops _Atomic. A macro does not re-expand
 * its own name, so the body still calls the builtin. The operation and its ordering are unchanged.
 */
#ifndef AZ_CLANG_ATOMIC_COMPAT_H
#define AZ_CLANG_ATOMIC_COMPAT_H
#define AZ_ATOMIC_PLAIN(p) ((__typeof__((0, *(p))) *)(p))
#define __atomic_load_n(p, o) __atomic_load_n(AZ_ATOMIC_PLAIN(p), o)
#define __atomic_store_n(p, v, o) __atomic_store_n(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_exchange_n(p, v, o) __atomic_exchange_n(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_compare_exchange_n(p, e, d, w, s, f) \
    __atomic_compare_exchange_n(AZ_ATOMIC_PLAIN(p), e, d, w, s, f)
#define __atomic_add_fetch(p, v, o) __atomic_add_fetch(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_sub_fetch(p, v, o) __atomic_sub_fetch(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_or_fetch(p, v, o) __atomic_or_fetch(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_xor_fetch(p, v, o) __atomic_xor_fetch(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_fetch_add(p, v, o) __atomic_fetch_add(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_fetch_sub(p, v, o) __atomic_fetch_sub(AZ_ATOMIC_PLAIN(p), v, o)
#define __atomic_fetch_or(p, v, o) __atomic_fetch_or(AZ_ATOMIC_PLAIN(p), v, o)
#endif
