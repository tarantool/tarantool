/*
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
#include "say.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

int64_t
vclock_follow(struct vclock *vclock, uint32_t server_id, int64_t lsn)
{
	assert(lsn >= 0);
	assert(server_id < VCLOCK_MAX);
	int64_t prev_lsn = vclock->lsn[server_id];
	if (lsn <= prev_lsn) {
		/* Never confirm LSN out of order. */
		panic("LSN for %u is used twice or COMMIT order is broken: "
		      "confirmed: %lld, new: %lld",
		      (unsigned) server_id,
		      (long long) prev_lsn, (long long) lsn);
	}
	vclock->lsn[server_id] = lsn;
	return prev_lsn;
}

void
vclock_merge(struct vclock *to, const struct vclock *with)
{
	/* Botched logic:
	 * - imagine there is 5.snap and 1.xlog
	 * - 5.snap has 1: 5 vclock
	 * - 1.xlog has 1: 1 vclock
	 * We begin reading the xlog after snap,
	 * but we don't skip setlsn (we never skip setlsn).
	 * So we must not update server id 1 with lsn 1,
	 * hence the code below only updates target if it
	 * is less than the source.
	 */
	for (int i = 0; i < VCLOCK_MAX; i++)
		if (with->lsn[i] > to->lsn[i])
			to->lsn[i] = with->lsn[i];
}

static inline __attribute__ ((format(FORMAT_PRINTF, 4, 0))) int
rsnprintf(char **buf, char **pos, char **end, const char *fmt, ...)
{
	int rc = 0;
	va_list ap;
	va_start(ap, fmt);

	while (1) {
		int n = vsnprintf(*pos, *end - *pos, fmt, ap);
		assert(n > -1); /* glibc >= 2.0.6, see vsnprintf(3) */
		if (n < *end - *pos) {
			*pos += n;
			break;
		}

		/* Reallocate buffer */
		size_t cap = (*end - *buf) > 0 ? (*end - *buf) : 32;
		while (cap <= *pos - *buf + n)
			cap *= 2;
		char *chunk = (char *) realloc(*buf, cap);
		if (chunk == NULL) {
			free(*buf);
			*buf = *end = *pos = NULL;
			rc = -1;
			break;
		}
		*pos = chunk + (*pos - *buf);
		*end = chunk + cap;
		*buf = chunk;
	}

	va_end(ap);
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
	for (uint32_t node_id = 0; node_id < VCLOCK_MAX; node_id++) {
		if (vclock->lsn[node_id] < 0)
			continue;
		if (rsnprintf(&buf, &pos, &end, "%s%u: %lld", sep, node_id,
		    (long long) vclock->lsn[node_id]) != 0)
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
	long node_id;
	long long lsn;

	const char *p = str;
	begin:
		if (*p == '{') {
			++p;
			/* goto key; */
		} else if (isblank(*p)) {
			++p;
			goto begin;
		} else goto error;
	key:
		if (isdigit(*p)) {
			errno = 0;
			node_id = strtol(p, (char **) &p, 10);
			if (errno != 0 || node_id < 0 || node_id >= VCLOCK_MAX)
				goto error;
			/* goto sep; */
		} else if (*p == '}') {
			++p;
			goto end;
		} else if (isblank(*p)) {
			++p;
			goto key;
		} else goto error;
	sep:
		if (*p == ':') {
			++p;
			/* goto val; */
		} else if (isblank(*p)) {
			++p;
			goto sep;
		} else goto error;
	val:
		if (isblank(*p)) {
			++p;
			goto val;
		} else if (isdigit(*p)) {
			errno = 0;
			lsn = strtoll(p, (char **)  &p, 10);
			if (errno != 0 || lsn < 0 || lsn > INT64_MAX ||
			    vclock->lsn[node_id] != -1)
				goto error;
			vclock->lsn[node_id] = lsn;
			/* goto comma; */
		} else goto error;
	comma:
		if (isspace(*p)) {
			++p;
			goto comma;
		} else if (*p == '}') {
			++p;
			/* goto end; */
		} else if (*p == ',') {
			++p;
			goto key;
		} else goto error;
	end:
		if (*p == '\0') {
			return 0;
		} else if (isblank(*p)) {
			++p;
			goto end;
		} else goto error;
	error:
		return p - str + 1; /* error */
}

static int
vclockset_node_compare(const struct vclock *a, const struct vclock *b)
{
	int res = vclock_compare(a, b);
	if (res == VCLOCK_ORDER_UNDEFINED)
		return 0;
	return res;
}

rb_gen(, vclockset_, vclockset_t, struct vclock, link, vclockset_node_compare);
