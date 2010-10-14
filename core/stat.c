/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <util.h>
#include <tarantool_ev.h>
#include <tbuf.h>
#include <say.h>

#include <third_party/khash.h>

KHASH_MAP_INIT_STR(char2int, i64, realloc);

#define SECS 5
static
khash_t(char2int) *
stats[SECS], *stats_all;
static ev_timer timer;

void
stat_collect(const char *name, int value)
{
	int ret;
	khiter_t k;

	k = kh_put(char2int, stats[0], name, &ret);
	if (ret == 0) {
		kh_value(stats[0], k) += value;
	} else {
		kh_key(stats[0], k) = name;
		kh_value(stats[0], k) = value;
	}

	k = kh_put(char2int, stats_all, name, &ret);
	if (ret == 0) {
		kh_value(stats_all, k) += value;
	} else {
		kh_key(stats_all, k) = name;
		kh_value(stats_all, k) = value;
	}
}

void
stat_print(struct tbuf *buf)
{
	int max_len = 0;
	tbuf_printf(buf, "statistics:\n");

	for (khiter_t k = kh_begin(stats_all); k != kh_end(stats_all); ++k) {
		if (!kh_exist(stats_all, k))
			continue;

		const char *key = kh_key(stats_all, k);
		max_len = MAX(max_len, strlen(key));
	}

	for (khiter_t k = kh_begin(stats_all); k != kh_end(stats_all); ++k) {
		if (!kh_exist(stats_all, k))
			continue;

		const char *key = kh_key(stats_all, k);
		int value = 0;
		for (int i = 0; i < SECS; i++) {
			khiter_t j = kh_get(char2int, stats[i], key);
			if (j != kh_end(stats[i]))
				value += kh_value(stats[i], j);
		}
		tbuf_printf(buf, "  %s:%*s{ rps:%- 6i, total:%- 12" PRIi64 " }\n",
			    key, 1 + max_len - (int)strlen(key), " ",
			    value / SECS, kh_value(stats_all, k));
	}
}

void
stat_age(ev_timer *timer, int events __unused__)
{
	khash_t(char2int) * last = stats[SECS - 1];
	for (int i = 0; i < SECS - 1; i++) {
		stats[i + 1] = stats[i];
	}
	stats[0] = last;
	kh_clear(char2int, last);
	ev_timer_again(timer);
}

void
stat_init(void)
{
	stats_all = kh_init(char2int, NULL);
	for (int i = 0; i < SECS; i++)
		stats[i] = kh_init(char2int, NULL);

	ev_init(&timer, stat_age);
	timer.repeat = 1.;
	ev_timer_again(&timer);
}
