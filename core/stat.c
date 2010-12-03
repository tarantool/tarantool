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
#include <stat.h>

#include <third_party/khash.h>

#define SECS 5
static ev_timer timer;

struct {
	char *name;
	i64 value[SECS + 1];
} *stats = NULL;
static int stats_size = 0;
static int stats_max = 0;
static int base = 0;

int
stat_register(char **name, size_t count)
{
	int initial_base = base;

	for (int i = 0; i < count; i++, name++, base++) {
		if (stats_size <= base) {
			stats_size += 1024;
			stats = realloc(stats, sizeof(*stats) * stats_size);
			if (stats == NULL)
				abort();
		}

		stats[base].name = *name;

		if (*name == NULL)
			continue;

		for (int i = 0; i < SECS + 1; i++)
			stats[base].value[i] = 0;

		stats_max = base;
	}

	return initial_base;
}

void
stat_collect(int base, int name, i64 value)
{
	stats[base + name].value[0] += value;
	stats[base + name].value[SECS] += value;
}

void
stat_print(struct tbuf *buf)
{
	int max_len = 0;
	tbuf_printf(buf, "statistics:\n");

	for (int i = 0; i <= stats_max; i++) {
		if (stats[i].name == NULL)
			continue;
		max_len = MAX(max_len, strlen(stats[i].name));
	}

	for (int i = 0; i <= stats_max; i++) {
		if (stats[i].name == NULL)
			continue;

		int diff = 0;
		for (int j = 0; j < SECS; j++)
			diff += stats[i].value[j];

		diff /= SECS;

		tbuf_printf(buf, "  %s:%*s{ rps: %- 6i, total: %- 12" PRIi64 " }\n",
			    stats[i].name, 1 + max_len - (int)strlen(stats[i].name), " ",
			    diff, stats[i].value[SECS]);
	}
}

void
stat_age(ev_timer *timer, int events __unused__)
{
	for (int i = 0; i <= stats_max; i++) {
		if (stats[i].name == NULL)
			continue;

		for (int j = SECS - 2; j >= 0;  j--)
			stats[i].value[j + 1] = stats[i].value[j];
		stats[i].value[0] = 0;
	}

	ev_timer_again(timer);
}

void
stat_init(void)
{
	ev_init(&timer, stat_age);
	timer.repeat = 1.;
	ev_timer_again(&timer);
}
