/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "checkpoint_schedule.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

void
checkpoint_schedule_cfg(struct checkpoint_schedule *sched,
			double now, double interval)
{
	sched->interval = interval;
	sched->start_time = now + interval;

	/*
	 * Add a random offset to the start time so as to avoid
	 * simultaneous checkpointing when multiple instances
	 * are running on the same host.
	 */
	if (interval > 0)
		sched->start_time += fmod(rand(), interval);
}

void
checkpoint_schedule_reset(struct checkpoint_schedule *sched, double now)
{
	sched->start_time = now + sched->interval;
}

double
checkpoint_schedule_timeout(struct checkpoint_schedule *sched, double now)
{
	if (sched->interval <= 0)
		return 0; /* checkpointing disabled */

	if (now < sched->start_time)
		return sched->start_time - now;

	/* Time elapsed since the last checkpoint. */
	double elapsed = fmod(now - sched->start_time, sched->interval);

	/* Time left to the next checkpoint. */
	double timeout = sched->interval - elapsed;

	assert(timeout > 0);
	return timeout;
}
