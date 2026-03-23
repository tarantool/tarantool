/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Gary Mills
 * Copyright 2011, Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 1994 Powerdog Industries.  All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY POWERDOG INDUSTRIES ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE POWERDOG INDUSTRIES BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Powerdog Industries.
 */

#include <sys/cdefs.h>
#include <stdbool.h>

#ifndef lint
#ifndef NOID
static char copyright[] __attribute__((unused)) =
"@(#) Copyright (c) 1994 Powerdog Industries.  All rights reserved.";
static char sccsid[] __attribute__((unused)) =
"@(#)strptime.c	0.1 (Powerdog) 94/03/27";
#endif /* !defined NOID */
#endif /* not lint */

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "private.h"
#include "timelocal.h"
#include "tzcode.h"
#include "timezone.h"
#include "trivia/util.h"

enum flags {
	FLAG_YEAR = 1 << 1,
	FLAG_MONTH = 1 << 2,
	FLAG_YDAY = 1 << 3,
	FLAG_MDAY = 1 << 4,
	FLAG_WDAY = 1 << 5,
	FLAG_EPOCH = 1 << 6,
	FLAG_NSEC = 1 << 7,
	FLAG_WEEK = 1 << 8,
	FLAG_SEC = 1 << 9,
	FLAG_MIN = 1 << 10,
	FLAG_HOUR = 1 << 11,
	FLAG_TZ = 1 << 12,
	FLAG_TEXT_WDAY = 1 << 13,
	FLAG_TEXT_MONTH = 1 << 14,

	FLAGS_TIME = FLAG_HOUR | FLAG_MIN | FLAG_SEC | FLAG_NSEC,
	FLAGS_DATE = FLAG_YEAR | FLAG_MONTH | FLAG_MDAY |
		     FLAG_YDAY |
		     FLAG_WEEK | FLAG_WDAY,

};

/*
 * Calculate the week day of the first day of a year. Valid for
 * the Gregorian calendar, which began Sept 14, 1752 in the UK
 * and its colonies. Ref:
 * http://en.wikipedia.org/wiki/Determination_of_the_day_of_the_week
 */

static int
first_wday_of(int year)
{
	return ((2 * (3 - (year / 100) % 4)) + (year % 100) +
		((year % 100) / 4) + (isleap(year) ? 6 : 0) + 1) % 7;
}

#define NUM_(N, buf) \
	({	size_t _len = N; \
		long val = 0; \
		long sign = +1; \
		if ('-' == *buf) { \
			buf++; \
			sign = -1; \
		} \
		for (; _len > 0 && *buf != 0 && is_digit((u_char)*buf); \
		     buf++, _len--) \
			val = val * 10 + (*buf - '0'); \
		sign * val; \
	})

#define NUM2(buf) NUM_(2, buf)
#define NUM3(buf) NUM_(3, buf)

struct tnt_tm_ex {
	struct tnt_tm base;
	/* Week [0..53]. */
	int week;
	/* Comes with week: TM_SUNDAY or TM_MONDAY. */
	int first_day_of_week;
	/* Parsed tm fields. */
	unsigned flags;
	/* Next of last parsed char, if no error. */
	const char *end;
};

static inline void
tnt_strptime_set_year(struct tnt_tm_ex *__restrict tm, int year)
{
	tm->base.tm_year = year;
	tm->flags |= FLAG_YEAR;
}

static inline void
tnt_strptime_set_month(struct tnt_tm_ex *__restrict tm, int month)
{
	tm->base.tm_mon = month;
	tm->flags |= FLAG_MONTH;
}

static inline void
tnt_strptime_set_mday(struct tnt_tm_ex *__restrict tm, int mday)
{
	tm->base.tm_mday = mday;
	tm->flags |= FLAG_MDAY;
}

static inline void
tnt_strptime_set_yday(struct tnt_tm_ex *__restrict tm, int yday)
{
	tm->base.tm_yday = yday;
	tm->flags |= FLAG_YDAY;
}

static inline void
tnt_strptime_set_week(struct tnt_tm_ex *__restrict tm,
		      int week, int first_day_of_week)
{
	tm->week = week;
	tm->first_day_of_week = first_day_of_week;
	tm->flags |= FLAG_WEEK;
}

static inline void
tnt_strptime_set_wday(struct tnt_tm_ex *__restrict tm, int wday)
{
	tm->base.tm_wday = wday;
	tm->flags |= FLAG_WDAY;
}

static inline void
tnt_strptime_set_hour(struct tnt_tm_ex *__restrict tm, int hour)
{
	tm->base.tm_hour = hour;
	tm->flags |= FLAG_HOUR;
}

static inline void
tnt_strptime_set_min(struct tnt_tm_ex *__restrict tm, int min)
{
	tm->base.tm_min = min;
	tm->flags |= FLAG_MIN;
}

static inline void
tnt_strptime_set_sec(struct tnt_tm_ex *__restrict tm, int sec)
{
	tm->base.tm_sec = sec;
	tm->flags |= FLAG_SEC;
}

static inline void
tnt_strptime_set_nsec(struct tnt_tm_ex *__restrict tm, int nsec)
{
	tm->base.tm_nsec = nsec;
	tm->flags |= FLAG_NSEC;
}

static inline void
tnt_strptime_set_epoch(struct tnt_tm_ex *__restrict tm, int64_t epoch)
{
	tm->base.tm_epoch = epoch;
	tm->flags |= FLAG_EPOCH;
}

static inline void
tnt_strptime_set_tz(struct tnt_tm_ex *__restrict tm, int tzindex, int gmtoff)
{
	tm->base.tm_tzindex = tzindex;
	tm->base.tm_gmtoff = gmtoff;
	tm->flags |= FLAG_TZ;
}

static int
tnt_strptime_parse(const char *__restrict buf, const char *__restrict fmt,
		   struct tnt_tm_ex *__restrict tm)
{
	char c;
	int i, j, len;
	int Ealternative, Oalternative;
	int century = -1;
	int year = -1;
	const char *ptr = fmt;

	printf("%s: fmt=%s buf=%s\n", __func__, fmt, buf);

	while (*ptr != 0) {
		c = *ptr++;

		if (c != '%') {
			/* Eat up white-space in buffer and in format. */
			if (isspace((u_char)c)) {
				while (*buf != 0 && isspace((u_char)*buf))
					buf++;
			}
			else if (c != *buf++)
				return -1;
			continue;
		}

		Ealternative = 0;
		Oalternative = 0;
	label:
		c = *ptr++;
		switch (c) {
		case '%':
			if (*buf++ != '%')
				return -2;
			break;

#define RECURSE(sub_fmt)					  \
			i = tnt_strptime_parse(buf, sub_fmt, tm); \
			if (i < 0)				  \
				return -100 + i;		  \
			buf = tm->end; 				  \
			break
		case '+':
			RECURSE(Locale->date_fmt);

		case 'C':
			if (!is_digit((u_char)*buf))
				return -3;

			/* XXX This will break for 3-digit centuries. */
			i = NUM2(buf);

			century = i;

			break;

		case 'c':
			RECURSE(Locale->c_fmt);

		case 'D':
			RECURSE("%m/%d/%y");

		case 'E':
			if (Ealternative || Oalternative)
				break;
			Ealternative++;
			goto label;

		case 'O':
			if (Ealternative || Oalternative)
				break;
			Oalternative++;
			goto label;

		case 'v':
			RECURSE("%e-%b-%Y");

		case 'F':
			RECURSE("%Y-%m-%d");

		case 'R':
			RECURSE("%H:%M");

		case 'r':
			RECURSE(Locale->ampm_fmt);

		case 'T':
			RECURSE("%H:%M:%S");

		case 'X':
			RECURSE(Locale->X_fmt);

		case 'x':
			RECURSE(Locale->x_fmt);
#undef RECURSE
		case 'j':
			if (!is_digit((u_char)*buf))
				return -4;

			i = NUM3(buf);
			if (i < 1 || i > 366)
				return -5;

			tnt_strptime_set_yday(tm, i - 1);
			break;

		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			for (; *ptr != 0 && is_digit((u_char)*ptr); ptr++)
				;

			c = *ptr++;
			if (c != 'f')
				return -6;
			/* fallthru */
		case 'f':
			if (!is_digit((u_char)*buf))
				return -7;

			len = 9;
			for (i = 0; len && *buf != 0 && is_digit((u_char)*buf);
			     buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			while (len) {
				i *= 10;
				len--;
			}
			tnt_strptime_set_nsec(tm, i);
			break;

		case 'M':
		case 'S':
			if (*buf == 0 || isspace((u_char)*buf))
				break;

			if (!is_digit((u_char)*buf))
				return -8;

			i = NUM2(buf);

			if (c == 'M') {
				if (i > 59)
					return -9;
				tm->base.tm_min = i;
				tnt_strptime_set_min(tm, i);
			} else {
				if (i > 60)
					return -10;
				tnt_strptime_set_sec(tm, i);
			}

			break;

		case 'H':
		case 'I':
		case 'k':
		case 'l':
			/*
			 * %k and %l specifiers are documented as being
			 * blank-padded.  However, there is no harm in
			 * allowing zero-padding.
			 *
			 * XXX %k and %l specifiers may gobble one too many
			 * digits if used incorrectly.
			 */

			len = 2;
			if ((c == 'k' || c == 'l') && isblank((u_char)*buf)) {
				buf++;
				len = 1;
			}

			if (!is_digit((u_char)*buf))
				return -11;

			i = NUM_(len, buf);
			if (c == 'H' || c == 'k') {
				if (i > 23)
					return -12;
			} else if (i == 0 || i > 12)
				return -13;

			tnt_strptime_set_hour(tm, i);
			break;

		case 'p':
			/*
			 * XXX This is bogus if parsed before hour-related
			 * specifiers.
			 */
			if ((tm->flags & FLAG_HOUR) == 0)
				return -14;
			if (tm->base.tm_hour > 12)
				return -15;

			len = strlen(Locale->am);
			if (strncasecmp(buf, Locale->am, len) == 0) {
				if (tm->base.tm_hour == 12)
					tm->base.tm_hour = 0;
				buf += len;
				break;
			}

			len = strlen(Locale->pm);
			if (strncasecmp(buf, Locale->pm, len) == 0) {
				if (tm->base.tm_hour != 12)
					tm->base.tm_hour += 12;
				buf += len;
				break;
			}

			return -16;

		case 'A':
		case 'a':
			for (i = 0; i < DAYSPERWEEK; i++) {
				len = strlen(Locale->weekday[i]);
				if (strncasecmp(buf, Locale->weekday[i], len) ==
				    0)
					break;
				len = strlen(Locale->wday[i]);
				if (strncasecmp(buf, Locale->wday[i], len) == 0)
					break;
			}
			if (i == DAYSPERWEEK)
				return -17;

			buf += len;
			tnt_strptime_set_wday(tm, i);
			tm->flags |= FLAG_TEXT_WDAY;
			break;

		case 'U':
		case 'W':
			/*
			 * XXX This is bogus, as we can not assume any valid
			 * information present in the tm structure at this
			 * point to calculate a real value, so just check the
			 * range for now.
			 */
			if (!is_digit((u_char)*buf))
				return -18;

			i = NUM2(buf);
			if (i > 53)
				return -19;

			if (c == 'U')
				j = TM_SUNDAY;
			else
				j = TM_MONDAY;

			tnt_strptime_set_week(tm, i, j);
			break;

		case 'u':
		case 'w':
			if (!is_digit((u_char)*buf))
				return -20;

			i = *buf++ - '0';
			if (i < 0 || i > 7 || (c == 'u' && i < 1) ||
			    (c == 'w' && i > 6))
				return -21;

			tnt_strptime_set_wday(tm, i % 7);
			break;

		case 'e':
			/*
			 * With %e format, our strftime(3) adds a blank space
			 * before single digits.
			 */
			if (*buf != 0 && isspace((u_char)*buf))
				buf++;
			/* FALLTHROUGH */
		case 'd':
			/*
			 * The %e specifier was once explicitly documented as
			 * not being zero-padded but was later changed to
			 * equivalent to %d.  There is no harm in allowing
			 * such padding.
			 *
			 * XXX The %e specifier may gobble one too many
			 * digits if used incorrectly.
			 */
			if (!is_digit((u_char)*buf))
				return -22;

			i = NUM2(buf);
			if (i == 0 || i > 31)
				return -23;

			tnt_strptime_set_mday(tm, i);
			break;

		case 'B':
		case 'b':
		case 'h':
			for (i = 0; i < MONTHSPERYEAR; i++) {
				if (Oalternative) {
					if (c == 'B') {
						len = strlen(
							Locale->alt_month[i]);
						if (strncasecmp(
							    buf,
							    Locale->alt_month
								    [i],
							    len) == 0)
							break;
					}
				} else {
					len = strlen(Locale->month[i]);
					if (strncasecmp(buf, Locale->month[i],
							len) == 0)
						break;
				}
			}
			/*
			 * Try the abbreviated month name if the full name
			 * wasn't found and Oalternative was not requested.
			 */
			if (i == MONTHSPERYEAR && !Oalternative) {
				for (i = 0; i < MONTHSPERYEAR; i++) {
					len = strlen(Locale->mon[i]);
					if (strncasecmp(buf, Locale->mon[i],
							len) == 0)
						break;
				}
			}
			if (i == MONTHSPERYEAR)
				return -24;

			buf += len;

			tnt_strptime_set_month(tm, i);
			tm->flags |= FLAG_TEXT_MONTH;
			break;

		case 'm':
			if (!is_digit((u_char)*buf))
				return -25;

			i = NUM2(buf);
			if (i < 1 || i > 12)
				return -26;

			tnt_strptime_set_month(tm, i - 1);
			break;

		case 's': {
			char *cp;
			long n;

			n = strtol(buf, &cp, 10);
			if (buf == cp && n == 0) {
				return -27;
			}
			buf = cp;
			tnt_strptime_set_epoch(tm, n);
		} break;

		case 'G': /* ISO 8601 year (four digits) */
		case 'g': /* ISO 8601 year (two digits) */
		case 'Y':
		case 'y':
			if (*buf == 0 || isspace((u_char)*buf))
				break;

			if (*buf != '-' && !is_digit((u_char)*buf))
				return -28;

			len = (c == 'Y' || c == 'G') ? 7 : 2;
			i = NUM_(len, buf);
			if (c == 'Y' || c == 'G')
				century = i / 100;
			year = i % 100;

			break;

		case 'Z': {
			const char *cp;
			char *zonestr;

			/* TODO: Olson DB specific cases e.g. "Europe/Moscow"
		  	 * doesn't fit for now:
			 */
			for (cp = buf; *cp && isupper((u_char)*cp); ++cp)
				/* empty */;
			if (cp - buf) {
				zonestr = alloca(cp - buf + 1);
				strlcpy(zonestr, buf, cp - buf + 1);

				const struct date_time_zone *zone;
				/* TODO Actually we need to postpone calculations
				 * until the epoch is obtained.
				*/
				struct tnt_tm tmp = tm->base;
				ssize_t n = timezone_tm_lookup(zonestr, cp - buf,
							       &zone, &tmp);
				if (n <= 0)
					return -29;

				buf += cp - buf;
				tnt_strptime_set_tz(tm, tmp.tm_tzindex,
						    tmp.tm_gmtoff);
			}
		} break;

		case 'z': {

			/* Even for %z format specifier we better to accept
			 * Zulu timezone as default Tarantool shortcut for
			 * +00:00 offset.
			 */
			if (*buf == 'Z') {
				buf++;
				/* TODO: Z have tzindex. */
				tnt_strptime_set_tz(tm, 0, 0);
				break;
			}
			int sign = 1;

			if (*buf != '+') {
				if (*buf == '-')
					sign = -1;
				else
					return -30;
			}

			buf++;
			i = 0;
			for (len = 4; len > 0; len--) {
				if (is_digit((u_char)*buf)) {
					i *= 10;
					i += *buf - '0';
					buf++;
				} else if (len == 2) {
					i *= 100;
					break;
				} else
					return -31;
			}

			/* Check as in dt_parse_iso_zone_lenient().
			 * Leave more precise check for datetime.c,
			   where MIN_TZOFFSET and MAX_TZOFFSET are defined.
			 */
			if (i > 2359 || (i % 100) >= 60)
				return -32;
			j = sign * ((i / 100) * 3600 + i % 100 * 60);
			tnt_strptime_set_tz(tm, 0, j);
		} break;

		case 'n':
		case 't':
			while (isspace((u_char)*buf))
				buf++;
			break;

		default:
			return -33;
		}
	}

	if (century != -1 || year != -1) {
		if (year == -1)
			year = 0;
		if (century == -1) {
			if (year < 69)
				year += 100;
		} else
			year += century * 100 - TM_YEAR_BASE;
		tnt_strptime_set_year(tm, year);
	}

	tm->end = buf;
	return 0;
}

static int start_of_month[2][13] = {
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

static void
tnt_strptime_calc_mon_day_by_yday(struct tnt_tm_ex *t)
{
	assert((t->flags & (FLAG_YEAR | FLAG_YDAY)) ==
	       (FLAG_YEAR | FLAG_YDAY));
	struct tnt_tm *tm = &t->base;
	assert(tm->tm_yday >= 0 && tm->tm_yday <= 365);

	int i = 0;
	while (i <= 12 &&
		tm->tm_yday >=
			start_of_month[isleap(tm->tm_year +
						TM_YEAR_BASE)][i])
		i++;
	/* Must we raize an error here instead of
	 * applying 366 day for 365 days year?
	 */
	if (i > 12) {
		i = 1;
		tm->tm_yday -= start_of_month[isleap(
			tm->tm_year + TM_YEAR_BASE)][12];
		tm->tm_year++;
	}
	tnt_strptime_set_month(t, i - 1);
	tnt_strptime_set_mday(t, tm->tm_yday -
		start_of_month[isleap(tm->tm_year +
					TM_YEAR_BASE)]
				[tm->tm_mon] +
		1);
}

static int
tnt_strptime_calc_yday_by_week_wday(struct tnt_tm_ex *t)
{
	assert((t->flags & (FLAG_YEAR | FLAG_WEEK)) ==
	       (FLAG_YEAR | FLAG_WEEK));
	struct tnt_tm *tm = &t->base;
	assert(t->week >= 0 && t->week <= 53);
	/* Unparsed wday is zero by default. */
	assert(tm->tm_wday >= 0 && tm->tm_wday <= 6);
	printf("%s: week=%d fdw=%d wday=%d\n", __func__, t->week, t->first_day_of_week, tm->tm_wday);

	int tmpwday, tmpyday, fwo;

	fwo = first_wday_of(tm->tm_year + TM_YEAR_BASE);
	/* First day of the year and first day of first week
	 * are the same. So, incomplete week (week 0)
	 * is not exists in this year and became invalid.
	 */
	if (t->week == 0 && fwo == t->first_day_of_week)
		return -1;

	/* Set the date to the first Sunday (or Monday)
		* of the specified week of the year.
		*/
	tmpwday = (t->flags & FLAG_WDAY) ? tm->tm_wday : t->first_day_of_week;
	tmpyday = (7 - fwo + t->first_day_of_week) % 7 +
		(t->week - 1) * 7 +
		(tmpwday - t->first_day_of_week + 7) % 7;
	/* Impossible wday for incomplete week (week 0). */
	if (tmpyday < 0) {
		assert(t->week == 0);
		/* wday is def and is invalid. */
		if (t->flags & FLAG_WDAY)
			return -2;
		/* wday isn't def, use start of week 0 (first day of year). */
		tmpyday = 0;
	}

	tnt_strptime_set_yday(t, tmpyday);
	return 0;
}

/* TODO replace with ssize_t for error handling */
char *
tnt_strptime(const char *__restrict buf, const char *__restrict fmt,
	     struct tnt_tm *__restrict tm)
{
	struct tnt_tm_ex t;
	memset(&t, 0, sizeof(t));
	memset(tm, 0, sizeof(*tm));
	int res = tnt_strptime_parse(buf, fmt, &t);
	printf("%s: parse res=%d flags=%X\n", __func__, res, t.flags);
	if (res != 0) {
		return NULL;
	}

	/* Handle timestamp case. */
	if ((t.flags & FLAG_EPOCH) != 0) {
		if ((t.flags & FLAGS_DATE) != 0) {
			/* TODO: ssize_t code for mix y/m/d with timestamp. */
			printf("%s: date + epoch error\n", __func__);
			return NULL;
		}
		if ((t.flags & FLAGS_TIME) != 0) {
			/* TODO: ssize_t code for mix h/m/s with timestamp. */
			printf("%s: time + epoch error\n", __func__);
			return NULL;
		}
		tm->tm_epoch = t.base.tm_epoch;
		printf("%s: res epoch=%ld\n", __func__, tm->tm_epoch);
		goto end;
	}

	/* Handle date-time case. */

	/* Handle date. */
#define DEFAULT_YEAR (EPOCH_YEAR - TM_YEAR_BASE)
	switch (t.flags & FLAGS_DATE) {
	/* Calendar date year or century ISO-8601:2019 5.2.2.3 (c/e). */
	case FLAG_YEAR:
		tnt_strptime_set_month(&t, 0);
		/* fallthru */
	/* Calendar date month ISO-8601:2019 5.2.2.3 (b). */
	case FLAG_YEAR | FLAG_MONTH:
		tnt_strptime_set_mday(&t, 1);
		/* fallthru */
	/* Calendar date day ISO-8601:2019 5.2.2.3 (a). */
	case FLAG_YEAR | FLAG_MONTH | FLAG_MDAY:
		break;

	/* Ordinal date ISO-8601:2019 5.2.3.2 (a). */
	case FLAG_YEAR | FLAG_YDAY:
	ordinal_date:
		tnt_strptime_calc_mon_day_by_yday(&t);
		break;

	/* Week date week ISO-8601:2019 5.2.4.3 (b). */
	case FLAG_YEAR | FLAG_WEEK:
		/* Undefined wday case is handled in
		 * tnt_strptime_calc_yday_by_week_wday().
		 */
		/* fallthru */
	/* Week date day ISO-8601:2019 5.2.4.3 (a). */
	case FLAG_YEAR | FLAG_WEEK | FLAG_WDAY:
	week_date:
		res = tnt_strptime_calc_yday_by_week_wday(&t);
		if (res != 0) {
			printf("%s: calc yday error res=%d\n", __func__, res);
			return NULL;
		}
		printf("%s: yday=%d\n", __func__, t.base.tm_yday);
		tnt_strptime_calc_mon_day_by_yday(&t);
		break;

	/* Allow non standart cases. */

	/* Calendar date - year and mday undefined. */
	case FLAG_MONTH:
		/* This special case allows text mon to mon conversion. */
		if ((t.flags & FLAG_TEXT_MONTH) != 0) {
			/*TODO*/
		}
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		tnt_strptime_set_mday(&t, 1);
		break;

	/* Calendar date - year and month undefined. */
	case FLAG_MDAY:
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		tnt_strptime_set_month(&t, 0);
		break;

	/* Calendar date - year undefined */
	case FLAG_MONTH | FLAG_MDAY:
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		break;

	/* Ordinal date - year undefined */
	case FLAG_YDAY:
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		goto ordinal_date;

	/* Week date - year and wday undefined */
	case FLAG_WEEK:
	/* Week date - year undefined */
	case FLAG_WEEK | FLAG_WDAY:
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		goto week_date;

	/* Week date - year and week undefined */
	case FLAG_WDAY:
		/* This special case allows text wday to wday conversion. */
		if ((t.flags & FLAG_TEXT_WDAY) != 0) {
			/*TODO*/
		}
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		/* First full week of DEFAULT_YEAR. */
		tnt_strptime_set_week(&t, 1, TM_SUNDAY);
		goto week_date;

	/* Date is completelly undefined. */
	case 0:
		tnt_strptime_set_year(&t, DEFAULT_YEAR);
		tnt_strptime_set_month(&t, 0);
		tnt_strptime_set_mday(&t, 1);
		break;

	default:
		/* Invalid mix, eg. calendar and ordinal dates parts. */
		printf("%s: invalid case error\n", __func__);
		return NULL;
	}
#undef DEFAULT_YEAR

	/* Set year/mon/mday. Yday, wday isn't used by our clients. */
	/* TODO: fix comment in header. */
	tm->tm_year = t.base.tm_year;
	tm->tm_mon = t.base.tm_mon;
	tm->tm_mday = t.base.tm_mday;
	printf("%s: year=%d mon=%d mday=%d\n", __func__, tm->tm_year, tm->tm_mon, tm->tm_mday);
	assert(tm->tm_mon >= 0 && tm->tm_mon <= 11);
	assert(tm->tm_mday >= 1 && tm->tm_mday <= 31);

	/* Handle time. Zero defaults is ok. */
	tm->tm_hour = t.base.tm_hour;
	tm->tm_min = t.base.tm_min;
	tm->tm_sec = t.base.tm_sec;
	printf("%s: hour=%d min=%d sec=%d\n", __func__, tm->tm_hour, tm->tm_min, tm->tm_sec);
	assert(tm->tm_hour >= 0 && tm->tm_hour <= 23);
	assert(tm->tm_min >= 0 && tm->tm_min <= 59);
	assert(tm->tm_sec >= 0 && tm->tm_sec <= 60);

end:
	/* Handle nsec & timezone info. Zero defaults is ok. */
	tm->tm_nsec = t.base.tm_nsec;
	printf("%s: nsec=%d\n", __func__, tm->tm_nsec);
	assert(tm->tm_nsec >= 0 && tm->tm_nsec <= 1000000000);
	tm->tm_gmtoff = t.base.tm_gmtoff;
	assert(tm->tm_gmtoff >= -86399 && tm->tm_gmtoff <= 86399);
	tm->tm_tzindex = t.base.tm_tzindex;
	assert(tm->tm_tzindex >= 0);
	/* tm->isdst isn't used by our clients. */
	printf("%s: tzindex=%d gmtoff=%ld\n", __func__, tm->tm_tzindex, tm->tm_gmtoff);

	return (char *)t.end;
}
