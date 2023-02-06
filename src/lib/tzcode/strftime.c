/* Convert a broken-down timestamp to a string.  */

/* Copyright 1989 The Regents of the University of California.
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the University nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
   OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE.  */

/*
** Based on the UCB version with the copyright notice appearing above.
**
** This is ANSIish only when "multibyte character == plain character".
*/

#include "private.h"
#include "trivia/util.h"
#include "datetime.h"
#include "tzcode.h"
#include "timezone.h"
#include "timelocal.h"

#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>

static int pow10[] = { 1,      10,	100,	  1000,	     10000,
		       100000, 1000000, 10000000, 100000000, 1000000000 };

enum warn {
	IN_NONE,
	IN_SOME,
	IN_THIS,
	IN_ALL
};

static ssize_t
_add(char *buf, ssize_t size, const char *str);
static ssize_t
_conv(char *buf, ssize_t size, const char *format, int n);
static ssize_t
_yconv(char *buf, ssize_t size, int a, int b, bool convert_top, bool convert_yy);
static ssize_t
_fmt(char *buf, ssize_t size, const char *format, const struct tnt_tm *t,
     enum warn *warnp);

size_t
tnt_strftime(char *s, size_t maxsize, const char *format,
	     const struct tnt_tm *t)
{
	tzset();
	enum warn warn = IN_NONE;

	if (s != NULL && maxsize > 0)
		s[0] = '\0';
	ssize_t total = _fmt(s, maxsize, format, t, &warn);
	assert(total >= 0);
	assert(maxsize == 0 || s[MIN((size_t)total, maxsize - 1)] == '\0');

	return (size_t)total;
}

static ssize_t
_fmt(char *buf, ssize_t size, const char *format, const struct tnt_tm *t,
     enum warn *warnp)
{
	ssize_t total = 0;

	for (; *format; ++format) {
		if (*format == '%') {
			/* length of %f format modifiers, if any */
			int mod_len = 0;
			int width = 0;
		label:
			switch (*++format) {
			case '\0':
				break;
			case 'A':
				SNPRINT(total, _add, buf, size,
					(t->tm_wday < 0 ||
					t->tm_wday >= DAYSPERWEEK) ? "?"
					: Locale->weekday[t->tm_wday]);
				continue;
			case 'a':
				SNPRINT(total, _add, buf, size,
					(t->tm_wday < 0 ||
					t->tm_wday >= DAYSPERWEEK) ? "?"
					: Locale->wday[t->tm_wday]);
				continue;
			case 'B':
				SNPRINT(total, _add, buf, size,
					(t->tm_mon < 0 ||
					t->tm_mon >= MONTHSPERYEAR) ? "?"
					: Locale->month[t->tm_mon]);
				continue;
			case 'b':
			case 'h':
				SNPRINT(total, _add, buf, size,
					(t->tm_mon < 0 ||
					t->tm_mon >= MONTHSPERYEAR) ? "?"
					: Locale->mon[t->tm_mon]);
				continue;
			case 'C':
				/*
				 ** %C used to do a...
				 **     _fmt("%a %b %e %X %Y", t);
				 ** ...whereas now POSIX 1003.2 calls for
				 ** something completely different.
				 ** (ado, 1993-05-24)
				 */
				SNPRINT(total, _yconv, buf, size,
					t->tm_year, TM_YEAR_BASE, true, false);
				continue;
			case 'c': {
				enum warn warn2 = IN_SOME;

				SNPRINT(total, _fmt, buf, size,
					Locale->c_fmt, t, &warn2);
				if (warn2 == IN_ALL)
					warn2 = IN_THIS;
				if (warn2 > *warnp)
					*warnp = warn2;
			}
				continue;
			case 'D':
				SNPRINT(total, _fmt, buf, size,
					"%m/%d/%y", t, warnp);
				continue;
			case 'd':
				SNPRINT(total, _conv, buf, size,
					"%02d", t->tm_mday);
				continue;
			case 'E':
			case 'O':
				/*
				 ** Locale modifiers of C99 and later.
				 ** The sequences
				 **     %Ec %EC %Ex %EX %Ey %EY
				 **     %Od %oe %OH %OI %Om %OM
				 **     %OS %Ou %OU %OV %Ow %OW %Oy
				 ** are supposed to provide alternative
				 ** representations.
				 */
				goto label;
			case 'e':
				SNPRINT(total, _conv, buf, size,
					"%2d", t->tm_mday);
				continue;
			case 'F':
				SNPRINT(total, _fmt, buf, size,
					"%Y-%m-%d", t, warnp);
				continue;
			case 'H':
				SNPRINT(total, _conv, buf, size,
					"%02d", t->tm_hour);
				continue;
			case 'I':
				SNPRINT(total, _conv, buf, size,
					"%02d", (t->tm_hour % 12) ?
					(t->tm_hour % 12) : 12);
				continue;
			case 'j':
				SNPRINT(total, _conv, buf, size,
					"%03d", t->tm_yday + 1);
				continue;
			case 'k':
				/*
				 ** This used to be...
				 **     _conv(t->tm_hour % 12 ?
				 **             t->tm_hour % 12 : 12, 2, ' ');
				 ** ...and has been changed to the below to
				 ** match SunOS 4.1.1 and Arnold Robbins'
				 ** strftime version 3.0. That is, "%k" and
				 ** "%l" have been swapped.
				 ** (ado, 1993-05-24)
				 */
				SNPRINT(total, _conv, buf, size,
					"%2d", t->tm_hour);
				continue;
#ifdef KITCHEN_SINK
			case 'K':
				/*
				 ** After all this time, still unclaimed!
				 */
				SNPRINT(total, _add, buf, size, "kitchen sink");
				continue;
#endif /* defined KITCHEN_SINK */
			case 'l':
				/*
				 ** This used to be...
				 **     _conv(t->tm_hour, 2, ' ');
				 ** ...and has been changed to the below to
				 ** match SunOS 4.1.1 and Arnold Robbin's
				 ** strftime version 3.0. That is, "%k" and
				 ** "%l" have been swapped.
				 ** (ado, 1993-05-24)
				 */
				SNPRINT(total, _conv, buf, size,
					"%2d", (t->tm_hour % 12) ?
					(t->tm_hour % 12) : 12);
				continue;
			case 'M':
				SNPRINT(total, _conv, buf, size,
					"%02d", t->tm_min);
				continue;
			case 'm':
				SNPRINT(total, _conv, buf, size,
					"%02d", t->tm_mon + 1);
				continue;
			case 'n':
				SNPRINT(total, _add, buf, size, "\n");
				continue;
			case 'p':
				SNPRINT(total, _add, buf, size,
					(t->tm_hour >= (HOURSPERDAY / 2))
					? Locale->pm
					: Locale->am);
				continue;
			case 'R':
				SNPRINT(total, _fmt, buf, size,
					"%H:%M", t, warnp);
				continue;
			case 'r':
				SNPRINT(total, _fmt, buf, size,
					"%I:%M:%S %p", t, warnp);
				continue;
			case 'S':
				SNPRINT(total, _conv, buf, size,
					"%02d", t->tm_sec);
				continue;
			case 's':
				SNPRINT(total, snprintf, buf, size,
					"%" PRIdMAX, (intmax_t)t->tm_epoch);
				continue;
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
				do {
					mod_len++;
				} while (is_digit(*++format));

				/*
				 ** flag is not accepting width modifiers -
				 ** unwind processed text back
				 */
				if (*format != 'f') {
					SNPRINT(total, snprintf, buf, size,
						"%.*s", mod_len + 1,
						format - mod_len);
					continue;
				}
				/*
				 ** We cannot cast returned value of strtol
				 ** to int in place, as the returned value has
				 ** long type: so, when width is more then
				 ** INT_MAX but less then LONG_MAX,
				 ** no errno will be set, we won't distinguish
				 ** if overflow happens.
				 */
				errno = 0;
				long tmp_width = strtol(format - mod_len,
							(char **)NULL, 10);
				width = (int)tmp_width;
				if (tmp_width < INT_MIN || tmp_width > INT_MAX ||
				    errno != 0 || width < 0 || width > 9)
					width = 9;
				/* fallthru */
			case 'f': {
				int nsec = t->tm_nsec;
				assert(nsec < MAX_NANOS_PER_SEC);

				/*
				 ** no length modifier provided -
				 ** enable default mode, as in
				 ** datetime_to_string()
				 */
				if (width == 0) {
					if ((nsec % 1000000) == 0) {
						width = 3;
						nsec /= 1000000;
					} else if ((nsec % 1000) == 0) {
						width = 6;
						nsec /= 1000;
					} else {
						width = 9;
					}
				} else {
					nsec /= pow10[9 - width];
				}
				SNPRINT(total, snprintf, buf, size,
					"%0*d", width, nsec);
			}
				continue;

			case 'T':
				SNPRINT(total, _fmt, buf, size,
					"%H:%M:%S", t, warnp);
				continue;
			case 't':
				SNPRINT(total, _add, buf, size, "\t");
				continue;
			case 'U':
				SNPRINT(total, _conv, buf, size,
					"%02d", (t->tm_yday + DAYSPERWEEK -
					t->tm_wday) / DAYSPERWEEK);
				continue;
			case 'u':
				/*
				 ** From Arnold Robbins' strftime version 3.0:
				 ** "ISO 8601: Weekday as a decimal number
				 ** [1 (Monday) - 7]"
				 ** (ado, 1993-05-24)
				 */
				SNPRINT(total, _conv, buf, size,
					"%d", (t->tm_wday == 0) ?
					DAYSPERWEEK : t->tm_wday);
				continue;
			case 'V': /* ISO 8601 week number */
			case 'G': /* ISO 8601 year (four digits) */
			case 'g': /* ISO 8601 year (two digits) */
				/*
				 ** From Arnold Robbins' strftime version 3.0:
				 *"the week number of the
				 ** year (the first Monday as the first day of
				 *week 1) as a decimal number
				 ** (01-53)."
				 ** (ado, 1993-05-24)
				 **
				 ** From
				 *<https://www.cl.cam.ac.uk/~mgk25/iso-time.html>
				 *by Markus Kuhn:
				 ** "Week 01 of a year is per definition the
				 *first week which has the
				 ** Thursday in this year, which is equivalent
				 *to the week which contains
				 ** the fourth day of January. In other words,
				 *the first week of a new year
				 ** is the week which has the majority of its
				 *days in the new year. Week 01
				 ** might also contain days from the previous
				 *year and the week before week
				 ** 01 of a year is the last week (52 or 53) of
				 *the previous year even if
				 ** it contains days from the new year. A week
				 *starts with Monday (day 1)
				 ** and ends with Sunday (day 7). For example,
				 *the first week of the year
				 ** 1997 lasts from 1996-12-30 to 1997-01-05..."
				 ** (ado, 1996-01-02)
				 */
				{
					int year;
					int base;
					int yday;
					int wday;
					int w;

					year = t->tm_year;
					base = TM_YEAR_BASE;
					yday = t->tm_yday;
					wday = t->tm_wday;
					for (;;) {
						int len;
						int bot;
						int top;

						len = isleap_sum(year, base)
							? DAYSPERLYEAR
							: DAYSPERNYEAR;
						/*
						 ** What yday (-3 ... 3) does
						 ** the ISO year begin on?
						 */
						bot = ((yday + 11 - wday) %
						       DAYSPERWEEK) -
							3;
						/*
						 ** What yday does the NEXT
						 ** ISO year begin on?
						 */
						top = bot - (len % DAYSPERWEEK);
						if (top < -3)
							top += DAYSPERWEEK;
						top += len;
						if (yday >= top) {
							++base;
							w = 1;
							break;
						}
						if (yday >= bot) {
							w = 1 + ((yday - bot) /
								 DAYSPERWEEK);
							break;
						}
						--base;
						yday += isleap_sum(year, base)
							? DAYSPERLYEAR
							: DAYSPERNYEAR;
					}
#ifdef XPG4_1994_04_09
					if ((w == 52 &&
					     t->tm_mon == TM_JANUARY) ||
					    (w == 1 &&
					     t->tm_mon == TM_DECEMBER))
						w = 53;
#endif /* defined XPG4_1994_04_09 */
					if (*format == 'V')
						SNPRINT(total, _conv, buf, size,
							"%02d", w);
					else if (*format == 'g') {
						*warnp = IN_ALL;
						SNPRINT(total, _yconv,
							buf, size,
							year, base,
							false, true);
					} else
						SNPRINT(total, _yconv,
							buf, size,
							year, base,
							true, true);
				}
				continue;
			case 'v':
				/*
				 ** From Arnold Robbins' strftime version 3.0:
				 ** "date as dd-bbb-YYYY"
				 ** (ado, 1993-05-24)
				 */
				SNPRINT(total, _fmt, buf, size,
					"%e-%b-%Y", t, warnp);
				continue;
			case 'W':
				SNPRINT(total, _conv, buf, size,
					"%02d",
					(t->tm_yday + DAYSPERWEEK -
					(t->tm_wday ? (t->tm_wday - 1)
					: (DAYSPERWEEK - 1))) / DAYSPERWEEK);
				continue;
			case 'w':
				SNPRINT(total, _conv, buf, size,
					"%d", t->tm_wday);
				continue;
			case 'X':
				SNPRINT(total, _fmt, buf, size,
					Locale->X_fmt, t, warnp);
				continue;
			case 'x': {
				enum warn warn2 = IN_SOME;

				SNPRINT(total, _fmt, buf, size,
					Locale->x_fmt, t, &warn2);
				if (warn2 == IN_ALL)
					warn2 = IN_THIS;
				if (warn2 > *warnp)
					*warnp = warn2;
			}
				continue;
			case 'y':
				*warnp = IN_ALL;
				SNPRINT(total, _yconv, buf, size,
					t->tm_year, TM_YEAR_BASE, false, true);
				continue;
			case 'Y':
				SNPRINT(total, _yconv, buf, size,
					t->tm_year, TM_YEAR_BASE, true, true);
				continue;
			case 'Z':
				if (t->tm_tzindex == 0)
					continue;
				assert(timezone_name(t->tm_tzindex) != NULL);
				SNPRINT(total, snprintf, buf, size,
					"%s", timezone_name(t->tm_tzindex));
				continue;
			case 'z': {
				long diff;
				char const *sign;
				bool negative;

				diff = t->tm_gmtoff;
				negative = diff < 0;
				if (negative) {
					sign = "-";
					diff = -diff;
				} else
					sign = "+";
				SNPRINT(total, _add, buf, size, sign);
				diff /= SECSPERMIN;
				diff = (diff / MINSPERHOUR) * 100 +
					(diff % MINSPERHOUR);
				SNPRINT(total, _conv, buf, size,
					"%04d", diff);
			}
				continue;
			case '+':
				SNPRINT(total, _fmt, buf, size,
					Locale->date_fmt, t, warnp);
				continue;
			case '%':
				/*
				 ** X311J/88-090 (4.12.3.5): if conversion char
				 ** is undefined, behavior is undefined. Print
				 ** out the character itself as printf(3) also
				 ** does.
				 */
			default:
				break;
			}
		}
		SNPRINT(total, _conv, buf, size, "%c", *format);
	}
	return total;
}

static ssize_t
_conv(char *buf, ssize_t size, const char *format, int n)
{
	ssize_t total = 0;
	SNPRINT(total, snprintf, buf, size, format, n);
	return total;
}

static ssize_t
_add(char *buf, ssize_t size, const char *str)
{
	ssize_t total = 0;
	SNPRINT(total, snprintf, buf, size, "%s", str);
	return total;
}

/*
** POSIX and the C Standard are unclear or inconsistent about
** what %C and %y do if the year is negative or exceeds 9999.
** Use the convention that %C concatenated with %y yields the
** same output as %Y, and that %Y contains at least 4 bytes,
** with more only if necessary.
*/

static ssize_t
_yconv(char *buf, ssize_t size, int a, int b, bool convert_top, bool convert_yy)
{
	register int lead;
	register int trail;
	ssize_t total = 0;

#define DIVISOR 100
	trail = a % DIVISOR + b % DIVISOR;
	lead = a / DIVISOR + b / DIVISOR + trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (convert_top) {
		if (lead == 0 && trail < 0)
			SNPRINT(total, _add, buf, size,"-0");
		else
			SNPRINT(total, _conv, buf, size, "%02d", lead);
	}
	if (convert_yy)
		SNPRINT(total, _conv, buf, size, "%02d",
			((trail < 0) ? -trail : trail));
	return total;
}
