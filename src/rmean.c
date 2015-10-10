/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "rmean.h"

#include "say.h"
#include "assoc.h"

void
rmean_collect(struct rmean *rmean, size_t name, int64_t value)
{
	assert(name < rmean->stats_n);

	rmean->stats[name].value[0] += value;
	rmean->stats[name].total += value;
}

int
rmean_foreach(struct rmean *rmean, rmean_cb cb, void *cb_ctx)
{
	for (size_t i = 0; i < rmean->stats_n; i++) {
		if (rmean->stats[i].name == NULL)
			continue;

		int diff = 0;
		for (size_t j = 1; j <= RMEAN_WINDOW; j++)
			diff += rmean->stats[i].value[j];
		/* value[0] not adds because second isn't over */

		diff /= RMEAN_WINDOW;

		int res = cb(rmean->stats[i].name, diff,
			     rmean->stats[i].total, cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;

}

void
rmean_age(ev_loop *loop,
	  ev_timer *timer, int events)
{
	(void) loop;
	(void) events;
	struct rmean *rmean = (struct rmean *) timer->data;

	for (size_t i = 0; i < rmean->stats_n; i++) {
		if (rmean->stats[i].name == NULL)
			continue;

		for (int j = RMEAN_WINDOW - 1; j >= 0;  j--)
			rmean->stats[i].value[j + 1] =
				rmean->stats[i].value[j];
		rmean->stats[i].value[0] = 0;
	}

	ev_timer_again(loop(), timer);
}

void
rmean_timer_tick(struct rmean *rmean)
{
	rmean_age(loop(), &rmean->timer, 0);
}

struct rmean *
rmean_new(const char **name, size_t n)
{
	struct rmean *rmean = (struct rmean *)
		realloc(NULL,
			sizeof(struct rmean) +
			sizeof(struct stats) * n);
	if (rmean == NULL)
		return NULL;
	memset(rmean, 0, sizeof(struct rmean) + sizeof(struct stats) * n);
	rmean->stats_n = n;
	rmean->timer.data = (void *)rmean;
	ev_timer_init(&rmean->timer, rmean_age, 0, 1.);
	ev_timer_again(loop(), &rmean->timer);
	for (size_t i = 0; i < n; i++, name++) {
		rmean->stats[i].name = *name;

		if (*name == NULL)
			continue;
	}
	return rmean;
}

void
rmean_delete(struct rmean *rmean)
{
	ev_timer_stop(loop(), &rmean->timer);
	free(rmean);
	rmean = 0;
}

void
rmean_cleanup(struct rmean *rmean)
{
	for (size_t i = 0; i < rmean->stats_n; i++) {
		for (size_t j = 0; j < RMEAN_WINDOW + 1; j++)
			rmean->stats[i].value[j] = 0;
		rmean->stats[i].total = 0;
	}
}

