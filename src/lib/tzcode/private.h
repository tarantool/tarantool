/* Private header for tzdb code.  */

#ifndef PRIVATE_H
#define PRIVATE_H

/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
*/

/*
** This header is for use ONLY with the time conversion code.
** There is no guarantee that it will remain unchanged,
** or that it will remain at all.
** Do NOT copy it to any system include directory.
** Thank you!
*/

/* This string was in the Factory zone through version 2016f.  */
#define GRANDPARENTED	"Local time zone must be set--see zic manual page"

/*
** Defaults for preprocessor symbols.
** You can override these in your C compiler options, e.g. '-DHAVE_GETTEXT=1'.
*/

#if !defined HAVE_GENERIC && defined __has_extension
# if __has_extension(c_generic_selections)
#  define HAVE_GENERIC 1
# else
#  define HAVE_GENERIC 0
# endif
#endif
/* _Generic is buggy in pre-4.9 GCC.  */
#if !defined HAVE_GENERIC && defined __GNUC__
# define HAVE_GENERIC (4 < __GNUC__ + (9 <= __GNUC_MINOR__))
#endif
#ifndef HAVE_GENERIC
# define HAVE_GENERIC (201112 <= __STDC_VERSION__)
#endif

#ifndef HAVE_MALLOC_ERRNO
#define HAVE_MALLOC_ERRNO 1
#endif

#ifndef NETBSD_INSPIRED
# define NETBSD_INSPIRED 1
#endif

/* Enable tm_gmtoff, tm_zone, and environ on GNUish systems.  */
#define _GNU_SOURCE 1

/*
** Nested includes
*/

#include <limits.h>		/* for CHAR_BIT et al. */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>		/* for time_t */
#include <time.h>

#include <errno.h>

#ifndef EOVERFLOW
# define EOVERFLOW EINVAL
#endif

/* Unlike <ctype.h>'s isdigit, this also works if c < 0 | c > UCHAR_MAX. */
#define is_digit(c) ((unsigned)(c) - '0' <= 9)

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#if 3 <= __GNUC__
# define ATTRIBUTE_CONST __attribute__((const))
# define ATTRIBUTE_MALLOC __attribute__((__malloc__))
# define ATTRIBUTE_PURE __attribute__((__pure__))
# define ATTRIBUTE_FORMAT(spec) __attribute__((__format__ spec))
#else
# define ATTRIBUTE_CONST /* empty */
# define ATTRIBUTE_MALLOC /* empty */
# define ATTRIBUTE_PURE /* empty */
# define ATTRIBUTE_FORMAT(spec) /* empty */
#endif

/*
** Workarounds for compilers/systems.
*/

#ifndef EPOCH_LOCAL
# define EPOCH_LOCAL 0
#endif
#ifndef EPOCH_OFFSET
# define EPOCH_OFFSET 0
#endif
#ifndef RESERVE_STD_EXT_IDS
# define RESERVE_STD_EXT_IDS 0
#endif

/* If standard C identifiers with external linkage (e.g., localtime)
   are reserved and are not already being renamed anyway, rename them
   as if compiling with '-Dtime_tz=time_t'.  */
#if !defined time_tz && RESERVE_STD_EXT_IDS && USE_LTZ
# define time_tz time_t
#endif

/*
** Compile with -Dtime_tz=T to build the tz package with a private
** time_t type equivalent to T rather than the system-supplied time_t.
** This debugging feature can test unusual design decisions
** (e.g., time_t wider than 'long', or unsigned time_t) even on
** typical platforms.
*/
#if defined time_tz || EPOCH_LOCAL || EPOCH_OFFSET != 0
# define TZ_TIME_T 1
#else
# define TZ_TIME_T 0
#endif

#if defined LOCALTIME_IMPLEMENTATION && TZ_TIME_T
static time_t sys_time(time_t *x) { return time(x); }
#endif

#ifndef HAVE_DECL_ENVIRON
# if defined environ || defined __USE_GNU
#  define HAVE_DECL_ENVIRON 1
# else
#  define HAVE_DECL_ENVIRON 0
# endif
#endif

#if !HAVE_DECL_ENVIRON
extern char **environ;
#endif

#if 2 <= HAVE_TZNAME + (TZ_TIME_T || !HAVE_POSIX_DECLS)
extern char *tzname[];
#endif
#if 2 <= USG_COMPAT + (TZ_TIME_T || !HAVE_POSIX_DECLS)
extern long timezone;
extern int daylight;
#endif
#if 2 <= ALTZONE + (TZ_TIME_T || !HAVE_POSIX_DECLS)
extern long altzone;
#endif

#ifdef STD_INSPIRED
# if TZ_TIME_T || !defined offtime
struct tm *offtime(time_t const *, long);
# endif
# if TZ_TIME_T || !defined timegm
time_t timegm(struct tm *);
# endif
# if TZ_TIME_T || !defined timelocal
time_t timelocal(struct tm *);
# endif
# if TZ_TIME_T || !defined timeoff
time_t timeoff(struct tm *, long);
# endif
# if TZ_TIME_T || !defined time2posix
time_t time2posix(time_t);
# endif
# if TZ_TIME_T || !defined posix2time
time_t posix2time(time_t);
# endif
#endif

/* Infer TM_ZONE on systems where this information is known, but suppress
   guessing if NO_TM_ZONE is defined.  Similarly for TM_GMTOFF.  */
#if (defined __GLIBC__ \
     || defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ \
     || (defined __APPLE__ && defined __MACH__))
# if !defined TM_GMTOFF && !defined NO_TM_GMTOFF
#  define TM_GMTOFF tm_gmtoff
# endif
# if !defined TM_ZONE && !defined NO_TM_ZONE
#  define TM_ZONE tm_zone
# endif
#endif

/*
** Define functions that are ABI compatible with NetBSD but have
** better prototypes.  NetBSD 6.1.4 defines a pointer type timezone_t
** and labors under the misconception that 'const timezone_t' is a
** pointer to a constant.  This use of 'const' is ineffective, so it
** is not done here.  What we call 'struct state' NetBSD calls
** 'struct __state', but this is a private name so it doesn't matter.
*/
#if NETBSD_INSPIRED
typedef struct state *timezone_t;
struct tm *localtime_rz(timezone_t restrict, time_t const *restrict,
			struct tm *restrict);
time_t mktime_z(timezone_t restrict, struct tm *restrict);
timezone_t tzalloc(char const *);
void tzfree(timezone_t);
# ifdef STD_INSPIRED
#  if TZ_TIME_T || !defined posix2time_z
time_t posix2time_z(timezone_t, time_t) ATTRIBUTE_PURE;
#  endif
#  if TZ_TIME_T || !defined time2posix_z
time_t time2posix_z(timezone_t, time_t) ATTRIBUTE_PURE;
#  endif
# endif
#endif

/*
** Finally, some convenience items.
*/

#define TYPE_BIT(type) (sizeof(type) * CHAR_BIT)
#define TYPE_SIGNED(type) (((type)-1) < 0)
#define TWOS_COMPLEMENT(t) ((t) ~(t)0 < 0)

/* Max and min values of the integer type T, of which only the bottom
   B bits are used, and where the highest-order used bit is considered
   to be a sign bit if T is signed.  */
#define MAXVAL(t, b)						\
  ((t) (((t) 1 << ((b) - 1 - TYPE_SIGNED(t)))			\
	- 1 + ((t) 1 << ((b) - 1 - TYPE_SIGNED(t)))))
#define MINVAL(t, b)						\
  ((t) (TYPE_SIGNED(t) ? - TWOS_COMPLEMENT(t) - MAXVAL(t, b) : 0))

/* The extreme time values, assuming no padding.  */
#define TIME_T_MIN_NO_PADDING MINVAL(time_t, TYPE_BIT(time_t))
#define TIME_T_MAX_NO_PADDING MAXVAL(time_t, TYPE_BIT(time_t))

/* The extreme time values.  These are macros, not constants, so that
   any portability problems occur only when compiling .c files that use
   the macros, which is safer for applications that need only zdump and zic.
   This implementation assumes no padding if time_t is signed and
   either the compiler lacks support for _Generic or time_t is not one
   of the standard signed integer types.  */
#if HAVE_GENERIC
# define TIME_T_MIN \
    _Generic((time_t) 0, \
	     signed char: SCHAR_MIN, short: SHRT_MIN, \
	     int: INT_MIN, long: LONG_MIN, long long: LLONG_MIN, \
	     default: TIME_T_MIN_NO_PADDING)
# define TIME_T_MAX \
    (TYPE_SIGNED(time_t) \
     ? _Generic((time_t) 0, \
		signed char: SCHAR_MAX, short: SHRT_MAX, \
		int: INT_MAX, long: LONG_MAX, long long: LLONG_MAX, \
		default: TIME_T_MAX_NO_PADDING)			    \
     : (time_t) -1)
#else
# define TIME_T_MIN TIME_T_MIN_NO_PADDING
# define TIME_T_MAX TIME_T_MAX_NO_PADDING
#endif

/*
** 302 / 1000 is log10(2.0) rounded up.
** Subtract one for the sign bit if the type is signed;
** add one for integer division truncation;
** add one more for a minus sign if the type is signed.
*/
#define INT_STRLEN_MAXIMUM(type) \
	((TYPE_BIT(type) - TYPE_SIGNED(type)) * 302 / 1000 + \
	1 + TYPE_SIGNED(type))

/*
** INITIALIZE(x)
*/

#ifdef GCC_LINT
# define INITIALIZE(x)	((x) = 0)
#else
# define INITIALIZE(x)
#endif

/* Whether memory access must strictly follow the C standard.
   If 0, it's OK to read uninitialized storage so long as the value is
   not relied upon.  Defining it to 0 lets mktime access parts of
   struct tm that might be uninitialized, as a heuristic when the
   standard doesn't say what to return and when tm_gmtoff can help
   mktime likely infer a better value.  */
#ifndef UNINIT_TRAP
# define UNINIT_TRAP 0
#endif

#ifdef DEBUG
# define UNREACHABLE() abort()
#elif 4 < __GNUC__ + (5 <= __GNUC_MINOR__)
# define UNREACHABLE() __builtin_unreachable()
#elif defined __has_builtin
# if __has_builtin(__builtin_unreachable)
#  define UNREACHABLE() __builtin_unreachable()
# endif
#endif
#ifndef UNREACHABLE
# define UNREACHABLE() ((void) 0)
#endif

/* Handy macros that are independent of tzfile implementation.  */

#define YEARSPERREPEAT 400 /* years before a Gregorian repeat */

#define SECSPERMIN     60
#define MINSPERHOUR    60
#define HOURSPERDAY    24
#define DAYSPERWEEK    7
#define DAYSPERNYEAR   365
#define DAYSPERLYEAR   366
#define SECSPERHOUR    (SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY     ((int_fast32_t) SECSPERHOUR * HOURSPERDAY)
#define MONTHSPERYEAR  12

#define YEARSPERREPEAT		400	/* years before a Gregorian repeat */
#define DAYSPERREPEAT		((int_fast32_t) 400 * 365 + 100 - 4 + 1)
#define SECSPERREPEAT		((int_fast64_t) DAYSPERREPEAT * SECSPERDAY)
#define AVGSECSPERYEAR		(SECSPERREPEAT / YEARSPERREPEAT)

#define TM_SUNDAY      0
#define TM_MONDAY      1
#define TM_TUESDAY     2
#define TM_WEDNESDAY   3
#define TM_THURSDAY    4
#define TM_FRIDAY      5
#define TM_SATURDAY    6
#define TM_JANUARY     0
#define TM_FEBRUARY    1
#define TM_MARCH       2
#define TM_APRIL       3
#define TM_MAY	       4
#define TM_JUNE	       5
#define TM_JULY	       6
#define TM_AUGUST      7
#define TM_SEPTEMBER   8
#define TM_OCTOBER     9
#define TM_NOVEMBER    10
#define TM_DECEMBER    11

#define TM_YEAR_BASE	1900
#define TM_WDAY_BASE	TM_MONDAY

#define EPOCH_YEAR     1970
#define EPOCH_WDAY     TM_THURSDAY

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

/*
** Since everything in isleap is modulo 400 (or a factor of 400), we know that
**	isleap(y) == isleap(y % 400)
** and so
**	isleap(a + b) == isleap((a + b) % 400)
** or
**	isleap(a + b) == isleap(a % 400 + b % 400)
** This is true even if % means modulo rather than Fortran remainder
** (which is allowed by C89 but not by C99 or later).
** We use this to avoid addition overflow problems.
*/
#define isleap_sum(a, b) isleap((a) % 400 + (b) % 400)

#endif /* !defined PRIVATE_H */
