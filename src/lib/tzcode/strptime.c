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
	FLAG_NONE = 1 << 0,
	FLAG_YEAR = 1 << 1,
	FLAG_MONTH = 1 << 2,
	FLAG_YDAY = 1 << 3,
	FLAG_MDAY = 1 << 4,
	FLAG_WDAY = 1 << 5,
	FLAG_EPOCH = 1 << 6,
	FLAG_NSEC = 1 << 7,
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

char *
tnt_strptime(const char *__restrict buf, const char *__restrict fmt,
	     struct tnt_tm *__restrict tm)
{
	char c;
	int day_offset = -1, wday_offset;
	int week_offset;
	int i, len;
	int Ealternative, Oalternative;
	enum flags flags = FLAG_NONE;
	int century = -1;
	int year = -1;
	const char *ptr = fmt;

	static int start_of_month[2][13] = {
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
		{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
	};

	while (*ptr != 0) {
		c = *ptr++;

		if (c != '%') {
			/* Eat up white-space in buffer and in format. */
			if (isspace((u_char)c)) {
				while (*buf != 0 && isspace((u_char)*buf))
					buf++;
			}
			else if (c != *buf++)
				return NULL;
			continue;
		}

		Ealternative = 0;
		Oalternative = 0;
	label:
		c = *ptr++;
		switch (c) {
		case '%':
			if (*buf++ != '%')
				return NULL;
			break;

		case '+':
			buf = tnt_strptime(buf, Locale->date_fmt, tm);
			if (buf == NULL)
				return NULL;
			flags |= FLAG_WDAY | FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'C':
			if (!is_digit((u_char)*buf))
				return NULL;

			/* XXX This will break for 3-digit centuries. */
			i = NUM2(buf);

			century = i;
			flags |= FLAG_YEAR;

			break;

		case 'c':
			buf = tnt_strptime(buf, Locale->c_fmt, tm);
			if (buf == NULL)
				return NULL;
			flags |= FLAG_WDAY | FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'D':
			buf = tnt_strptime(buf, "%m/%d/%y", tm);
			if (buf == NULL)
				return NULL;
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

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
			buf = tnt_strptime(buf, "%e-%b-%Y", tm);
			if (buf == NULL)
				return NULL;
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'F':
			buf = tnt_strptime(buf, "%Y-%m-%d", tm);
			if (buf == NULL)
				return NULL;
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'R':
			buf = tnt_strptime(buf, "%H:%M", tm);
			if (buf == NULL)
				return NULL;
			break;

		case 'r':
			buf = tnt_strptime(buf, Locale->ampm_fmt, tm);
			if (buf == NULL)
				return NULL;
			break;

		case 'T':
			buf = tnt_strptime(buf, "%H:%M:%S", tm);
			if (buf == NULL)
				return NULL;
			break;

		case 'X':
			buf = tnt_strptime(buf, Locale->X_fmt, tm);
			if (buf == NULL)
				return NULL;
			break;

		case 'x':
			buf = tnt_strptime(buf, Locale->x_fmt, tm);
			if (buf == NULL)
				return NULL;
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'j':
			if (!is_digit((u_char)*buf))
				return NULL;

			i = NUM3(buf);
			if (i < 1 || i > 366)
				return NULL;

			tm->tm_yday = i - 1;
			flags |= FLAG_YDAY;

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
			assert(c == 'f');
			/* fallthru */
		case 'f':
			if (!is_digit((u_char)*buf))
				return NULL;

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
			tm->tm_nsec = i;
			flags |= FLAG_NSEC;

			break;

		case 'M':
		case 'S':
			if (*buf == 0 || isspace((u_char)*buf))
				break;

			if (!is_digit((u_char)*buf))
				return NULL;

			i = NUM2(buf);

			if (c == 'M') {
				if (i > 59)
					return NULL;
				tm->tm_min = i;
			} else {
				if (i > 60)
					return NULL;
				tm->tm_sec = i;
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
				return NULL;

			i = NUM_(len, buf);
			if (c == 'H' || c == 'k') {
				if (i > 23)
					return NULL;
			} else if (i == 0 || i > 12)
				return NULL;

			tm->tm_hour = i;

			break;

		case 'p':
			/*
			 * XXX This is bogus if parsed before hour-related
			 * specifiers.
			 */
			if (tm->tm_hour > 12)
				return NULL;

			len = strlen(Locale->am);
			if (strncasecmp(buf, Locale->am, len) == 0) {
				if (tm->tm_hour == 12)
					tm->tm_hour = 0;
				buf += len;
				break;
			}

			len = strlen(Locale->pm);
			if (strncasecmp(buf, Locale->pm, len) == 0) {
				if (tm->tm_hour != 12)
					tm->tm_hour += 12;
				buf += len;
				break;
			}

			return NULL;

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
				return NULL;

			buf += len;
			tm->tm_wday = i;
			flags |= FLAG_WDAY;
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
				return NULL;

			i = NUM2(buf);
			if (i > 53)
				return NULL;

			if (c == 'U')
				day_offset = TM_SUNDAY;
			else
				day_offset = TM_MONDAY;

			week_offset = i;

			break;

		case 'u':
		case 'w':
			if (!is_digit((u_char)*buf))
				return NULL;

			i = *buf++ - '0';
			if (i < 0 || i > 7 || (c == 'u' && i < 1) ||
			    (c == 'w' && i > 6))
				return NULL;

			tm->tm_wday = i % 7;
			flags |= FLAG_WDAY;

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
				return NULL;

			i = NUM2(buf);
			if (i == 0 || i > 31)
				return NULL;

			tm->tm_mday = i;
			flags |= FLAG_MDAY;

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
				return NULL;

			tm->tm_mon = i;
			buf += len;
			flags |= FLAG_MONTH;

			break;

		case 'm':
			if (!is_digit((u_char)*buf))
				return NULL;

			i = NUM2(buf);
			if (i < 1 || i > 12)
				return NULL;

			tm->tm_mon = i - 1;
			flags |= FLAG_MONTH;

			break;

		case 's': {
			char *cp;
			long n;

			n = strtol(buf, &cp, 10);
			if (n == 0) {
				return NULL;
			}
			buf = cp;
			tm->tm_epoch = n;
			flags |= FLAG_EPOCH;
		} break;

		case 'G': /* ISO 8601 year (four digits) */
		case 'g': /* ISO 8601 year (two digits) */
		case 'Y':
		case 'y':
			if (*buf == 0 || isspace((u_char)*buf))
				break;

			if (*buf != '-' && !is_digit((u_char)*buf))
				return NULL;

			len = (c == 'Y' || c == 'G') ? 7 : 2;
			i = NUM_(len, buf);
			if (c == 'Y' || c == 'G')
				century = i / 100;
			year = i % 100;

			flags |= FLAG_YEAR;

			break;

		case 'Z': {
			const char *cp;
			char *zonestr;

			for (cp = buf; *cp && isupper((u_char)*cp); ++cp)
				/* empty */;
			if (cp - buf) {
				zonestr = alloca(cp - buf + 1);
				strlcpy(zonestr, buf, cp - buf + 1);

				const struct date_time_zone *zone;
				size_t n = timezone_tm_lookup(zonestr, cp - buf,
							      &zone, tm);
				if (n <= 0)
					return NULL;

				buf += cp - buf;
			}
		} break;

		case 'z': {

			/* Even for %z format specifier we better to accept
			 * Zulu timezone as default Tarantool shortcut for
			 * +00:00 offset.
			 */
			if (*buf == 'Z') {
				buf++;
				tm->tm_gmtoff = 0;
				break;
			}
			int sign = 1;

			if (*buf != '+') {
				if (*buf == '-')
					sign = -1;
				else
					return NULL;
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
					return NULL;
			}

			if (i > 1400 || (sign == -1 && i > 1200) ||
			    (i % 100) >= 60)
				return NULL;
			tm->tm_gmtoff =
				sign * ((i / 100) * 3600 + i % 100 * 60);
		} break;

		case 'n':
		case 't':
			while (isspace((u_char)*buf))
				buf++;
			break;

		default:
			return NULL;
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
		tm->tm_year = year;
	}

	if (!(flags & FLAG_YDAY) && (flags & FLAG_YEAR)) {
		if ((flags & (FLAG_MONTH | FLAG_MDAY)) ==
		    (FLAG_MONTH | FLAG_MDAY)) {
			tm->tm_yday = start_of_month[isleap(tm->tm_year +
							    TM_YEAR_BASE)]
						    [tm->tm_mon] +
				(tm->tm_mday - 1);
			flags |= FLAG_YDAY;
		} else if (day_offset != -1) {
			int tmpwday, tmpyday, fwo;

			fwo = first_wday_of(tm->tm_year + TM_YEAR_BASE);
			/* No incomplete week (week 0). */
			if (week_offset == 0 && fwo == day_offset)
				return NULL;

			/* Set the date to the first Sunday (or Monday)
			 * of the specified week of the year.
			 */
			tmpwday =
				(flags & FLAG_WDAY) ? tm->tm_wday : day_offset;
			tmpyday = (7 - fwo + day_offset) % 7 +
				(week_offset - 1) * 7 +
				(tmpwday - day_offset + 7) % 7;
			/* Impossible yday for incomplete week (week 0). */
			if (tmpyday < 0) {
				if (flags & FLAG_WDAY)
					return NULL;
				tmpyday = 0;
			}
			tm->tm_yday = tmpyday;
			flags |= FLAG_YDAY;
		}
	}

	if ((flags & (FLAG_YEAR | FLAG_YDAY)) == (FLAG_YEAR | FLAG_YDAY)) {
		if (!(flags & FLAG_MONTH)) {
			i = 0;
			while (i <= 12 &&
			       tm->tm_yday >=
				       start_of_month[isleap(tm->tm_year +
							     TM_YEAR_BASE)][i])
				i++;
			if (i > 12) {
				i = 1;
				tm->tm_yday -= start_of_month[isleap(
					tm->tm_year + TM_YEAR_BASE)][12];
				tm->tm_year++;
			}
			tm->tm_mon = i - 1;
			flags |= FLAG_MONTH;
		}
		if (!(flags & FLAG_MDAY)) {
			tm->tm_mday = tm->tm_yday -
				start_of_month[isleap(tm->tm_year +
						      TM_YEAR_BASE)]
					      [tm->tm_mon] +
				1;
			flags |= FLAG_MDAY;
		}
		if (!(flags & FLAG_WDAY)) {
			i = 0;
			wday_offset = first_wday_of(tm->tm_year);
			while (i++ <= tm->tm_yday) {
				if (wday_offset++ >= 6)
					wday_offset = 0;
			}
			tm->tm_wday = wday_offset;
			flags |= FLAG_WDAY;
		}
	}

	return (char *)buf;
}
