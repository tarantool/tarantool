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

/*
** Defaults for preprocessor symbols.
** You can override these in your C compiler options, e.g. '-DHAVE_GETTEXT=1'.
*/

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

/*
** Finally, some convenience items.
*/

#define TYPE_BIT(type) (sizeof(type) * CHAR_BIT)
#define TYPE_SIGNED(type) (((type)-1) < 0)
#define TWOS_COMPLEMENT(t) ((t) ~(t)0 < 0)

/*
** 302 / 1000 is log10(2.0) rounded up.
** Subtract one for the sign bit if the type is signed;
** add one for integer division truncation;
** add one more for a minus sign if the type is signed.
*/
#define INT_STRLEN_MAXIMUM(type) \
	((TYPE_BIT(type) - TYPE_SIGNED(type)) * 302 / 1000 + \
	1 + TYPE_SIGNED(type))

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
#define TM_YEAR_BASE   1900
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
