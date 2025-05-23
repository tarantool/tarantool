/* Convert timestamp from time_t to struct tnt_tm.  */

/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
*/

/*
** Leap second handling from Bradley White.
** POSIX-style TZ environment variable handling from Guy Harris.
*/

/*LINTLIBRARY*/

#include <unistd.h>
#define LOCALTIME_IMPLEMENTATION
#include "private.h"
#include "tzfile.h"
#include "tzcode.h"
#include "trivia/util.h"

#include <fcntl.h>

#ifndef TZ_ABBR_MAX_LEN
#define TZ_ABBR_MAX_LEN 16
#endif /* !defined TZ_ABBR_MAX_LEN */

#ifndef TZ_ABBR_CHAR_SET
#define TZ_ABBR_CHAR_SET                                                       \
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :+-._"
#endif /* !defined TZ_ABBR_CHAR_SET */

#ifndef TZ_ABBR_ERR_CHAR
#define TZ_ABBR_ERR_CHAR '_'
#endif /* !defined TZ_ABBR_ERR_CHAR */

/*
** SunOS 4.1.1 headers lack O_BINARY.
*/

#ifdef O_BINARY
#define OPEN_MODE (O_RDONLY | O_BINARY)
#endif /* defined O_BINARY */
#ifndef O_BINARY
#define OPEN_MODE O_RDONLY
#endif /* !defined O_BINARY */

#ifndef WILDABBR
/*
** Someone might make incorrect use of a time zone abbreviation:
**	1.	They might reference tzname[0] before calling tzset (explicitly
**		or implicitly).
**	2.	They might reference tzname[1] before calling tzset (explicitly
**		or implicitly).
**	3.	They might reference tzname[1] after setting to a time zone
**		in which Daylight Saving Time is never observed.
**	4.	They might reference tzname[0] after setting to a time zone
**		in which Standard Time is never observed.
**	5.	They might reference tm.TM_ZONE after calling offtime.
** What's best to do in the above cases is open to debate;
** for now, we just set things up so that in any of the five cases
** WILDABBR is used. Another possibility: initialize tzname[0] to the
** string "tzname[0] used before set", and similarly for the other cases.
** And another: initialize tzname[0] to "ERA", with an explanation in the
** manual page of what this "time zone abbreviation" means (doing this so
** that tzname[0] has the "normal" length of three characters).
*/
#define WILDABBR "   "
#endif /* !defined WILDABBR */

static const char gmt[] = "GMT";

#if defined(HAVE_TZNAME) || defined(TM_ZONE)
static const char wildabbr[] = WILDABBR;
#endif

/*
** The DST rules to use if TZ has no rules and we can't load TZDEFRULES.
** Default to US rules as of 2017-05-07.
** POSIX does not specify the default DST rules;
** for historical reasons, US rules are a common default.
*/
#ifndef TZDEFRULESTRING
#define TZDEFRULESTRING ",M3.2.0,M11.1.0"
#endif

struct ttinfo { /* time type information */
	int_fast32_t tt_utoff; /* UT offset in seconds */
	bool tt_isdst; /* used to set tm_isdst */
	int tt_desigidx; /* abbreviation list index */
	bool tt_ttisstd; /* transition is std time */
	bool tt_ttisut; /* transition is UT */
};

struct lsinfo { /* leap second information */
	time_t ls_trans; /* transition time */
	int_fast32_t ls_corr; /* correction to apply */
};

#define SMALLEST(a, b) (((a) < (b)) ? (a) : (b))
#define BIGGEST(a, b) (((a) > (b)) ? (a) : (b))

/* This abbreviation means local time is unspecified.  */
static char const UNSPEC[] = "-00";

/* How many extra bytes are needed at the end of struct state's chars array.
   This needs to be at least 1 for null termination in case the input
   data isn't properly terminated, and it also needs to be big enough
   for ttunspecified to work without crashing.  */
enum { CHARS_EXTRA = BIGGEST(sizeof UNSPEC, 2) - 1 };

#ifdef TZNAME_MAX
#define MY_TZNAME_MAX TZNAME_MAX
#endif /* defined TZNAME_MAX */
#ifndef TZNAME_MAX
#define MY_TZNAME_MAX 255
#endif /* !defined TZNAME_MAX */

struct state {
	int leapcnt;
	int timecnt;
	int typecnt;
	int charcnt;
	bool goback;
	bool goahead;
	time_t ats[TZ_MAX_TIMES];
	unsigned char types[TZ_MAX_TIMES];
	struct ttinfo ttis[TZ_MAX_TYPES];
	char chars[BIGGEST(BIGGEST(TZ_MAX_CHARS + CHARS_EXTRA, sizeof gmt),
			   (2 * (MY_TZNAME_MAX + 1)))];
	struct lsinfo lsis[TZ_MAX_LEAPS];

	/* The time type to use for early times or if no transitions.
	   It is always zero for recent tzdb releases.
	   It might be nonzero for data from tzdb 2018e or earlier.  */
	int defaulttype;
};

enum r_type {
	JULIAN_DAY, /* Jn = Julian day */
	DAY_OF_YEAR, /* n = day of year */
	MONTH_NTH_DAY_OF_WEEK /* Mm.n.d = month, week, day of week */
};

struct rule {
	enum r_type r_type; /* type of rule */
	int r_day; /* day number of rule */
	int r_week; /* week number of rule */
	int r_mon; /* month number of rule */
	int_fast32_t r_time; /* transition time of rule */
};

static struct tnt_tm *
gmtsub(struct state const *, time_t const *, int_fast32_t, struct tnt_tm *);
static bool
increment_overflow(int *, int);
static bool
increment_overflow_time(time_t *, int_fast32_t);
static int_fast32_t
leapcorr(struct state const *, time_t);
static struct tnt_tm *
timesub(time_t const *, int_fast32_t, struct state const *, struct tnt_tm *);
static bool
typesequiv(struct state const *, int, int);
static bool
tzparse(char const *, struct state *, struct state *);

#ifdef ALL_STATE
static struct state *lclptr;
static struct state *gmtptr;
#endif /* defined ALL_STATE */

#ifndef ALL_STATE
static struct state gmtmem;
#define gmtptr (&gmtmem)
#endif /* State Farm */

#if 2 <= HAVE_TZNAME + TZ_TIME_T
char *tzname[2] = { (char *)wildabbr, (char *)wildabbr };
#endif
#if 2 <= USG_COMPAT + TZ_TIME_T
long timezone;
int daylight;
#endif
#if 2 <= ALTZONE + TZ_TIME_T
long altzone;
#endif

/* Initialize *S to a value based on UTOFF, ISDST, and DESIGIDX.  */
static void
init_ttinfo(struct ttinfo *s, int_fast32_t utoff, bool isdst, int desigidx)
{
	s->tt_utoff = utoff;
	s->tt_isdst = isdst;
	s->tt_desigidx = desigidx;
	s->tt_ttisstd = false;
	s->tt_ttisut = false;
}

/* Return true if SP's time type I does not specify local time.  */
static bool
ttunspecified(struct state const *sp, int i)
{
	char const *abbr = &sp->chars[sp->ttis[i].tt_desigidx];
	/* memcmp is likely faster than strcmp, and is safe due to CHARS_EXTRA.
	 */
	return memcmp(abbr, UNSPEC, sizeof UNSPEC) == 0;
}

static int_fast32_t
detzcode(const char *const codep)
{
	int_fast32_t result;
	int i;
	int_fast32_t one = 1;
	int_fast32_t halfmaxval = one << (32 - 2);
	int_fast32_t maxval = halfmaxval - 1 + halfmaxval;
	int_fast32_t minval = -1 - maxval;

	result = codep[0] & 0x7f;
	for (i = 1; i < 4; ++i)
		result = (result << 8) | (codep[i] & 0xff);

	if (codep[0] & 0x80) {
		/* Do two's-complement negation even on non-two's-complement
		   machines. If the result would be minval - 1, return minval.
		 */
		result -= !TWOS_COMPLEMENT(int_fast32_t) && result != 0;
		result += minval;
	}
	return result;
}

static int_fast64_t
detzcode64(const char *const codep)
{
	int_fast64_t result;
	int i;
	int_fast64_t one = 1;
	int_fast64_t halfmaxval = one << (64 - 2);
	int_fast64_t maxval = halfmaxval - 1 + halfmaxval;
	int_fast64_t minval = -TWOS_COMPLEMENT(int_fast64_t) - maxval;

	result = codep[0] & 0x7f;
	for (i = 1; i < 8; ++i)
		result = (result << 8) | (codep[i] & 0xff);

	if (codep[0] & 0x80) {
		/* Do two's-complement negation even on non-two's-complement
		   machines. If the result would be minval - 1, return minval.
		 */
		result -= !TWOS_COMPLEMENT(int_fast64_t) && result != 0;
		result += minval;
	}
	return result;
}

static void
update_tzname_etc(struct state const *sp, struct ttinfo const *ttisp)
{
#if HAVE_TZNAME
	tzname[ttisp->tt_isdst] = (char *)&sp->chars[ttisp->tt_desigidx];
#endif
#if USG_COMPAT
	if (!ttisp->tt_isdst)
		timezone = -ttisp->tt_utoff;
#endif
#if ALTZONE
	if (ttisp->tt_isdst)
		altzone = -ttisp->tt_utoff;
#endif
	(void)sp;
	(void)ttisp;
}

static void
scrub_abbrs(struct state *sp)
{
	int i;
	/*
	** First, replace bogus characters.
	*/
	for (i = 0; i < sp->charcnt; ++i)
		if (strchr(TZ_ABBR_CHAR_SET, sp->chars[i]) == NULL)
			sp->chars[i] = TZ_ABBR_ERR_CHAR;
	/*
	** Second, truncate long abbreviations.
	*/
	for (i = 0; i < sp->typecnt; ++i) {
		const struct ttinfo *const ttisp = &sp->ttis[i];
		char *cp = &sp->chars[ttisp->tt_desigidx];

		if (strlen(cp) > TZ_ABBR_MAX_LEN &&
		    strcmp(cp, GRANDPARENTED) != 0)
			*(cp + TZ_ABBR_MAX_LEN) = '\0';
	}
}

/* Input buffer for data read from a compiled tz file.  */
union input_buffer {
	/* The first part of the buffer, interpreted as a header.  */
	struct tzhead tzhead;

	/* The entire buffer.  */
	char buf[2 * sizeof(struct tzhead) + 2 * sizeof(struct state) +
		 4 * TZ_MAX_TIMES];
};

/* TZDIR with a trailing '/' rather than a trailing '\0'.  */
#if __has_attribute(nonstring)
__attribute__((nonstring))
#endif
static char const tzdirslash[sizeof TZDIR] = TZDIR "/";

/* Local storage needed for 'tzloadbody'.  */
union local_storage {
	/* The results of analyzing the file's contents after it is opened.  */
	struct file_analysis {
		/* The input buffer.  */
		union input_buffer u;

		/* A temporary state used for parsing a TZ string in the file.
		 */
		struct state st;
	} u;

	/* The file name to be opened.  */
	char fullname[BIGGEST(sizeof(struct file_analysis),
			      sizeof tzdirslash + 1024)];
};

/* Load tz data from the file named NAME into *SP.  Read extended
   format if DOEXTEND.  Use *LSP for temporary storage.  Return 0 on
   success, an errno value on failure.  */
static int
tzloadbody(char const *name, struct state *sp, bool doextend,
	   union local_storage *lsp)
{
	int i;
	int fid;
	int stored;
	ssize_t nread;
	bool doaccess;
	union input_buffer *up = &lsp->u.u;
	int tzheadsize = sizeof(struct tzhead);

	sp->goback = sp->goahead = false;

	if (!name) {
		name = TZDEFAULT;
		if (!name)
			return EINVAL;
	}

	if (name[0] == ':')
		++name;
#ifdef SUPPRESS_TZDIR
	/* Do not prepend TZDIR.  This is intended for specialized
	   applications only, due to its security implications.  */
	doaccess = true;
#else
	doaccess = name[0] == '/';
#endif
	if (!doaccess) {
		char const *dot;
		size_t namelen = strlen(name);
		if (sizeof lsp->fullname - sizeof tzdirslash <= namelen)
			return ENAMETOOLONG;

		/* Create a string "TZDIR/NAME".  Using sprintf here
		   would pull in stdio (and would fail if the
		   resulting string length exceeded INT_MAX!).  */
		memcpy(lsp->fullname, tzdirslash, sizeof tzdirslash);
		strlcpy(lsp->fullname + sizeof tzdirslash, name,
			sizeof lsp->fullname - sizeof tzdirslash);

		/* Set doaccess if NAME contains a ".." file name
		   component, as such a name could read a file outside
		   the TZDIR virtual subtree.  */
		for (dot = name; (dot = strchr(dot, '.')); dot++)
			if ((dot == name || dot[-1] == '/') && dot[1] == '.' &&
			    (dot[2] == '/' || !dot[2])) {
				doaccess = true;
				break;
			}

		name = lsp->fullname;
	}
	if (doaccess && access(name, R_OK) != 0)
		return errno;
	fid = open(name, OPEN_MODE);
	if (fid < 0)
		return errno;

	nread = read(fid, up->buf, sizeof up->buf);
	if (nread < tzheadsize) {
		int err = nread < 0 ? errno : EINVAL;
		close(fid);
		return err;
	}
	if (close(fid) < 0)
		return errno;
	for (stored = 4; stored <= 8; stored *= 2) {
		char version = up->tzhead.tzh_version[0];
		bool skip_datablock = stored == 4 && version;
		int_fast32_t datablock_size;
		int_fast32_t ttisstdcnt = detzcode(up->tzhead.tzh_ttisstdcnt);
		int_fast32_t ttisutcnt = detzcode(up->tzhead.tzh_ttisutcnt);
		int_fast64_t prevtr = -1;
		int_fast32_t prevcorr = 0;
		int_fast32_t leapcnt = detzcode(up->tzhead.tzh_leapcnt);
		int_fast32_t timecnt = detzcode(up->tzhead.tzh_timecnt);
		int_fast32_t typecnt = detzcode(up->tzhead.tzh_typecnt);
		int_fast32_t charcnt = detzcode(up->tzhead.tzh_charcnt);
		char const *p = up->buf + tzheadsize;
		/* Although tzfile(5) currently requires typecnt to be nonzero,
		   support future formats that may allow zero typecnt
		   in files that have a TZ string and no transitions.  */
		if (!(0 <= leapcnt && leapcnt < TZ_MAX_LEAPS && 0 <= typecnt &&
		      typecnt < TZ_MAX_TYPES && 0 <= timecnt &&
		      timecnt < TZ_MAX_TIMES && 0 <= charcnt &&
		      charcnt < TZ_MAX_CHARS && 0 <= ttisstdcnt &&
		      ttisstdcnt < TZ_MAX_TYPES && 0 <= ttisutcnt &&
		      ttisutcnt < TZ_MAX_TYPES))
			return EINVAL;
		datablock_size = (timecnt * stored /* ats */
				  + timecnt /* types */
				  + typecnt * 6 /* ttinfos */
				  + charcnt /* chars */
				  + leapcnt * (stored + 4) /* lsinfos */
				  + ttisstdcnt /* ttisstds */
				  + ttisutcnt); /* ttisuts */
		if (nread < tzheadsize + datablock_size)
			return EINVAL;
		if (skip_datablock)
			p += datablock_size;
		else {
			if (!((ttisstdcnt == typecnt || ttisstdcnt == 0) &&
			      (ttisutcnt == typecnt || ttisutcnt == 0)))
				return EINVAL;

			sp->leapcnt = leapcnt;
			sp->timecnt = timecnt;
			sp->typecnt = typecnt;
			sp->charcnt = charcnt;

			/* Read transitions, discarding those out of time_t
			   range. But pretend the last transition before
			   TIME_T_MIN occurred at TIME_T_MIN.  */
			timecnt = 0;
			for (i = 0; i < sp->timecnt; ++i) {
				int_fast64_t at = stored == 4 ? detzcode(p)
							      : detzcode64(p);
				sp->types[i] = at <= TIME_T_MAX;
				if (sp->types[i]) {
					time_t attime =
						((TYPE_SIGNED(time_t)
							  ? at < TIME_T_MIN
							  : at < 0)
							 ? TIME_T_MIN
							 : at);
					if (timecnt &&
					    attime <= sp->ats[timecnt - 1]) {
						if (attime <
						    sp->ats[timecnt - 1])
							return EINVAL;
						sp->types[i - 1] = 0;
						timecnt--;
					}
					sp->ats[timecnt++] = attime;
				}
				p += stored;
			}

			timecnt = 0;
			for (i = 0; i < sp->timecnt; ++i) {
				unsigned char typ = *p++;
				if (sp->typecnt <= typ)
					return EINVAL;
				if (sp->types[i])
					sp->types[timecnt++] = typ;
			}
			sp->timecnt = timecnt;
			for (i = 0; i < sp->typecnt; ++i) {
				struct ttinfo *ttisp;
				unsigned char isdst, desigidx;

				ttisp = &sp->ttis[i];
				ttisp->tt_utoff = detzcode(p);
				p += 4;
				isdst = *p++;
				if (!(isdst < 2))
					return EINVAL;
				ttisp->tt_isdst = isdst;
				desigidx = *p++;
				if (!(desigidx < sp->charcnt))
					return EINVAL;
				ttisp->tt_desigidx = desigidx;
			}
			for (i = 0; i < sp->charcnt; ++i)
				sp->chars[i] = *p++;
			/* Ensure '\0'-terminated, and make it safe to call
			   ttunspecified later.  */
			memset(&sp->chars[i], 0, CHARS_EXTRA);

			/* Read leap seconds, discarding those out of time_t
			 * range.  */
			leapcnt = 0;
			for (i = 0; i < sp->leapcnt; ++i) {
				int_fast64_t tr = stored == 4 ? detzcode(p)
							      : detzcode64(p);
				int_fast32_t corr = detzcode(p + stored);
				p += stored + 4;

				/* Leap seconds cannot occur before the Epoch,
				   or out of order.  */
				if (tr <= prevtr)
					return EINVAL;

				/* To avoid other botches in this code, each
				   leap second's correction must differ from the
				   previous one's by 1 second or less, except
				   that the first correction can be any value;
				   these requirements are more generous than RFC
				   8536, to allow future RFC extensions.  */
				if (!(i == 0 ||
				      (prevcorr < corr
					       ? corr == prevcorr + 1
					       : (corr == prevcorr ||
						  corr == prevcorr - 1))))
					return EINVAL;
				prevtr = tr;
				prevcorr = corr;

				if (tr <= TIME_T_MAX) {
					sp->lsis[leapcnt].ls_trans = tr;
					sp->lsis[leapcnt].ls_corr = corr;
					leapcnt++;
				}
			}
			sp->leapcnt = leapcnt;

			for (i = 0; i < sp->typecnt; ++i) {
				struct ttinfo *ttisp;

				ttisp = &sp->ttis[i];
				if (ttisstdcnt == 0)
					ttisp->tt_ttisstd = false;
				else {
					if (*p != true && *p != false)
						return EINVAL;
					ttisp->tt_ttisstd = *p++;
				}
			}
			for (i = 0; i < sp->typecnt; ++i) {
				struct ttinfo *ttisp;

				ttisp = &sp->ttis[i];
				if (ttisutcnt == 0)
					ttisp->tt_ttisut = false;
				else {
					if (*p != true && *p != false)
						return EINVAL;
					ttisp->tt_ttisut = *p++;
				}
			}
		}

		nread -= p - up->buf;
		memmove(up->buf, p, nread);

		/* If this is an old file, we're done.  */
		if (!version)
			break;
	}
	if (doextend && nread > 2 && up->buf[0] == '\n' &&
	    up->buf[nread - 1] == '\n' && sp->typecnt + 2 <= TZ_MAX_TYPES) {
		struct state *ts = &lsp->u.st;

		up->buf[nread - 1] = '\0';
		if (tzparse(&up->buf[1], ts, sp)) {

			/* Attempt to reuse existing abbreviations.
			   Without this, America/Anchorage would be right on
			   the edge after 2037 when TZ_MAX_CHARS is 50, as
			   sp->charcnt equals 40 (for LMT AST AWT APT AHST
			   AHDT YST AKDT AKST) and ts->charcnt equals 10
			   (for AKST AKDT).  Reusing means sp->charcnt can
			   stay 40 in this example.  */
			int gotabbr = 0;
			int charcnt = sp->charcnt;
			for (i = 0; i < ts->typecnt; i++) {
				char *tsabbr =
					ts->chars + ts->ttis[i].tt_desigidx;
				int j;
				for (j = 0; j < charcnt; j++)
					if (strcmp(sp->chars + j, tsabbr) ==
					    0) {
						ts->ttis[i].tt_desigidx = j;
						gotabbr++;
						break;
					}
				if (!(j < charcnt)) {
					int tsabbrlen = strlen(tsabbr);
					if (j + tsabbrlen < TZ_MAX_CHARS) {
						strlcpy(sp->chars + j, tsabbr,
							sizeof sp->chars - j);
						charcnt = j + tsabbrlen + 1;
						ts->ttis[i].tt_desigidx = j;
						gotabbr++;
					}
				}
			}
			if (gotabbr == ts->typecnt) {
				sp->charcnt = charcnt;

				/* Ignore any trailing, no-op transitions
				   generated by zic as they don't help here and
				   can run afoul of bugs in zic 2016j or
				   earlier.  */
				while (1 < sp->timecnt &&
				       (sp->types[sp->timecnt - 1] ==
					sp->types[sp->timecnt - 2]))
					sp->timecnt--;

				for (i = 0; i < ts->timecnt &&
				     sp->timecnt < TZ_MAX_TIMES;
				     i++) {
					time_t t = ts->ats[i];
					if (increment_overflow_time(
						    &t, leapcorr(sp, t)) ||
					    (0 < sp->timecnt &&
					     t <= sp->ats[sp->timecnt - 1]))
						continue;
					sp->ats[sp->timecnt] = t;
					sp->types[sp->timecnt] =
						(sp->typecnt + ts->types[i]);
					sp->timecnt++;
				}
				for (i = 0; i < ts->typecnt; i++)
					sp->ttis[sp->typecnt++] = ts->ttis[i];
			}
		}
	}
	if (sp->typecnt == 0)
		return EINVAL;
	if (sp->timecnt > 1) {
		if (sp->ats[0] <= TIME_T_MAX - SECSPERREPEAT) {
			time_t repeatat = sp->ats[0] + SECSPERREPEAT;
			int repeattype = sp->types[0];
			for (i = 1; i < sp->timecnt; ++i)
				if (sp->ats[i] == repeatat &&
				    typesequiv(sp, sp->types[i], repeattype)) {
					sp->goback = true;
					break;
				}
		}
		if (TIME_T_MIN + SECSPERREPEAT <= sp->ats[sp->timecnt - 1]) {
			time_t repeatat =
				sp->ats[sp->timecnt - 1] - SECSPERREPEAT;
			int repeattype = sp->types[sp->timecnt - 1];
			for (i = sp->timecnt - 2; i >= 0; --i)
				if (sp->ats[i] == repeatat &&
				    typesequiv(sp, sp->types[i], repeattype)) {
					sp->goahead = true;
					break;
				}
		}
	}

	/* Infer sp->defaulttype from the data.  Although this default
	   type is always zero for data from recent tzdb releases,
	   things are trickier for data from tzdb 2018e or earlier.

	   The first set of heuristics work around bugs in 32-bit data
	   generated by tzdb 2013c or earlier.  The workaround is for
	   zones like Australia/Macquarie where timestamps before the
	   first transition have a time type that is not the earliest
	   standard-time type.  See:
	   https://mm.icann.org/pipermail/tz/2013-May/019368.html */
	/*
	** If type 0 does not specify local time, or is unused in transitions,
	** it's the type to use for early times.
	*/
	for (i = 0; i < sp->timecnt; ++i)
		if (sp->types[i] == 0)
			break;
	i = i < sp->timecnt && !ttunspecified(sp, 0) ? -1 : 0;
	/*
	** Absent the above,
	** if there are transition times
	** and the first transition is to a daylight time
	** find the standard type less than and closest to
	** the type of the first transition.
	*/
	if (i < 0 && sp->timecnt > 0 && sp->ttis[sp->types[0]].tt_isdst) {
		i = sp->types[0];
		while (--i >= 0)
			if (!sp->ttis[i].tt_isdst)
				break;
	}
	/* The next heuristics are for data generated by tzdb 2018e or
	   earlier, for zones like EST5EDT where the first transition
	   is to DST.  */
	/*
	** If no result yet, find the first standard type.
	** If there is none, punt to type zero.
	*/
	if (i < 0) {
		i = 0;
		while (sp->ttis[i].tt_isdst)
			if (++i >= sp->typecnt) {
				i = 0;
				break;
			}
	}
	/* A simple 'sp->defaulttype = 0;' would suffice here if we
	   didn't have to worry about 2018e-or-earlier data.  Even
	   simpler would be to remove the defaulttype member and just
	   use 0 in its place.  */
	sp->defaulttype = i;

	return 0;
}

/* Load tz data from the file named NAME into *SP.  Read extended
   format if DOEXTEND.  Return 0 on success, an errno value on failure.  */
static int
tzload(char const *name, struct state *sp, bool doextend)
{
#ifdef ALL_STATE
	union local_storage *lsp = malloc(sizeof *lsp);
	if (!lsp) {
		return HAVE_MALLOC_ERRNO ? errno : ENOMEM;
	} else {
		int err = tzloadbody(name, sp, doextend, lsp);
		free(lsp);
		return err;
	}
#else
	union local_storage ls;
	return tzloadbody(name, sp, doextend, &ls);
#endif
}

static bool
typesequiv(const struct state *sp, int a, int b)
{
	bool result;

	if (sp == NULL || a < 0 || a >= sp->typecnt || b < 0 ||
	    b >= sp->typecnt)
		result = false;
	else {
		const struct ttinfo *ap = &sp->ttis[a];
		const struct ttinfo *bp = &sp->ttis[b];
		result = (ap->tt_utoff == bp->tt_utoff &&
			  ap->tt_isdst == bp->tt_isdst &&
			  ap->tt_ttisstd == bp->tt_ttisstd &&
			  ap->tt_ttisut == bp->tt_ttisut &&
			  (strcmp(&sp->chars[ap->tt_desigidx],
				  &sp->chars[bp->tt_desigidx]) == 0));
	}
	return result;
}

static const int mon_lengths[2][MONTHSPERYEAR] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static const int year_lengths[2] = { DAYSPERNYEAR, DAYSPERLYEAR };

/*
** Given a pointer into a timezone string, scan until a character that is not
** a valid character in a time zone abbreviation is found.
** Return a pointer to that character.
*/

static ATTRIBUTE_PURE const char *
getzname(const char *strp)
{
	char c;

	while ((c = *strp) != '\0' && !is_digit(c) && c != ',' && c != '-' &&
	       c != '+')
		++strp;
	return strp;
}

/*
** Given a pointer into an extended timezone string, scan until the ending
** delimiter of the time zone abbreviation is located.
** Return a pointer to the delimiter.
**
** As with getzname above, the legal character set is actually quite
** restricted, with other characters producing undefined results.
** We don't do any checking here; checking is done later in common-case code.
*/

static ATTRIBUTE_PURE const char *
getqzname(const char *strp, const int delim)
{
	int c;

	while ((c = *strp) != '\0' && c != delim)
		++strp;
	return strp;
}

/*
** Given a pointer into a timezone string, extract a number from that string.
** Check that the number is within a specified range; if it is not, return
** NULL.
** Otherwise, return a pointer to the first character not part of the number.
*/

static const char *
getnum(const char *strp, int *const nump, const int min, const int max)
{
	char c;
	int num;

	if (strp == NULL || !is_digit(c = *strp))
		return NULL;
	num = 0;
	do {
		num = num * 10 + (c - '0');
		if (num > max)
			return NULL; /* illegal value */
		c = *++strp;
	} while (is_digit(c));
	if (num < min)
		return NULL; /* illegal value */
	*nump = num;
	return strp;
}

/*
** Given a pointer into a timezone string, extract a number of seconds,
** in hh[:mm[:ss]] form, from the string.
** If any error occurs, return NULL.
** Otherwise, return a pointer to the first character not part of the number
** of seconds.
*/

static const char *
getsecs(const char *strp, int_fast32_t *const secsp)
{
	int num;
	int_fast32_t secsperhour = SECSPERHOUR;

	/*
	** 'HOURSPERDAY * DAYSPERWEEK - 1' allows quasi-Posix rules like
	** "M10.4.6/26", which does not conform to Posix,
	** but which specifies the equivalent of
	** "02:00 on the first Sunday on or after 23 Oct".
	*/
	strp = getnum(strp, &num, 0, HOURSPERDAY * DAYSPERWEEK - 1);
	if (strp == NULL)
		return NULL;
	*secsp = num * secsperhour;
	if (*strp == ':') {
		++strp;
		strp = getnum(strp, &num, 0, MINSPERHOUR - 1);
		if (strp == NULL)
			return NULL;
		*secsp += num * SECSPERMIN;
		if (*strp == ':') {
			++strp;
			/* 'SECSPERMIN' allows for leap seconds.  */
			strp = getnum(strp, &num, 0, SECSPERMIN);
			if (strp == NULL)
				return NULL;
			*secsp += num;
		}
	}
	return strp;
}

/*
** Given a pointer into a timezone string, extract an offset, in
** [+-]hh[:mm[:ss]] form, from the string.
** If any error occurs, return NULL.
** Otherwise, return a pointer to the first character not part of the time.
*/

static const char *
getoffset(const char *strp, int_fast32_t *const offsetp)
{
	bool neg = false;

	if (*strp == '-') {
		neg = true;
		++strp;
	} else if (*strp == '+')
		++strp;
	strp = getsecs(strp, offsetp);
	if (strp == NULL)
		return NULL; /* illegal time */
	if (neg)
		*offsetp = -*offsetp;
	return strp;
}

/*
** Given a pointer into a timezone string, extract a rule in the form
** date[/time]. See POSIX section 8 for the format of "date" and "time".
** If a valid rule is not found, return NULL.
** Otherwise, return a pointer to the first character not part of the rule.
*/

static const char *
getrule(const char *strp, struct rule *const rulep)
{
	if (*strp == 'J') {
		/*
		** Julian day.
		*/
		rulep->r_type = JULIAN_DAY;
		++strp;
		strp = getnum(strp, &rulep->r_day, 1, DAYSPERNYEAR);
	} else if (*strp == 'M') {
		/*
		** Month, week, day.
		*/
		rulep->r_type = MONTH_NTH_DAY_OF_WEEK;
		++strp;
		strp = getnum(strp, &rulep->r_mon, 1, MONTHSPERYEAR);
		if (strp == NULL)
			return NULL;
		if (*strp++ != '.')
			return NULL;
		strp = getnum(strp, &rulep->r_week, 1, 5);
		if (strp == NULL)
			return NULL;
		if (*strp++ != '.')
			return NULL;
		strp = getnum(strp, &rulep->r_day, 0, DAYSPERWEEK - 1);
	} else if (is_digit(*strp)) {
		/*
		** Day of year.
		*/
		rulep->r_type = DAY_OF_YEAR;
		strp = getnum(strp, &rulep->r_day, 0, DAYSPERLYEAR - 1);
	} else
		return NULL; /* invalid format */
	if (strp == NULL)
		return NULL;
	if (*strp == '/') {
		/*
		** Time specified.
		*/
		++strp;
		strp = getoffset(strp, &rulep->r_time);
	} else
		rulep->r_time = 2 * SECSPERHOUR; /* default = 2:00:00 */
	return strp;
}

/*
** Given a year, a rule, and the offset from UT at the time that rule takes
** effect, calculate the year-relative time that rule takes effect.
*/

static int_fast32_t
transtime(const int year, const struct rule *const rulep,
	  const int_fast32_t offset)
{
	bool leapyear;
	int_fast32_t value;
	int i;
	int d, m1, yy0, yy1, yy2, dow;

	leapyear = isleap(year);
	switch (rulep->r_type) {

	case JULIAN_DAY:
		/*
		** Jn - Julian day, 1 == January 1, 60 == March 1 even in leap
		** years.
		** In non-leap years, or if the day number is 59 or less, just
		** add SECSPERDAY times the day number-1 to the time of
		** January 1, midnight, to get the day.
		*/
		value = (rulep->r_day - 1) * SECSPERDAY;
		if (leapyear && rulep->r_day >= 60)
			value += SECSPERDAY;
		break;

	case DAY_OF_YEAR:
		/*
		** n - day of year.
		** Just add SECSPERDAY times the day number to the time of
		** January 1, midnight, to get the day.
		*/
		value = rulep->r_day * SECSPERDAY;
		break;

	case MONTH_NTH_DAY_OF_WEEK:
		/*
		** Mm.n.d - nth "dth day" of month m.
		*/

		/*
		** Use Zeller's Congruence to get day-of-week of first day of
		** month.
		*/
		m1 = (rulep->r_mon + 9) % 12 + 1;
		yy0 = (rulep->r_mon <= 2) ? (year - 1) : year;
		yy1 = yy0 / 100;
		yy2 = yy0 % 100;
		dow = ((26 * m1 - 2) / 10 + 1 + yy2 + yy2 / 4 + yy1 / 4 -
		       2 * yy1) %
			7;
		if (dow < 0)
			dow += DAYSPERWEEK;

		/*
		** "dow" is the day-of-week of the first day of the month. Get
		** the day-of-month (zero-origin) of the first "dow" day of the
		** month.
		*/
		d = rulep->r_day - dow;
		if (d < 0)
			d += DAYSPERWEEK;
		for (i = 1; i < rulep->r_week; ++i) {
			if (d + DAYSPERWEEK >=
			    mon_lengths[leapyear][rulep->r_mon - 1])
				break;
			d += DAYSPERWEEK;
		}

		/*
		** "d" is the day-of-month (zero-origin) of the day we want.
		*/
		value = d * SECSPERDAY;
		for (i = 0; i < rulep->r_mon - 1; ++i)
			value += mon_lengths[leapyear][i] * SECSPERDAY;
		break;

	default:
		UNREACHABLE();
	}

	/*
	** "value" is the year-relative time of 00:00:00 UT on the day in
	** question. To get the year-relative time of the specified local
	** time on that day, add the transition time and the current offset
	** from UT.
	*/
	return value + rulep->r_time + offset;
}

/*
** Given a POSIX section 8-style TZ string, fill in the rule tables as
** appropriate.
*/

static bool
tzparse(const char *name, struct state *sp, struct state *basep)
{
	const char *stdname;
	const char *dstname;
	size_t stdlen;
	size_t dstlen;
	size_t charcnt;
	int_fast32_t stdoffset;
	int_fast32_t dstoffset;
	char *cp;
	bool load_ok;
	time_t atlo = TIME_T_MIN, leaplo = TIME_T_MIN;

	stdname = name;
	if (*name == '<') {
		name++;
		stdname = name;
		name = getqzname(name, '>');
		if (*name != '>')
			return false;
		stdlen = name - stdname;
		name++;
	} else {
		name = getzname(name);
		stdlen = name - stdname;
	}
	if (!stdlen)
		return false;
	name = getoffset(name, &stdoffset);
	if (name == NULL)
		return false;
	charcnt = stdlen + 1;
	if (sizeof sp->chars < charcnt)
		return false;
	if (basep) {
		if (0 < basep->timecnt)
			atlo = basep->ats[basep->timecnt - 1];
		load_ok = false;
		sp->leapcnt = basep->leapcnt;
		memcpy(sp->lsis, basep->lsis, sp->leapcnt * sizeof *sp->lsis);
	} else {
		load_ok = tzload(TZDEFRULES, sp, false) == 0;
		if (!load_ok)
			sp->leapcnt = 0; /* So, we're off a little.  */
	}
	if (0 < sp->leapcnt)
		leaplo = sp->lsis[sp->leapcnt - 1].ls_trans;
	if (*name != '\0') {
		if (*name == '<') {
			dstname = ++name;
			name = getqzname(name, '>');
			if (*name != '>')
				return false;
			dstlen = name - dstname;
			name++;
		} else {
			dstname = name;
			name = getzname(name);
			dstlen = name - dstname; /* length of DST abbr. */
		}
		if (!dstlen)
			return false;
		charcnt += dstlen + 1;
		if (sizeof sp->chars < charcnt)
			return false;
		if (*name != '\0' && *name != ',' && *name != ';') {
			name = getoffset(name, &dstoffset);
			if (name == NULL)
				return false;
		} else
			dstoffset = stdoffset - SECSPERHOUR;
		if (*name == '\0' && !load_ok)
			name = TZDEFRULESTRING;
		if (*name == ',' || *name == ';') {
			struct rule start;
			struct rule end;
			int year;
			int timecnt;
			time_t janfirst;
			int_fast32_t janoffset = 0;
			int yearbeg, yearlim;

			++name;
			if ((name = getrule(name, &start)) == NULL)
				return false;
			if (*name++ != ',')
				return false;
			if ((name = getrule(name, &end)) == NULL)
				return false;
			if (*name != '\0')
				return false;
			sp->typecnt = 2; /* standard time and DST */
			/*
			** Two transitions per year, from EPOCH_YEAR forward.
			*/
			init_ttinfo(&sp->ttis[0], -stdoffset, false, 0);
			init_ttinfo(&sp->ttis[1], -dstoffset, true, stdlen + 1);
			sp->defaulttype = 0;
			timecnt = 0;
			janfirst = 0;
			yearbeg = EPOCH_YEAR;

			do {
				int_fast32_t yearsecs =
					year_lengths[isleap(yearbeg - 1)] *
					SECSPERDAY;
				yearbeg--;
				if (increment_overflow_time(&janfirst,
							    -yearsecs)) {
					janoffset = -yearsecs;
					break;
				}
			} while (atlo < janfirst &&
				 EPOCH_YEAR - YEARSPERREPEAT / 2 < yearbeg);

			while (true) {
				int_fast32_t yearsecs =
					year_lengths[isleap(yearbeg)] *
					SECSPERDAY;
				int yearbeg1 = yearbeg;
				time_t janfirst1 = janfirst;
				if (increment_overflow_time(&janfirst1,
							    yearsecs) ||
				    increment_overflow(&yearbeg1, 1) ||
				    atlo <= janfirst1)
					break;
				yearbeg = yearbeg1;
				janfirst = janfirst1;
			}

			yearlim = yearbeg;
			if (increment_overflow(&yearlim, YEARSPERREPEAT + 1))
				yearlim = INT_MAX;
			for (year = yearbeg; year < yearlim; year++) {
				int_fast32_t starttime = transtime(year, &start,
								   stdoffset),
					     endtime = transtime(year, &end,
								 dstoffset);
				int_fast32_t yearsecs =
					(year_lengths[isleap(year)] *
					 SECSPERDAY);
				bool reversed = endtime < starttime;
				if (reversed) {
					int_fast32_t swap = starttime;
					starttime = endtime;
					endtime = swap;
				}
				if (reversed ||
				    (starttime < endtime &&
				     endtime - starttime < yearsecs)) {
					if (TZ_MAX_TIMES - 2 < timecnt)
						break;
					sp->ats[timecnt] = janfirst;
					if (!increment_overflow_time(
						    &sp->ats[timecnt],
						    janoffset + starttime) &&
					    atlo <= sp->ats[timecnt])
						sp->types[timecnt++] =
							!reversed;
					sp->ats[timecnt] = janfirst;
					if (!increment_overflow_time(
						    &sp->ats[timecnt],
						    janoffset + endtime) &&
					    atlo <= sp->ats[timecnt]) {
						sp->types[timecnt++] = reversed;
					}
				}
				if (endtime < leaplo) {
					yearlim = year;
					if (increment_overflow(&yearlim,
							       YEARSPERREPEAT +
								       1))
						yearlim = INT_MAX;
				}
				if (increment_overflow_time(
					    &janfirst, janoffset + yearsecs))
					break;
				janoffset = 0;
			}
			sp->timecnt = timecnt;
			if (!timecnt) {
				sp->ttis[0] = sp->ttis[1];
				sp->typecnt = 1; /* Perpetual DST.  */
			} else if (YEARSPERREPEAT < year - yearbeg)
				sp->goback = sp->goahead = true;
		} else {
			int_fast32_t theirstdoffset;
			int_fast32_t theirdstoffset;
			int_fast32_t theiroffset;
			bool isdst;
			int i;
			int j;

			if (*name != '\0')
				return false;
			/*
			** Initial values of theirstdoffset and theirdstoffset.
			*/
			theirstdoffset = 0;
			for (i = 0; i < sp->timecnt; ++i) {
				j = sp->types[i];
				if (!sp->ttis[j].tt_isdst) {
					theirstdoffset = -sp->ttis[j].tt_utoff;
					break;
				}
			}
			theirdstoffset = 0;
			for (i = 0; i < sp->timecnt; ++i) {
				j = sp->types[i];
				if (sp->ttis[j].tt_isdst) {
					theirdstoffset = -sp->ttis[j].tt_utoff;
					break;
				}
			}
			/*
			** Initially we're assumed to be in standard time.
			*/
			isdst = false;
			/*
			** Now juggle transition times and types
			** tracking offsets as you do.
			*/
			for (i = 0; i < sp->timecnt; ++i) {
				j = sp->types[i];
				sp->types[i] = sp->ttis[j].tt_isdst;
				if (sp->ttis[j].tt_ttisut) {
					/* No adjustment to transition time */
				} else {
					/*
					** If daylight saving time is in
					** effect, and the transition time was
					** not specified as standard time, add
					** the daylight saving time offset to
					** the transition time; otherwise, add
					** the standard time offset to the
					** transition time.
					*/
					/*
					** Transitions from DST to DDST
					** will effectively disappear since
					** POSIX provides for only one DST
					** offset.
					*/
					if (isdst && !sp->ttis[j].tt_ttisstd) {
						sp->ats[i] += dstoffset -
							theirdstoffset;
					} else {
						sp->ats[i] += stdoffset -
							theirstdoffset;
					}
				}
				theiroffset = -sp->ttis[j].tt_utoff;
				if (sp->ttis[j].tt_isdst)
					theirdstoffset = theiroffset;
				else
					theirstdoffset = theiroffset;
			}
			/*
			** Finally, fill in ttis.
			*/
			init_ttinfo(&sp->ttis[0], -stdoffset, false, 0);
			init_ttinfo(&sp->ttis[1], -dstoffset, true, stdlen + 1);
			sp->typecnt = 2;
			sp->defaulttype = 0;
		}
	} else {
		dstlen = 0;
		sp->typecnt = 1; /* only standard time */
		sp->timecnt = 0;
		init_ttinfo(&sp->ttis[0], -stdoffset, false, 0);
		sp->defaulttype = 0;
	}
	sp->charcnt = charcnt;
	cp = sp->chars;
	memcpy(cp, stdname, stdlen);
	cp += stdlen;
	*cp++ = '\0';
	if (dstlen != 0) {
		memcpy(cp, dstname, dstlen);
		*(cp + dstlen) = '\0';
	}
	return true;
}

/* Initialize *SP to a value appropriate for the TZ setting NAME.
   Return 0 on success, an errno value on failure.  */
static int
zoneinit(struct state *sp, char const *name)
{
	if (name && !name[0]) {
		/*
		** User wants it fast rather than right.
		*/
		sp->leapcnt = 0; /* so, we're off a little */
		sp->timecnt = 0;
		sp->typecnt = 0;
		sp->charcnt = 0;
		sp->goback = sp->goahead = false;
		init_ttinfo(&sp->ttis[0], 0, false, 0);
		strlcpy(sp->chars, gmt, sizeof sp->chars);
		sp->defaulttype = 0;
		return 0;
	} else {
		int err = tzload(name, sp, true);
		if (err != 0 && name && name[0] != ':' &&
		    tzparse(name, sp, NULL))
			err = 0;
		if (err == 0)
			scrub_abbrs(sp);
		return err;
	}
}

#if NETBSD_INSPIRED

timezone_t
tzalloc(char const *name)
{
	timezone_t sp = malloc(sizeof *sp);
	if (sp) {
		int err = zoneinit(sp, name);
		if (err != 0) {
			free(sp);
			errno = err;
			return NULL;
		}
	} else if (!HAVE_MALLOC_ERRNO)
		errno = ENOMEM;
	return sp;
}

void
tzfree(timezone_t sp)
{
	free(sp);
}

/*
** NetBSD 6.1.4 has ctime_rz, but omit it because POSIX says ctime and
** ctime_r are obsolescent and have potential security problems that
** ctime_rz would share.  Callers can instead use localtime_rz + strftime.
**
** NetBSD 6.1.4 has tzgetname, but omit it because it doesn't work
** in zones with three or more time zone abbreviations.
** Callers can instead use localtime_rz + strftime.
*/

#endif

/*
** The easy way to behave "as if no library function calls" localtime
** is to not call it, so we drop its guts into "localsub", which can be
** freely called. (And no, the PANS doesn't require the above behavior,
** but it *is* desirable.)
**
** If successful and SETNAME is nonzero,
** set the applicable parts of tzname, timezone and altzone;
** however, it's OK to omit this step if the timezone is POSIX-compatible,
** since in that case tzset should have already done this step correctly.
** SETNAME's type is intfast32_t for compatibility with gmtsub,
** but it is actually a boolean and its value should be 0 or 1.
*/

/*ARGSUSED*/
static struct tnt_tm *
localsub(struct state const *sp, time_t const *timep, int_fast32_t setname,
	 struct tnt_tm *const tmp)
{
	const struct ttinfo *ttisp;
	int i;
	struct tnt_tm *result;
	const time_t t = *timep;

	if (sp == NULL) {
		/* Don't bother to set tzname etc.; tzset has already done it.
		 */
		return gmtsub(gmtptr, timep, 0, tmp);
	}
	if ((sp->goback && t < sp->ats[0]) ||
	    (sp->goahead && t > sp->ats[sp->timecnt - 1])) {
		time_t newt;
		time_t seconds;
		time_t years;

		if (t < sp->ats[0])
			seconds = sp->ats[0] - t;
		else
			seconds = t - sp->ats[sp->timecnt - 1];
		--seconds;

		/* Beware integer overflow, as SECONDS might
		   be close to the maximum time_t.  */
		years = seconds / SECSPERREPEAT * YEARSPERREPEAT;
		seconds = years * AVGSECSPERYEAR;
		years += YEARSPERREPEAT;
		if (t < sp->ats[0])
			newt = t + seconds + SECSPERREPEAT;
		else
			newt = t - seconds - SECSPERREPEAT;

		if (newt < sp->ats[0] || newt > sp->ats[sp->timecnt - 1])
			return NULL; /* "cannot happen" */
		result = localsub(sp, &newt, setname, tmp);
		if (result) {
			int_fast64_t newy;

			newy = result->tm_year;
			if (t < sp->ats[0])
				newy -= years;
			else
				newy += years;
			if (!(INT_MIN <= newy && newy <= INT_MAX))
				return NULL;
			result->tm_year = newy;
		}
		return result;
	}
	if (sp->timecnt == 0 || t < sp->ats[0]) {
		i = sp->defaulttype;
	} else {
		int lo = 1;
		int hi = sp->timecnt;

		while (lo < hi) {
			int mid = (lo + hi) >> 1;

			if (t < sp->ats[mid])
				hi = mid;
			else
				lo = mid + 1;
		}
		i = sp->types[lo - 1];
	}
	ttisp = &sp->ttis[i];
	/*
	** To get (wrong) behavior that's compatible with System V Release 2.0
	** you'd replace the statement below with
	**	t += ttisp->tt_utoff;
	**	timesub(&t, 0L, sp, tmp);
	*/
	result = timesub(&t, ttisp->tt_utoff, sp, tmp);
	if (result) {
		result->tm_isdst = ttisp->tt_isdst;
#ifdef TM_ZONE
		result->TM_ZONE = (char *)&sp->chars[ttisp->tt_desigidx];
#endif /* defined TM_ZONE */
		if (setname)
			update_tzname_etc(sp, ttisp);
	}
	return result;
}

#if NETBSD_INSPIRED

struct tnt_tm *
tnt_localtime_rz(struct state *sp, time_t const *timep, struct tnt_tm *tmp)
{
	return localsub(sp, timep, 0, tmp);
}

#endif

/*
** gmtsub is to gmtime as localsub is to localtime.
*/

static struct tnt_tm *
gmtsub(struct state const *sp, time_t const *timep, int_fast32_t offset,
       struct tnt_tm *tmp)
{
	struct tnt_tm *result;

	result = timesub(timep, offset, gmtptr, tmp);
#ifdef TM_ZONE
	/*
	** Could get fancy here and deliver something such as
	** "+xx" or "-xx" if offset is non-zero,
	** but this is no time for a treasure hunt.
	*/
	tmp->TM_ZONE = ((char *)(offset	? wildabbr : gmtptr->chars));
#endif /* defined TM_ZONE */
	(void)sp;
	return result;
}

#ifdef STD_INSPIRED

struct tnt_tm *
offtime(const time_t *timep, long offset)
{
	gmtcheck();
	return gmtsub(gmtptr, timep, offset, &tm);
}

#endif /* defined STD_INSPIRED */

/*
** Return the number of leap years through the end of the given year
** where, to make the math easy, the answer for year zero is defined as zero.
*/

static time_t
leaps_thru_end_of_nonneg(time_t y)
{
	return y / 4 - y / 100 + y / 400;
}

static time_t
leaps_thru_end_of(time_t y)
{
	return (y < 0 ? -1 - leaps_thru_end_of_nonneg(-1 - y)
		      : leaps_thru_end_of_nonneg(y));
}

static struct tnt_tm *
timesub(const time_t *timep, int_fast32_t offset, const struct state *sp,
	struct tnt_tm *tmp)
{
	const struct lsinfo *lp;
	time_t tdays;
	const int *ip;
	int_fast32_t corr;
	int i;
	int_fast32_t idays, rem, dayoff, dayrem;
	time_t y;

	/* If less than SECSPERMIN, the number of seconds since the
	   most recent positive leap second; otherwise, do not add 1
	   to localtime tm_sec because of leap seconds.  */
	time_t secs_since_posleap = SECSPERMIN;

	corr = 0;
	i = (sp == NULL) ? 0 : sp->leapcnt;
	while (--i >= 0) {
		lp = &sp->lsis[i];
		if (*timep >= lp->ls_trans) {
			corr = lp->ls_corr;
			if ((i == 0 ? 0 : lp[-1].ls_corr) < corr)
				secs_since_posleap = *timep - lp->ls_trans;
			break;
		}
	}

	/* Calculate the year, avoiding integer overflow even if
	   time_t is unsigned.  */
	tdays = *timep / SECSPERDAY;
	rem = *timep % SECSPERDAY;
	rem += offset % SECSPERDAY - corr % SECSPERDAY + 3 * SECSPERDAY;
	dayoff = offset / SECSPERDAY - corr / SECSPERDAY + rem / SECSPERDAY - 3;
	rem %= SECSPERDAY;
	/* y = (EPOCH_YEAR
		+ floor((tdays + dayoff) / DAYSPERREPEAT) * YEARSPERREPEAT),
	   sans overflow.  But calculate against 1570 (EPOCH_YEAR -
	   YEARSPERREPEAT) instead of against 1970 so that things work
	   for localtime values before 1970 when time_t is unsigned.  */
	dayrem = tdays % DAYSPERREPEAT;
	dayrem += dayoff % DAYSPERREPEAT;
	y = (EPOCH_YEAR - YEARSPERREPEAT +
	     ((1 + dayoff / DAYSPERREPEAT + dayrem / DAYSPERREPEAT -
	       ((dayrem % DAYSPERREPEAT) < 0) + tdays / DAYSPERREPEAT) *
	      YEARSPERREPEAT));
	/* idays = (tdays + dayoff) mod DAYSPERREPEAT, sans overflow.  */
	idays = tdays % DAYSPERREPEAT;
	idays += dayoff % DAYSPERREPEAT + 2 * DAYSPERREPEAT;
	idays %= DAYSPERREPEAT;
	/* Increase Y and decrease IDAYS until IDAYS is in range for Y.  */
	while (year_lengths[isleap(y)] <= idays) {
		int tdelta = idays / DAYSPERLYEAR;
		int_fast32_t ydelta = tdelta + !tdelta;
		time_t newy = y + ydelta;
		int leapdays;
		leapdays =
			leaps_thru_end_of(newy - 1) - leaps_thru_end_of(y - 1);
		idays -= ydelta * DAYSPERNYEAR;
		idays -= leapdays;
		y = newy;
	}

	if (!TYPE_SIGNED(time_t) && y < TM_YEAR_BASE) {
		int signed_y = y;
		tmp->tm_year = signed_y - TM_YEAR_BASE;
	} else if ((!TYPE_SIGNED(time_t) || INT_MIN + TM_YEAR_BASE <= y) &&
		   y - TM_YEAR_BASE <= INT_MAX)
		tmp->tm_year = y - TM_YEAR_BASE;
	else {
		errno = EOVERFLOW;
		return NULL;
	}
	tmp->tm_yday = idays;
	/*
	** The "extra" mods below avoid overflow problems.
	*/
	tmp->tm_wday =
		(TM_WDAY_BASE +
		 ((tmp->tm_year % DAYSPERWEEK) * (DAYSPERNYEAR % DAYSPERWEEK)) +
		 leaps_thru_end_of(y - 1) -
		 leaps_thru_end_of(TM_YEAR_BASE - 1) + idays);
	tmp->tm_wday %= DAYSPERWEEK;
	if (tmp->tm_wday < 0)
		tmp->tm_wday += DAYSPERWEEK;
	tmp->tm_hour = rem / SECSPERHOUR;
	rem %= SECSPERHOUR;
	tmp->tm_min = rem / SECSPERMIN;
	tmp->tm_sec = rem % SECSPERMIN;

	/* Use "... ??:??:60" at the end of the localtime minute containing
	   the second just before the positive leap second.  */
	tmp->tm_sec += secs_since_posleap <= tmp->tm_sec;

	ip = mon_lengths[isleap(y)];
	for (tmp->tm_mon = 0; idays >= ip[tmp->tm_mon]; ++(tmp->tm_mon))
		idays -= ip[tmp->tm_mon];
	tmp->tm_mday = idays + 1;
	tmp->tm_isdst = 0;
	tmp->tm_gmtoff = offset;
	return tmp;
}

#if 0
char *
ctime(const time_t *timep)
{
	/*
	** Section 4.12.3.2 of X3.159-1989 requires that
	**	The ctime function converts the calendar time pointed to by
	*timer *	to local time in the form of a string. It is equivalent
	*to *		asctime(localtime(timer))
	*/
	struct tnt_tm *tmp = tnt_localtime(timep);
	return tmp ? asctime(tmp) : NULL;
}

char *
ctime_r(const time_t *timep, char *buf)
{
	struct tnt_tm mytm;
	struct tnt_tm *tmp = tnt_localtime_r(timep, &mytm);
	return tmp ? asctime_r(tmp, buf) : NULL;
}
#endif

/*
** Adapted from code provided by Robert Elz, who writes:
**	The "best" way to do mktime I think is based on an idea of Bob
**	Kridle's (so its said...) from a long time ago.
**	It does a binary search of the time_t space. Since time_t's are
**	just 32 bits, its a max of 32 iterations (even at 64 bits it
**	would still be very reasonable).
*/

#ifndef WRONG
#define WRONG (-1)
#endif /* !defined WRONG */

/*
** Normalize logic courtesy Paul Eggert.
*/

static bool
increment_overflow(int *ip, int j)
{
	int const i = *ip;

	/*
	** If i >= 0 there can only be overflow if i + j > INT_MAX
	** or if j > INT_MAX - i; given i >= 0, INT_MAX - i cannot overflow.
	** If i < 0 there can only be overflow if i + j < INT_MIN
	** or if j < INT_MIN - i; given i < 0, INT_MIN - i cannot overflow.
	*/
	if ((i >= 0) ? (j > INT_MAX - i) : (j < INT_MIN - i))
		return true;
	*ip += j;
	return false;
}

static bool
increment_overflow_time(time_t *tp, int_fast32_t j)
{
	/*
	** This is like
	** 'if (! (TIME_T_MIN <= *tp + j && *tp + j <= TIME_T_MAX)) ...',
	** except that it does the right thing even if *tp + j would overflow.
	*/
	if (!(j < 0 ? (TYPE_SIGNED(time_t) ? TIME_T_MIN - j <= *tp
					   : -1 - j < *tp)
		    : *tp <= TIME_T_MAX - j))
		return true;
	*tp += j;
	return false;
}

#ifdef STD_INSPIRED

time_t
timelocal(struct tnt_tm *tmp)
{
	if (tmp != NULL)
		tmp->tm_isdst = -1; /* in case it wasn't initialized */
	return mktime(tmp);
}

time_t
timegm(struct tnt_tm *tmp)
{
	return timeoff(tmp, 0);
}

time_t
timeoff(struct tnt_tm *tmp, long offset)
{
	if (tmp)
		tmp->tm_isdst = 0;
	gmtcheck();
	return time1(tmp, gmtsub, gmtptr, offset);
}

#endif /* defined STD_INSPIRED */

static int_fast32_t
leapcorr(struct state const *sp, time_t t)
{
	struct lsinfo const *lp;
	int i;

	i = sp->leapcnt;
	while (--i >= 0) {
		lp = &sp->lsis[i];
		if (t >= lp->ls_trans)
			return lp->ls_corr;
	}
	return 0;
}

#if TZ_TIME_T

#if !USG_COMPAT
#define daylight 0
#define timezone 0
#endif
#if !ALTZONE
#define altzone 0
#endif

/* Convert from the underlying system's time_t to the ersatz time_tz,
   which is called 'time_t' in this file.  Typically, this merely
   converts the time's integer width.  On some platforms, the system
   time is local time not UT, or uses some epoch other than the POSIX
   epoch.

   Although this code appears to define a function named 'time' that
   returns time_t, the macros in private.h cause this code to actually
   define a function named 'tz_time' that returns tz_time_t.  The call
   to sys_time invokes the underlying system's 'time' function.  */

time_t
time(time_t *p)
{
	time_t r = sys_time(0);
	if (r != (time_t)-1) {
		int_fast32_t offset =
			EPOCH_LOCAL ? (daylight ? timezone : altzone) : 0;
		if (increment_overflow32(&offset, -EPOCH_OFFSET) ||
		    increment_overflow_time(&r, offset)) {
			errno = EOVERFLOW;
			r = -1;
		}
	}
	if (p)
		*p = r;
	return r;
}

#endif
