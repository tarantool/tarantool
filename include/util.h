#ifndef TARANTOOL_UTIL_H_INCLUDED
#define TARANTOOL_UTIL_H_INCLUDED
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "config.h"

#include <unistd.h>
#include <inttypes.h>

#ifndef NDEBUG
#define TRASH(ptr) memset(ptr, '#', sizeof(*ptr))
#else
#define TRASH(ptr)
#endif

#ifndef MAX
# define MAX(a, b) ((a) > (b) ? (a) : (b))
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Macros to define enum and corresponding strings. */
#define ENUM0_MEMBER(s, ...) s,
#define ENUM_MEMBER(s, v, ...) s = v,
#define ENUM_STRS_MEMBER(s, v, ...) [s] = #s,
#define ENUM0(enum_name, enum_members) enum enum_name {enum_members(ENUM0_MEMBER) enum_name##_MAX}
#define ENUM(enum_name, enum_members) enum enum_name {enum_members(ENUM_MEMBER) enum_name##_MAX}
#define STRS(enum_name, enum_members) \
	const char *enum_name##_strs[enum_name##_MAX + 1] = {enum_members(ENUM_STRS_MEMBER) '\0'}
#define STR2ENUM(enum_name, str) ((enum enum_name) strindex(enum_name##_strs, str, enum_name##_MAX))

uint32_t
strindex(const char **haystack, const char *needle, uint32_t hmax);

// Macros for printf functions
#ifdef __x86_64__
#define PRI_SZ  "lu"
#define PRI_SSZ "ld"
#define PRI_OFFT "lu"
#define PRI_XFFT "lx"
#else
#define PRI_SZ  "u"
#define PRI_SSZ "d"
#define PRI_OFFT "llu"
#define PRI_XFFT "llx"
#endif

#define nelem(x)     (sizeof((x))/sizeof((x)[0]))
#define likely(x)    __builtin_expect((x),1)
#define unlikely(x)  __builtin_expect((x),0)
#define field_sizeof(compound_type, field) sizeof(((compound_type *)NULL)->field)

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

#ifndef __offsetof
#define __offsetof offsetof
#endif

#ifndef lengthof
#define lengthof(array) (sizeof (array) / sizeof ((array)[0]))
#endif

#ifndef TYPEALIGN
#define TYPEALIGN(ALIGNVAL,LEN)  \
        (((uintptr_t) (LEN) + ((ALIGNVAL) - 1)) & ~((uintptr_t) ((ALIGNVAL) - 1)))

#define SHORTALIGN(LEN)                 TYPEALIGN(sizeof(int16_t), (LEN))
#define INTALIGN(LEN)                   TYPEALIGN(sizeof(int32_t), (LEN))
#define MAXALIGN(LEN)                   TYPEALIGN(sizeof(int64_t), (LEN))
#define PTRALIGN(LEN)                   TYPEALIGN(sizeof(void*), (LEN))
#define CACHEALIGN(LEN)			TYPEALIGN(32, (LEN))
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define CRLF "\r\n"

#ifdef GCC
# define FORMAT_PRINTF gnu_printf
#else
# define FORMAT_PRINTF printf
#endif

extern int forked;
pid_t tfork();
void close_all_xcpt(int fdc, ...);
void coredump(int dump_interval);
void *tnt_xrealloc(void *ptr, size_t size);

void __gcov_flush();


extern void *__libc_stack_end;

#ifdef ENABLE_BACKTRACE
char *backtrace(void *frame, void *stack, size_t stack_size);
#endif /* ENABLE_BACKTRACE */

#ifdef HAVE_BFD
struct symbol {
	void *addr;
	const char *name;
	void *end;
};
struct symbol *addr2symbol(void *addr);
void symbols_load(const char *name);
void symbols_free();
#endif /* HAVE_BFD */

#ifdef NDEBUG
#  define assert(pred) (void)(0)
#else
#  define assert(pred) ((pred) ? (void)(0) : assert_fail (#pred, __FILE__, __LINE__, __FUNCTION__))
void assert_fail(const char *assertion, const char *file,
		 unsigned int line, const char *function) __attribute__ ((noreturn));
#endif

#ifndef HAVE_MEMMEM
/* Declare memmem(). */
void *
memmem(const void *block, size_t blen, const void *pat, size_t plen);
#endif /* HAVE_MEMMEM */

#ifndef HAVE_MEMRCHR
/* Declare memrchr(). */
void *
memrchr(const void *s, int c, size_t n);
#endif /* HAVE_MEMRCHR */

#endif /* TARANTOOL_UTIL_H_INCLUDED */
