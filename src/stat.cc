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
#include "stat.h"

#include "tarantool/util.h"
#include <tarantool_ev.h>
#include <tbuf.h>
#include <say.h>

#include <assoc.h>

#define SECS 5
static ev_timer timer;

struct stats {
	const char *name;
	i64 value[SECS + 1];
} *stats = NULL;
static int stats_size = 0;
static int stats_max = 0;
static int base = 0;
int stat_max_name_len = 0;

static void
stat_recalc_max_name_len()
{
	stat_max_name_len = 0;
	for (unsigned i = 0; i <= stats_max; i++) {
		if (stats[i].name != NULL)
			stat_max_name_len = MAX(stat_max_name_len,
						strlen(stats[i].name));
	}
}

int
stat_register(const char **name, size_t max_idx)
{
	int initial_base = base;

	for (int i = 0; i < max_idx; i++, name++, base++) {
		if (stats_size <= base) {
			stats_size += 1024;
			stats = (struct stats *) realloc(stats, sizeof(*stats) * stats_size);
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
	stat_recalc_max_name_len();

	return initial_base;
}

void
stat_collect(int base, int name, i64 value)
{
	stats[base + name].value[0] += value;
	stats[base + name].value[SECS] += value;
}

int
stat_foreach(stat_cb cb, void *cb_ctx)
{
	for (unsigned i = 0; i <= stats_max; i++) {
		if (stats[i].name == NULL)
			continue;

		int diff = 0;
		for (int j = 0; j < SECS; j++)
			diff += stats[i].value[j];

		diff /= SECS;

		int res = cb(stats[i].name, diff,
			     stats[i].value[SECS], cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;

}

void
stat_age(ev_timer *timer, int events __attribute__((unused)))
{
	if (stats == NULL)
		return;

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

void
stat_free(void)
{
	ev_timer_stop(&timer);
	if (stats)
		free(stats);
}

void
stat_cleanup(int base, size_t max_idx)
{
	for (int i = base; i < max_idx; i++)
		for (int j = 0; j < SECS + 1; j++)
			stats[i].value[j] = 0;
}
