#ifndef TARANTOOL_UTIL_H_INCLUDED
#define TARANTOOL_UTIL_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "trivia/config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define restrict __restrict__

#ifndef NDEBUG
#define TRASH(ptr) memset(ptr, '#', sizeof(*ptr))
#else
#define TRASH(ptr) (void) (ptr)
#endif

#ifndef MAX
# define MAX(a, b) ((a) > (b) ? (a) : (b))
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define SWAP(a, b) do {							\
	__typeof__(a) tmp = (a);					\
	(a) = (b);							\
	(b) = tmp;							\
} while (0)

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* Macros to define enum and corresponding strings. */
#define ENUM0_MEMBER(s, ...) s,
#define ENUM_MEMBER(s, v, ...) s = v,
#define ENUM0(enum_name, enum_members) enum enum_name { enum_members(ENUM0_MEMBER) enum_name##_MAX }
#define ENUM(enum_name, enum_members) enum enum_name { enum_members(ENUM_MEMBER) enum_name##_MAX }
#if defined(__cplusplus)
#define ENUM_STRS_MEMBER(s, v, ...) names[s] = #s;
/* A special hack to emulate C99 designated initializers */
#define STRS(enum_name, enum_members)					\
	const char *enum_name##_strs[enum_name##_MAX];			\
	namespace {							\
		const struct enum_name##_strs_init {			\
			enum_name##_strs_init(const char **names) {	\
				memset(names, 0, sizeof(*names) *	\
				       enum_name##_MAX);		\
				enum_members(ENUM_STRS_MEMBER)		\
			}						\
		}  enum_name##_strs_init(enum_name##_strs);		\
	}
#else /* !defined(__cplusplus) */
#define ENUM_STRS_MEMBER(s, v, ...) [s] = #s,
#define STRS(enum_name, enum_members) \
	const char *enum_name##_strs[(unsigned) enum_name##_MAX + 1] = {enum_members(ENUM_STRS_MEMBER) 0}
#endif
#define STR2ENUM(enum_name, str) ((enum enum_name) strindex(enum_name##_strs, str, enum_name##_MAX))
#define STRN2ENUM(enum_name, str, len) ((enum enum_name) strnindex(enum_name##_strs, str, len, enum_name##_MAX))

uint32_t
strindex(const char *const *haystack, const char *needle, uint32_t hmax);

uint32_t
strnindex(const char *const *haystack, const char *needle, uint32_t len,
	  uint32_t hmax);

#define nelem(x)     (sizeof((x))/sizeof((x)[0]))
#define field_sizeof(compound_type, field) sizeof(((compound_type *)NULL)->field)
#ifndef lengthof
#define lengthof(array) (sizeof (array) / sizeof ((array)[0]))
#endif

/**
 * An x* variant of a memory allocation function calls the original function
 * and panics if it fails (i.e. it should never return NULL).
 */
#define xalloc_impl(size, func, args...)					\
	({									\
		void *ret = func(args);						\
		if (unlikely(ret == NULL)) {					\
			fprintf(stderr, "Can't allocate %zu bytes at %s:%d",	\
				(size_t)(size), __FILE__, __LINE__);		\
			exit(EXIT_FAILURE);					\
		}								\
		ret;								\
	})

#define xmalloc(size)		xalloc_impl((size), malloc, (size))
#define xcalloc(n, size)	xalloc_impl((n) * (size), calloc, (n), (size))
#define xrealloc(ptr, size)	xalloc_impl((size), realloc, (ptr), (size))
#define xstrdup(s)		xalloc_impl(strlen((s)) + 1, strdup, (s))
#define xstrndup(s, n)		xalloc_impl((n) + 1, strndup, (s), (n))

/** \cond public */

/**
 * Feature test macros for -std=c11 / -std=c++11
 *
 * Sic: clang aims to be gcc-compatible and thus defines __GNUC__
 */
#ifndef __has_feature
#  define __has_feature(x) 0
#endif
#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif
#ifndef __has_attribute
#  define __has_attribute(x) 0
#endif
#ifndef __has_cpp_attribute
#  define __has_cpp_attribute(x) 0
#endif

/**
 * Compiler-independent built-ins.
 *
 * \see https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
 *
 * {{{ Built-ins
 */

/**
 * You may use likely()/unlikely() to provide the compiler with branch
 * prediction information.
 */

#if __has_builtin(__builtin_expect) || defined(__GNUC__)
#  define likely(x)    __builtin_expect(!! (x),1)
#  define unlikely(x)  __builtin_expect(!! (x),0)
#else
#  define likely(x)    (x)
#  define unlikely(x)  (x)
#endif

/**
 * This macro is used to minimize cache-miss latency by moving data into
 * a cache before it is accessed. You can insert calls to prefetch() into
 * code for which you know addresses of data in memory that is likely to be
 * accessed soon. If the target supports them, data prefetch instructions
 * will be generated. If the prefetch is done early enough before the access
 * then the data will be in the cache by the time it is accessed.
 *
 * The value of addr is the address of the memory to prefetch. There are two
 * optional arguments, rw and locality. The value of rw is a compile-time
 * constant one or zero; one means that the prefetch is preparing for a write
 * to the memory address and zero, the default, means that the prefetch is
 * preparing for a read. The value locality must be a compile-time constant
 * integer between zero and three. A value of zero means that the data has
 * no temporal locality, so it need not be left in the cache after the access.
 * A value of three means that the data has a high degree of temporal locality
 * and should be left in all levels of cache possible. Values of one and two
 * mean, respectively, a low or moderate degree of temporal locality.
 * The default is three.
 */
#if __has_builtin(__builtin_prefetch) || defined(__GNUC__)
#  define prefetch(addr, ...) (__builtin_prefetch(addr, __VA_ARGS__))
#else
#  define prefetch(addr, ...) ((void) addr)
#endif

/**
 * If control flow reaches the point of the unreachable(), the program is
 * undefined. It is useful in situations where the compiler cannot deduce
 * the unreachability of the code.
 */
#if __has_builtin(__builtin_unreachable) || defined(__GNUC__)
#  define unreachable() (assert(0), __builtin_unreachable())
#else
#  define unreachable() (assert(0))
#endif

/**
 * The macro offsetof expands to an integral constant expression of
 * type size_t, the value of which is the offset, in bytes, from
 * the beginning of an object of specified type to its specified member,
 * including padding if any.
 */
#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

/**
 * This macro is used to retrieve an enclosing structure from a pointer to
 * a nested element.
 */
#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member  ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member)  );})
#endif

/**
 * C11/C++11 keyword. Appears in the declaration syntax as one of the type
 * specifiers to modify the alignment requirement of the object being
 * declared.
 *
 * Sic: alignas() doesn't work on anonymous structs on gcc < 4.9
 *
 * \example struct obuf { int a; int b; alignas(16) int c; };
 */
#if defined(__cplusplus)
#  include <stdalign.h>
#endif
#if !defined(alignas) && !defined(__alignas_is_defined)
#  if __has_feature(c_alignas) || (defined(__GNUC__) && __GNUC__ >= 5)
#    include <stdalign.h>
#  elif __has_attribute(aligned) || defined(__GNUC__)
#    define alignas(_n) __attribute__((aligned(_n)))
#    define __alignas_is_defined 1
#  else
#    define alignas(_n)
#  endif
#endif

/**
 * C11/C++11 operator. Returns the alignment, in bytes, required for any
 * instance of the type indicated by type-id, which is either complete type,
 * an array type, or a reference type.
 */
#if !defined(alignof) && !defined(__alignof_is_defined)
#  if __has_feature(c_alignof) || (defined(__GNUC__) && __GNUC__ >= 5)
#    include <stdalign.h>
#  elif defined(__GNUC__)
#    define alignof(_T) __alignof(_T)
#    define __alignof_is_defined 1
#  else
#    define alignof(_T) offsetof(struct { char c; _T member; }, member)
#    define __alignof_is_defined 1
#  endif
#endif

/** Built-ins }}} */

/**
 * Compiler-independent function attributes.
 *
 * \see https://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
 * \see http://clang.llvm.org/docs/AttributeReference.html#function-attributes
 * \see http://en.cppreference.com/w/cpp/language/attributes
 *
 * {{{ Function Attributes
 */

/**
 * The MAYBE_UNUSED function attribute can be used to silence -Wunused
 * diagnostics when the entity cannot be removed. For instance, a local
 * variable may exist solely for use in an assert() statement, which makes
 * the local variable unused when NDEBUG is defined.
 *
 * \example int fun(MAYBE_UNUSED int unused_arg);
 */
#if defined(__cplusplus) && __has_cpp_attribute(maybe_unused)
#  define MAYBE_UNUSED [[maybe_unused]]
#elif __has_attribute(unused) || defined(__GNUC__)
#  define MAYBE_UNUSED __attribute__((unused))
#else
#  define MAYBE_UNUSED
#endif

/**
 * A diagnostic is generated when a function is marked with NODISCARD and
 * the function call appears as a potentially-evaluated discarded-value
 * expression that is not explicitly cast to void.
 *
 * \example NODISCARD int function() { return -1 };
 */
#if defined(__cplusplus) && __has_cpp_attribute(nodiscard)
#  define NODISCARD [[nodiscard]]
#elif __has_attribute(warn_unused_result) || defined(__GNUC__)
#  define NODISCARD __attribute__((warn_unused_result))
#else
#  define NODISCARD
#endif

/**
 * This function attribute prevents a function from being considered for
 * inlining.
 *
 * \example NOINLINE int function() { return 0; };
 */
#if __has_attribute(noinline) || defined(__GNUC__)
#  define NOINLINE __attribute__((noinline))
#else
#  define NOINLINE
#endif

/**
 * A function declared as NORETURN shall not return to its caller.
 * The compiler will generate a diagnostic for a function declared as
 * NORETURN that appears to be capable of returning to its caller.
 *
 * \example NORETURN void abort();
 */
#if defined(__cplusplus) && __has_cpp_attribute(noreturn)
#  define NORETURN [[noreturn]]
#elif __has_attribute(noreturn) || defined(__GNUC__)
#  define NORETURN  __attribute__((noreturn))
#else
#  define NORETURN
#endif

/**
 * The DEPRECATED attribute can be applied to a function, a variable, or
 * a type. This is useful when identifying functions, variables, or types
 * that are expected to be removed in a future version of a program.
 */
#if defined(__cplusplus) && __has_cpp_attribute(deprecated)
#  define DEPRECATED(_msg) [[deprecated(_msg)]]
#elif __has_attribute(deprecated) || defined(__GNUC__)
#  define DEPREACTED  __attribute__((deprecated(_msg)))
#else
#  define DEPRECATED(_msg)
#endif

/**
 * The API_EXPORT attribute declares public C API function.
 */
#if defined(__cplusplus) && defined(__GNUC__)
#  define API_EXPORT extern "C" __attribute__ ((nothrow, visibility ("default")))
#elif defined(__cplusplus)
#  define API_EXPORT extern "C"
#elif defined(__GNUC__)
#  define API_EXPORT extern __attribute__ ((nothrow, visibility ("default")))
#else
#  define API_EXPORT extern
#endif

/**
 * The CFORMAT attribute specifies that a function takes printf, scanf,
 * strftime or strfmon style arguments that should be type-checked against
 * a format string.
 *
 * \see https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html
 */
#if __has_attribute(format) || defined(__GNUC__)
#  define CFORMAT(_archetype, _stringindex, _firsttocheck) \
	__attribute__((format(_archetype, _stringindex, _firsttocheck)))
#else
#  define CFORMAT(archetype, stringindex, firsttocheck)
#endif

/**
 * The PACKED qualifier is useful to map a structure to an external data
 * structure, or for accessing unaligned data, but it is generally not
 * useful to save data size because of the relatively high cost of
 * unaligned access on some architectures.
 *
 * \example struct PACKED name { char a; int b; };
 */
#if __has_attribute(packed) || defined(__GNUC__)
#  define PACKED  __attribute__((packed))
#elif defined(__CC_ARM)
#  define PACKED __packed
#else
#  define PACKED
#endif

/** Function Attributes }}} */

/** {{{ Statement Attributes */

/**
 * The fallthrough attribute with a null statement serves as a fallthrough
 * statement. It hints to the compiler that a statement that falls through
 * to another case label, or user-defined label in a switch statement is
 * intentional and thus the -Wimplicit-fallthrough warning must not trigger.
 * The fallthrough attribute may appear at most once in each attribute list,
 * and may not be mixed with other attributes. It can only be used in a switch
 * statement (the compiler will issue an error otherwise), after a preceding
 * statement and before a logically succeeding case label, or user-defined
 * label.
 */
#if defined(__cplusplus) && __has_cpp_attribute(fallthrough)
#  define FALLTHROUGH [[fallthrough]]
#elif __has_attribute(fallthrough) || (defined(__GNUC__) && __GNUC__ >= 7)
#  define FALLTHROUGH __attribute__((fallthrough))
#else
#  define FALLTHROUGH
#endif

/** Statement Attributes }}} */

/** \endcond public */

void close_all_xcpt(int fdc, ...);

#if defined(__GNUC__) && __GNUC__ >= 11
/** Import __gcov_dump function. */
void
__gcov_dump(void);

/** Import __gcov_reset function. */
void
__gcov_reset(void);

static inline void
gcov_flush(void)
{
	__gcov_dump();
	__gcov_reset();
}
#else
/** Import __gcov_flush function. */
void
__gcov_flush(void);

static inline void
gcov_flush(void)
{
	__gcov_flush();
}
#endif

/**
 * Async-signal-safe implementation of printf(), to
 * be able to write messages into the error log
 * inside a signal handler.
 */
ssize_t
fdprintf(int fd, const char *format, ...) __attribute__((format(printf, 2, 3)));

char *
find_path(const char *argv0);

char *
abspath(const char *filename);

int
mkdirpath(char *path);

char *
int2str(long long int val);

void
fpconv_check(void);

enum {
	FPCONV_G_FMT_BUFSIZE = 32,
	FPCONV_G_FMT_MAX_PRECISION = 14
};

extern const char *precision_fmts[];

/**
 * @brief Locale-independent printf("%.(precision)lg")
 * @sa snprintf()
 */
static inline int
fpconv_g_fmt(char *str, double num, int precision)
{
	if (precision <= 0 || precision > FPCONV_G_FMT_MAX_PRECISION)
		precision = FPCONV_G_FMT_MAX_PRECISION;

	const char *fmt = precision_fmts[precision];
	return snprintf(str, FPCONV_G_FMT_BUFSIZE, fmt, num);
}

/**
 * @brief Locale-independent strtod.
 * @sa strtod()
 */
static inline double
fpconv_strtod(const char *nptr, char **endptr)
{
	return strtod(nptr, endptr);
}

#ifndef HAVE_STRLCPY
/**
 * Copy string. Unlike @a strncpy the result string
 * is always null-terminated.
 *
 * @param dst destination buffer.
 * @param src source string.
 * @param size destination buffer size.
 *
 * @return size of @a src string.
 */
size_t
strlcpy(char *dst, const char *src, size_t size);
#endif

/**
 * Check that @a str is valid utf-8 sequence and can be printed
 * unescaped.
 * @param str string
 * @param length string length
 */
int
utf8_check_printable(const char *str, size_t length);

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

#include <time.h>
#include <sys/time.h>
#ifndef HAVE_CLOCK_GETTIME_DECL
/* Declare clock_gettime(). */
int clock_gettime(uint32_t clock_id, struct timespec *tp);
#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			1
#define CLOCK_PROCESS_CPUTIME_ID	2
#define CLOCK_THREAD_CPUTIME_ID		3
#endif

/**
 * Escape special characters in @a data to @a buf
 */
int
json_escape(char *buf, int size, const char *data);

/**
 * Helper macro to handle easily snprintf() result
 */
#define SNPRINT(_total, _fun, _buf, _size, ...) do {				\
	int written =_fun(_buf, _size, ##__VA_ARGS__);				\
	if (written < 0)							\
		return -1;							\
	_total += written;							\
	if (written < _size) {							\
		_buf += written, _size -= written;				\
	} else {								\
		_buf = NULL, _size = 0;						\
	}									\
} while(0)

#define COMPARE_RESULT(a, b) (a < b ? -1 : a > b)

/**
 * Compare LHS with RHS, return a value <0, 0 or >0 depending on the
 * comparison result (strcmp-style).
 * Normally, K==1. If K==-1, the result is inverted (as if LHS and RHS
 * were swapped).
 * K is needed to enable tail call optimization in Release build.
 */
int
double_compare_uint64(double lhs, uint64_t rhs, int k);

/**
 * The same as double_compare_uint64(), but for negative int64_t
 * value. To avoid unnecessary negation for cast to uint64_t to
 * be able to use the other function, and to avoid the undefined
 * behaviour in it, because "(uint64_t)-value" is UB, if value is
 * INT64_MIN.
 */
int
double_compare_nint64(double lhs, int64_t rhs, int k);

/**
 * A shortcut to automatically choose a needed double vs
 * int64/uint64 function.
 */
static inline int
double_compare_int64(double lhs, int64_t rhs, int k)
{
	if (rhs >= 0)
		return double_compare_uint64(lhs, (uint64_t)rhs, k);
	return double_compare_nint64(lhs, rhs, k);
}

/**
 * Compare two operands as int64_t.
 * Needed for qsort.
 */
static inline int
cmp_i64(const void *_a, const void *_b)
{
	const int64_t *a = (const int64_t *) _a, *b = (const int64_t *) _b;
	return COMPARE_RESULT(*a, *b);
}

static inline bool
is_exp_of_two(unsigned n)
{
	return (n & (n - 1)) == 0;
}

/**
 * Put the current thread in sleep for the given number of
 * seconds.
 */
void
thread_sleep(double sec);

#if !defined(__cplusplus) && !defined(static_assert)
# define static_assert _Static_assert
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_UTIL_H_INCLUDED */
