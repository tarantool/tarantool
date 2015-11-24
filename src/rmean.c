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

#include "fiber.h"

void
rmean_roll(int64_t *value, double dt)
{
	value[0] /= dt;
	int j = RMEAN_WINDOW;
	/* in case when dt >= 2. we update not only last counter */
	for (; j > (int)(dt + 0.1); j--)
		value[j] = value[j - 1];
	for (; j > 0; j--)
		value[j] = value[0];
	value[0] = 0;
}

int64_t
rmean_mean(int64_t *value)
{
	int64_t mean = 0;
	for (size_t j = 1; j <= RMEAN_WINDOW; j++)
		mean += value[j];
	/* value[0] not adds because second isn't over */

	return mean / RMEAN_WINDOW;
}

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
		int res = cb(rmean->stats[i].name,
			     rmean_mean(rmean->stats[i].value),
			     rmean->stats[i].total,
			     cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;

}

static void
rmean_age(ev_loop *loop,
	  ev_timer *timer, int events)
{
	(void) events;
	struct rmean *rmean = (struct rmean *) timer->data;

	double dt = rmean->prev_ts;
	rmean->prev_ts = ev_now(loop);
	dt = rmean->prev_ts - dt;
	for (size_t i = 0; i < rmean->stats_n; i++) {
		if (rmean->stats[i].name == NULL)
			continue;
		rmean_roll(rmean->stats[i].value, dt);
	}

	ev_timer_again(loop, timer);
}

struct rmean *
rmean_new(const char **name, size_t n)
{
	struct rmean *rmean = (struct rmean *)
		malloc(sizeof(struct rmean) + sizeof(struct stats) * n);
	if (rmean == NULL)
		return NULL;
	memset(rmean, 0, sizeof(struct rmean) + sizeof(struct stats) * n);
	rmean->stats_n = n;
	rmean->timer.data = (void *)rmean;
	for (size_t i = 0; i < n; i++, name++) {
		rmean->stats[i].name = *name;
	}
	rmean->prev_ts = ev_now(loop());
	ev_timer_init(&rmean->timer, rmean_age, 0, 1.);
	ev_timer_again(loop(), &rmean->timer);
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

