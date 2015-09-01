/*-
 * pmatomic.h - Poor Man's atomics
 *
 * Borrowed from FreeBSD (original copyright follows).
 *
 * Standard atomic facilities in stdatomic.h are great, unless you are
 * stuck with an old compiler, or you attempt to compile code using
 * stdatomic.h in C++ mode [gcc 4.9], or if you were desperate enough to
 * enable OpenMP in C mode [gcc 4.9].
 *
 * There are several discrepancies between gcc and clang, namely clang
 * refuses to apply atomic operations to non-atomic types while gcc is
 * more tolerant.
 *
 * For these reasons we provide a custom implementation of operations on
 * atomic types:
 *
 *   A. same names/semantics as in stdatomic.h;
 *   B. all names prefixed with 'pm_' to avoid name collisions;
 *   C. applicable to non-atomic types.
 *
 * Ex:
 *     int i;
 *     pm_atomic_fetch_add_explicit(&i, 1, pm_memory_order_relaxed);
 *
 * Note: do NOT use _Atomic keyword (see gcc issues above).
 */

/*-
 * Migration strategy
 *
 * Switching to <stdatomic.h> will be relatively easy. A
 * straightforward text replace on the codebase removes 'pm_' prefix
 * in names. Compiling with clang reveals missing _Atomic qualifiers.
 */

/*-
 * Logistics
 *
 * In order to make it possible to merge with the updated upstream we
 * restrict modifications in this file to the bare minimum. For this
 * reason we comment unused code regions with #if 0 instead of removing
 * them.
 *
 * Renames are carried out by a script generating the final header.
 */

/*-
 * Copyright (c) 2011 Ed Schouten <ed@FreeBSD.org>
 *                    David Chisnall <theraven@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: releng/10.1/sys/sys/stdatomic.h 264496 2014-04-15 09:41:52Z tijl $
 */

#ifndef PMATOMIC_H__
#define	PMATOMIC_H__

/* Compiler-fu */
#if !defined(__has_feature)
#define __has_feature(x) 0
#endif
#if !defined(__has_builtin)
#define __has_builtin(x) __has_feature(x)
#endif
#if !defined(__GNUC_PREREQ__)
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define __GNUC_PREREQ__(maj, min)					\
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#define __GNUC_PREREQ__(maj, min) 0
#endif
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Removed __PM_CLANG_ATOMICS clause, this is because
 * 1) clang understands gcc intrinsics as well;
 * 2) clang intrinsics require _Atomic quialified types while gcc ones
 *    don't.
 */
#if __GNUC_PREREQ__(4, 7)
#define	__PM_GNUC_ATOMICS
#elif defined(__GNUC__)
#define	__PM_SYNC_ATOMICS
#else
#error "pmatomic.h does not support your compiler"
#endif

/*
 * 7.17.1 Atomic lock-free macros.
 */
#if 0

#ifdef __GCC_ATOMIC_BOOL_LOCK_FREE
#define	ATOMIC_BOOL_LOCK_FREE		__GCC_ATOMIC_BOOL_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_CHAR_LOCK_FREE
#define	ATOMIC_CHAR_LOCK_FREE		__GCC_ATOMIC_CHAR_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_CHAR16_T_LOCK_FREE
#define	ATOMIC_CHAR16_T_LOCK_FREE	__GCC_ATOMIC_CHAR16_T_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_CHAR32_T_LOCK_FREE
#define	ATOMIC_CHAR32_T_LOCK_FREE	__GCC_ATOMIC_CHAR32_T_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_WCHAR_T_LOCK_FREE
#define	ATOMIC_WCHAR_T_LOCK_FREE	__GCC_ATOMIC_WCHAR_T_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_SHORT_LOCK_FREE
#define	ATOMIC_SHORT_LOCK_FREE		__GCC_ATOMIC_SHORT_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_INT_LOCK_FREE
#define	ATOMIC_INT_LOCK_FREE		__GCC_ATOMIC_INT_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_LONG_LOCK_FREE
#define	ATOMIC_LONG_LOCK_FREE		__GCC_ATOMIC_LONG_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_LLONG_LOCK_FREE
#define	ATOMIC_LLONG_LOCK_FREE		__GCC_ATOMIC_LLONG_LOCK_FREE
#endif
#ifdef __GCC_ATOMIC_POINTER_LOCK_FREE
#define	ATOMIC_POINTER_LOCK_FREE	__GCC_ATOMIC_POINTER_LOCK_FREE
#endif

#endif

/*
 * 7.17.2 Initialization.
 */
#if 0

#if defined(__PM_CLANG_ATOMICS)
#define	ATOMIC_VAR_INIT(value)		(value)
#define	atomic_init(obj, value)		__c11_atomic_init(obj, value)
#else
#define	ATOMIC_VAR_INIT(value)		{ .__val = (value) }
#define	atomic_init(obj, value)		((void)((obj)->__val = (value)))
#endif

#endif

/*
 * Clang and recent GCC both provide predefined macros for the memory
 * orderings.  If we are using a compiler that doesn't define them, use the
 * clang values - these will be ignored in the fallback path.
 */

#ifndef __ATOMIC_RELAXED
#define __ATOMIC_RELAXED		0
#endif
#ifndef __ATOMIC_CONSUME
#define __ATOMIC_CONSUME		1
#endif
#ifndef __ATOMIC_ACQUIRE
#define __ATOMIC_ACQUIRE		2
#endif
#ifndef __ATOMIC_RELEASE
#define __ATOMIC_RELEASE		3
#endif
#ifndef __ATOMIC_ACQ_REL
#define __ATOMIC_ACQ_REL		4
#endif
#ifndef __ATOMIC_SEQ_CST
#define __ATOMIC_SEQ_CST		5
#endif

/*
 * 7.17.3 Order and consistency.
 *
 * The pm_memory_order_* constants that denote the barrier behaviour of the
 * atomic operations.
 */

typedef enum {
	pm_memory_order_relaxed = __ATOMIC_RELAXED,
	pm_memory_order_consume = __ATOMIC_CONSUME,
	pm_memory_order_acquire = __ATOMIC_ACQUIRE,
	pm_memory_order_release = __ATOMIC_RELEASE,
	pm_memory_order_acq_rel = __ATOMIC_ACQ_REL,
	pm_memory_order_seq_cst = __ATOMIC_SEQ_CST
} pm_memory_order;

/*
 * 7.17.4 Fences.
 */

static __inline void
pm_atomic_thread_fence(pm_memory_order __order __attribute__((__unused__)))
{

#ifdef __PM_CLANG_ATOMICS
	__c11_atomic_thread_fence(__order);
#elif defined(__PM_GNUC_ATOMICS)
	__atomic_thread_fence(__order);
#else
	__sync_synchronize();
#endif
}

static __inline void
pm_atomic_signal_fence(pm_memory_order __order __attribute__((__unused__)))
{

#ifdef __PM_CLANG_ATOMICS
	__c11_atomic_signal_fence(__order);
#elif defined(__PM_GNUC_ATOMICS)
	__atomic_signal_fence(__order);
#else
	__asm volatile ("" ::: "memory");
#endif
}

/*
 * 7.17.5 Lock-free property.
 */
#if 0

#if defined(_KERNEL)
/* Atomics in kernelspace are always lock-free. */
#define	atomic_is_lock_free(obj) \
	((void)(obj), (bool)1)
#elif defined(__PM_CLANG_ATOMICS)
#define	atomic_is_lock_free(obj) \
	__atomic_is_lock_free(sizeof(*(obj)), obj)
#elif defined(__PM_GNUC_ATOMICS)
#define	atomic_is_lock_free(obj) \
	__atomic_is_lock_free(sizeof((obj)->__val), &(obj)->__val)
#else
#define	atomic_is_lock_free(obj) \
	((void)(obj), sizeof((obj)->__val) <= sizeof(void *))
#endif

#endif

/*
 * 7.17.6 Atomic integer types.
 */
#if 0

typedef _Atomic(bool)			atomic_bool;
typedef _Atomic(char)			atomic_char;
typedef _Atomic(signed char)		atomic_schar;
typedef _Atomic(unsigned char)		atomic_uchar;
typedef _Atomic(short)			atomic_short;
typedef _Atomic(unsigned short)		atomic_ushort;
typedef _Atomic(int)			atomic_int;
typedef _Atomic(unsigned int)		atomic_uint;
typedef _Atomic(long)			atomic_long;
typedef _Atomic(unsigned long)		atomic_ulong;
typedef _Atomic(long long)		atomic_llong;
typedef _Atomic(unsigned long long)	atomic_ullong;
typedef _Atomic(__char16_t)		atomic_char16_t;
typedef _Atomic(__char32_t)		atomic_char32_t;
typedef _Atomic(___wchar_t)		atomic_wchar_t;
typedef _Atomic(__int_least8_t)		atomic_int_least8_t;
typedef _Atomic(__uint_least8_t)	atomic_uint_least8_t;
typedef _Atomic(__int_least16_t)	atomic_int_least16_t;
typedef _Atomic(__uint_least16_t)	atomic_uint_least16_t;
typedef _Atomic(__int_least32_t)	atomic_int_least32_t;
typedef _Atomic(__uint_least32_t)	atomic_uint_least32_t;
typedef _Atomic(__int_least64_t)	atomic_int_least64_t;
typedef _Atomic(__uint_least64_t)	atomic_uint_least64_t;
typedef _Atomic(__int_fast8_t)		atomic_int_fast8_t;
typedef _Atomic(__uint_fast8_t)		atomic_uint_fast8_t;
typedef _Atomic(__int_fast16_t)		atomic_int_fast16_t;
typedef _Atomic(__uint_fast16_t)	atomic_uint_fast16_t;
typedef _Atomic(__int_fast32_t)		atomic_int_fast32_t;
typedef _Atomic(__uint_fast32_t)	atomic_uint_fast32_t;
typedef _Atomic(__int_fast64_t)		atomic_int_fast64_t;
typedef _Atomic(__uint_fast64_t)	atomic_uint_fast64_t;
typedef _Atomic(__intptr_t)		atomic_intptr_t;
typedef _Atomic(__uintptr_t)		atomic_uintptr_t;
typedef _Atomic(__size_t)		atomic_size_t;
typedef _Atomic(__ptrdiff_t)		atomic_ptrdiff_t;
typedef _Atomic(__intmax_t)		atomic_intmax_t;
typedef _Atomic(__uintmax_t)		atomic_uintmax_t;

#endif

/*
 * 7.17.7 Operations on atomic types.
 */

/*
 * Compiler-specific operations.
 */

#if defined(__PM_CLANG_ATOMICS)
#define	pm_atomic_compare_exchange_strong_explicit(object, expected,	\
    desired, success, failure)						\
	__c11_atomic_compare_exchange_strong(object, expected, desired,	\
	    success, failure)
#define	pm_atomic_compare_exchange_weak_explicit(object, expected,		\
    desired, success, failure)						\
	__c11_atomic_compare_exchange_weak(object, expected, desired,	\
	    success, failure)
#define	pm_atomic_exchange_explicit(object, desired, order)		\
	__c11_atomic_exchange(object, desired, order)
#define	pm_atomic_fetch_add_explicit(object, operand, order)		\
	__c11_atomic_fetch_add(object, operand, order)
#define	pm_atomic_fetch_and_explicit(object, operand, order)		\
	__c11_atomic_fetch_and(object, operand, order)
#define	pm_atomic_fetch_or_explicit(object, operand, order)		\
	__c11_atomic_fetch_or(object, operand, order)
#define	pm_atomic_fetch_sub_explicit(object, operand, order)		\
	__c11_atomic_fetch_sub(object, operand, order)
#define	pm_atomic_fetch_xor_explicit(object, operand, order)		\
	__c11_atomic_fetch_xor(object, operand, order)
#define	pm_atomic_load_explicit(object, order)				\
	__c11_atomic_load(object, order)
#define	pm_atomic_store_explicit(object, desired, order)			\
	__c11_atomic_store(object, desired, order)
#elif defined(__PM_GNUC_ATOMICS)
#define	pm_atomic_compare_exchange_strong_explicit(object, expected,	\
    desired, success, failure)						\
	__atomic_compare_exchange_n(object, expected,		\
	    desired, 0, success, failure)
#define	pm_atomic_compare_exchange_weak_explicit(object, expected,		\
    desired, success, failure)						\
	__atomic_compare_exchange_n(object, expected,		\
	    desired, 1, success, failure)
#define	pm_atomic_exchange_explicit(object, desired, order)		\
	__atomic_exchange_n(object, desired, order)
#define	pm_atomic_fetch_add_explicit(object, operand, order)		\
	__atomic_fetch_add(object, operand, order)
#define	pm_atomic_fetch_and_explicit(object, operand, order)		\
	__atomic_fetch_and(object, operand, order)
#define	pm_atomic_fetch_or_explicit(object, operand, order)		\
	__atomic_fetch_or(object, operand, order)
#define	pm_atomic_fetch_sub_explicit(object, operand, order)		\
	__atomic_fetch_sub(object, operand, order)
#define	pm_atomic_fetch_xor_explicit(object, operand, order)		\
	__atomic_fetch_xor(object, operand, order)
#define	pm_atomic_load_explicit(object, order)				\
	__atomic_load_n(object, order)
#define	pm_atomic_store_explicit(object, desired, order)			\
	__atomic_store_n(object, desired, order)
#else
#define	__pm_atomic_apply_stride(object, operand) \
	(((__typeof__(*(object)))0) + (operand))
#define	pm_atomic_compare_exchange_strong_explicit(object, expected,	\
    desired, success, failure)	__extension__ ({			\
	__typeof__(expected) __ep = (expected);				\
	__typeof__(*__ep) __e = *__ep;					\
	(void)(success); (void)(failure);				\
	(bool)((*__ep = __sync_val_compare_and_swap(object,	\
	    __e, desired)) == __e);					\
})
#define	pm_atomic_compare_exchange_weak_explicit(object, expected,		\
    desired, success, failure)						\
	pm_atomic_compare_exchange_strong_explicit(object, expected,	\
		desired, success, failure)
#if __has_builtin(__sync_swap)
/* Clang provides a full-barrier atomic exchange - use it if available. */
#define	pm_atomic_exchange_explicit(object, desired, order)		\
	((void)(order), __sync_swap(object, desired))
#else
/*
 * __sync_lock_test_and_set() is only an acquire barrier in theory (although in
 * practice it is usually a full barrier) so we need an explicit barrier before
 * it.
 */
#define	pm_atomic_exchange_explicit(object, desired, order)		\
__extension__ ({							\
	__typeof__(object) __o = (object);				\
	__typeof__(desired) __d = (desired);				\
	(void)(order);							\
	__sync_synchronize();						\
	__sync_lock_test_and_set(__o, __d);			\
})
#endif
#define	pm_atomic_fetch_add_explicit(object, operand, order)		\
	((void)(order), __sync_fetch_and_add(object,		\
	    __pm_atomic_apply_stride(object, operand)))
#define	pm_atomic_fetch_and_explicit(object, operand, order)		\
	((void)(order), __sync_fetch_and_and(object, operand))
#define	pm_atomic_fetch_or_explicit(object, operand, order)		\
	((void)(order), __sync_fetch_and_or(object, operand))
#define	pm_atomic_fetch_sub_explicit(object, operand, order)		\
	((void)(order), __sync_fetch_and_sub(object,		\
	    __pm_atomic_apply_stride(object, operand)))
#define	pm_atomic_fetch_xor_explicit(object, operand, order)		\
	((void)(order), __sync_fetch_and_xor(object, operand))
#define	pm_atomic_load_explicit(object, order)				\
	((void)(order), __sync_fetch_and_add(object, 0))
#define	pm_atomic_store_explicit(object, desired, order)			\
	((void)pm_atomic_exchange_explicit(object, desired, order))
#endif

/*
 * Convenience functions.
 *
 * Don't provide these in kernel space. In kernel space, we should be
 * disciplined enough to always provide explicit barriers.
 */

#ifndef _KERNEL
#define	pm_atomic_compare_exchange_strong(object, expected, desired)	\
	pm_atomic_compare_exchange_strong_explicit(object, expected,	\
	    desired, pm_memory_order_seq_cst, pm_memory_order_seq_cst)
#define	pm_atomic_compare_exchange_weak(object, expected, desired)		\
	pm_atomic_compare_exchange_weak_explicit(object, expected,		\
	    desired, pm_memory_order_seq_cst, pm_memory_order_seq_cst)
#define	pm_atomic_exchange(object, desired)				\
	pm_atomic_exchange_explicit(object, desired, pm_memory_order_seq_cst)
#define	pm_atomic_fetch_add(object, operand)				\
	pm_atomic_fetch_add_explicit(object, operand, pm_memory_order_seq_cst)
#define	pm_atomic_fetch_and(object, operand)				\
	pm_atomic_fetch_and_explicit(object, operand, pm_memory_order_seq_cst)
#define	pm_atomic_fetch_or(object, operand)				\
	pm_atomic_fetch_or_explicit(object, operand, pm_memory_order_seq_cst)
#define	pm_atomic_fetch_sub(object, operand)				\
	pm_atomic_fetch_sub_explicit(object, operand, pm_memory_order_seq_cst)
#define	pm_atomic_fetch_xor(object, operand)				\
	pm_atomic_fetch_xor_explicit(object, operand, pm_memory_order_seq_cst)
#define	pm_atomic_load(object)						\
	pm_atomic_load_explicit(object, pm_memory_order_seq_cst)
#define	pm_atomic_store(object, desired)					\
	pm_atomic_store_explicit(object, desired, pm_memory_order_seq_cst)
#endif /* !_KERNEL */

/*
 * 7.17.8 Atomic flag type and operations.
 *
 * XXX: Assume atomic_bool can be used as an atomic_flag. Is there some
 * kind of compiler built-in type we could use?
 */
#if 0

typedef struct {
	atomic_bool	__flag;
} atomic_flag;

#define	ATOMIC_FLAG_INIT		{ ATOMIC_VAR_INIT(0) }

static __inline bool
atomic_flag_test_and_set_explicit(volatile atomic_flag *__object,
    pm_memory_order __order)
{
	return (pm_atomic_exchange_explicit(&__object->__flag, 1, __order));
}

static __inline void
atomic_flag_clear_explicit(volatile atomic_flag *__object, pm_memory_order __order)
{

	pm_atomic_store_explicit(&__object->__flag, 0, __order);
}

#ifndef _KERNEL
static __inline bool
atomic_flag_test_and_set(volatile atomic_flag *__object)
{

	return (atomic_flag_test_and_set_explicit(__object,
	    pm_memory_order_seq_cst));
}

static __inline void
atomic_flag_clear(volatile atomic_flag *__object)
{

	atomic_flag_clear_explicit(__object, pm_memory_order_seq_cst);
}
#endif /* !_KERNEL */

#endif

#endif /* !_STDATOMIC_H_ */
