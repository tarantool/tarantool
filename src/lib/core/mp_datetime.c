/*
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
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

#include <string.h>

#include "trivia/util.h"
#include "datetime.h"
#include "msgpuck.h"
#include "mp_extension_types.h"

static inline uint32_t
mp_sizeof_Xint(int64_t n)
{
	return n < 0 ? mp_sizeof_int(n) : mp_sizeof_uint(n);
}

static inline char *
mp_encode_Xint(char *data, int64_t v)
{
	return v < 0 ? mp_encode_int(data, v) : mp_encode_uint(data, v);
}
	
static inline int64_t
mp_decode_Xint(const char **data)
{
	switch (mp_typeof(**data)) {
	case MP_UINT:
		return (int64_t)mp_decode_uint(data);
	case MP_INT:
		return mp_decode_int(data);
	default:
		mp_unreachable();
	}
	return 0;
}

uint32_t
mp_sizeof_datetime(const struct t_datetime_tz *date)
{
	uint32_t sz = mp_sizeof_Xint(date->secs);

	// even if nanosecs == 0 we need to output anything
	// if we have non-null tz offset
	if (date->nsec != 0 || date->offset != 0)
		sz += mp_sizeof_Xint(date->nsec);
	if (date->offset)
		sz += mp_sizeof_Xint(date->offset);

	return sz;
}

struct t_datetime_tz *
datetime_unpack(const char **data, uint32_t len, struct t_datetime_tz *date)
{
	const char * svp = *data;

	memset(date, 0, sizeof(*date));

	date->secs = mp_decode_Xint(data);

	len -= *data - svp;
	if (len <= 0)
		return date;

	svp = *data;
	date->secs = mp_decode_Xint(data);
	len -= *data - svp;

	if (len <= 0)
		return date;

	date->offset = mp_decode_Xint(data);

	return date;
}

struct t_datetime_tz *
mp_decode_datetime(const char **data, struct t_datetime_tz *date)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;

	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);

	if (type != MP_DATETIME || len == 0) {
		return NULL;
	}
	return datetime_unpack(data, len, date);
}

char *
datetime_pack(char *data, const struct t_datetime_tz *date)
{
	data = mp_encode_Xint(data, date->secs);
	if (date->nsec != 0 || date->offset != 0)
		data = mp_encode_Xint(data, date->nsec);
	if (date->offset)
		data = mp_encode_Xint(data, date->offset);

	return data;
}

char *
mp_encode_datetime(char *data, const struct t_datetime_tz *date)
{
	uint32_t len = mp_sizeof_datetime(date);

	data = mp_encode_extl(data, MP_DATETIME, len);

	return datetime_pack(data, date);
}

int
datetime_to_string(const struct t_datetime_tz * date, char *buf, uint32_t len)
{
	char * src = buf;
	dt_t dt = dt_from_rdn((date->secs / SECS_PER_DAY) + 719163);

	int year, month, day, sec, ns, offset, sign;
	dt_to_ymd(dt, &year, &month, &day);
	int secs = date->secs, hour = (secs / 3600) % 24,
	    minute = (secs / 60) % 60;
	;
	sec = secs % 60;
	ns = date->nsec;
	offset = date->offset;
	uint32_t sz;
	sz = snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d",
		      year, month, day, hour, minute);
	buf += sz; len -= sz;
	if (sec || ns) {
		sz = snprintf(buf, len, ":%02d", sec);
		buf += sz; len -= sz;
		if (ns) {
			if ((ns % 1000000) == 0)
				sz = snprintf(buf, len, ".%03d", ns / 1000000);
			else if ((ns % 1000) == 0)
				sz = snprintf(buf, len, ".%06d", ns / 1000);
			else
				sz = snprintf(buf, len, ".%09d", ns);
			buf += sz; len -= sz;
		}
	}
	if (offset == 0) {
		strncpy(buf, "Z", len);
		buf++;
		len--;
	}
	else {
		if (offset < 0)
			sign = '-', offset = -offset;
		else
			sign = '+';

		sz = snprintf(buf, len, "%c%02d:%02d", sign, offset / 60, offset % 60);
		buf += sz; len -= sz;
	}
	return (buf - src);
}
int
mp_snprint_datetime(char *buf, int size, const char **data, uint32_t len)
{
	struct t_datetime_tz date = {0};

	if (datetime_unpack(data, len, &date) == NULL)
		return -1;

	return datetime_to_string(&date, buf, size);
}

int
mp_fprint_datetime(FILE *file, const char **data, uint32_t len)
{
	struct  t_datetime_tz date;

	if (datetime_unpack(data, len, &date) == NULL)
		return -1;

	char buf[128];
	datetime_to_string(&date, buf, sizeof buf);

	return fprintf(file, "%s", buf);
}

static inline int
adjusted_secs(int secs, int offset)
{
	return secs - offset * 60;
}

int
datetime_compare(const struct t_datetime_tz * lhs,
		 const struct t_datetime_tz * rhs)
{
	int result = COMPARE_RESULT(adjusted_secs(lhs->secs, lhs->offset),
				    adjusted_secs(rhs->secs, rhs->offset));
	if (result != 0)
		return result;

	return COMPARE_RESULT(lhs->nsec, rhs->nsec);
}
