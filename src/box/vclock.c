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
#include "vclock.h"

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#include "diag.h"

int64_t
vclock_follow(struct vclock *vclock, uint32_t replica_id, int64_t lsn)
{
	assert(lsn >= 0);
	assert(replica_id < VCLOCK_MAX);
	int64_t prev_lsn = vclock->lsn[replica_id];
	assert(lsn > prev_lsn);
	/* Easier add each time than check. */
	vclock->map |= 1 << replica_id;
	vclock->lsn[replica_id] = lsn;
	vclock->signature += lsn - prev_lsn;
	return prev_lsn;
}

CFORMAT(printf, 4, 0) static inline int
rsnprintf(char **buf, char **pos, char **end, const char *fmt, ...)
{
	int rc = 0;
	va_list ap;

	while (1) {
		va_start(ap, fmt);
		int n = vsnprintf(*pos, *end - *pos, fmt, ap);
		va_end(ap);
		assert(n > -1); /* glibc >= 2.0.6, see vsnprintf(3) */
		if (n < *end - *pos) {
			*pos += n;
			break;
		}

		/* Reallocate buffer */
		ptrdiff_t cap = (*end - *buf) > 0 ? (*end - *buf) : 32;
		while (cap <= *pos - *buf + n)
			cap *= 2;
		char *chunk = (char *) realloc(*buf, cap);
		if (chunk == NULL) {
			diag_set(OutOfMemory, cap, "malloc", "vclock");
			free(*buf);
			*buf = *end = *pos = NULL;
			rc = -1;
			break;
		}
		*pos = chunk + (*pos - *buf);
		*end = chunk + cap;
		*buf = chunk;
	}

	return rc;
}

char *
vclock_to_string(const struct vclock *vclock)
{
	(void) vclock;
	char *buf = NULL, *pos = NULL, *end = NULL;

	if (rsnprintf(&buf, &pos, &end, "{") != 0)
		return NULL;

	const char *sep = "";
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	vclock_foreach(&it, replica) {
		if (rsnprintf(&buf, &pos, &end, "%s%u: %lld", sep,
			      replica.id, (long long) replica.lsn) != 0)
			return NULL;
		sep = ", ";
	}

	if (rsnprintf(&buf, &pos, &end, "}") != 0)
		return NULL;

	return buf;
}

size_t
vclock_from_string(struct vclock *vclock, const char *str)
{
	long replica_id;
	long long lsn;

	const char *p = str;
	begin:
		if (*p == '{') {
			++p;
			goto key;
		} else if (isblank(*p)) {
			++p;
			goto begin;
		}
		goto error;
	key:
		if (isdigit(*p)) {
			errno = 0;
			replica_id = strtol(p, (char **) &p, 10);
			if (errno != 0 || replica_id < 0 || replica_id >= VCLOCK_MAX)
				goto error;
			goto sep;
		} else if (*p == '}') {
			++p;
			goto end;
		} else if (isblank(*p)) {
			++p;
			goto key;
		}
		goto error;
	sep:
		if (*p == ':') {
			++p;
			goto val;
		} else if (isblank(*p)) {
			++p;
			goto sep;
		}
		goto error;
	val:
		if (isblank(*p)) {
			++p;
			goto val;
		} else if (isdigit(*p)) {
			errno = 0;
			lsn = strtoll(p, (char **)  &p, 10);
			if (errno != 0 || lsn < 0 || lsn > INT64_MAX ||
			    replica_id >= VCLOCK_MAX || vclock->lsn[replica_id] > 0)
				goto error;
			vclock->map |= 1 << replica_id;
			vclock->lsn[replica_id] = lsn;
			goto comma;
		}
		goto error;
	comma:
		if (isspace(*p)) {
			++p;
			goto comma;
		} else if (*p == '}') {
			++p;
			goto end;
		} else if (*p == ',') {
			++p;
			goto key;
		}
		goto error;
	end:
		if (*p == '\0') {
			vclock->signature = vclock_calc_sum(vclock);
			return 0;
		} else if (isblank(*p)) {
			++p;
			goto end;
		}
		/* goto error; */
	error:
		return p - str + 1; /* error */
}

static int
vclockset_node_compare(const struct vclock *a, const struct vclock *b)
{
	int res = vclock_compare(a, b);
	/*
	 * In a vclock set, we do not allow clocks which are not
	 * strictly ordered.
	 * See also xdir_scan(), in which we check & skip
	 * duplicate vclocks.
	 */
	if (res == VCLOCK_ORDER_UNDEFINED)
		return 0;
	return res;
}

rb_gen(, vclockset_, vclockset_t, struct vclock, link, vclockset_node_compare);
