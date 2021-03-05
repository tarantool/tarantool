/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include "trigger.h"

#include "fiber.h"

#include <small/region.h>

static void
trigger_fiber_run_timeout(ev_loop *loop, ev_timer *watcher, int revents)
{
	(void) loop;
	(void) revents;

	bool *expired = (bool *)watcher->data;
	assert(expired != NULL);
	*expired = true;
}

static int
trigger_fiber_f(va_list ap)
{
	struct trigger *trigger = va_arg(ap, struct trigger *);
	void *event = va_arg(ap, void *);
	return trigger->run(trigger, event);
}

int
trigger_run(struct rlist *list, void *event)
{
	struct trigger *trigger, *tmp;
	rlist_foreach_entry_safe(trigger, list, link, tmp)
		if (trigger->run(trigger, event) != 0)
			return -1;
	return 0;
}

int
trigger_run_reverse(struct rlist *list, void *event)
{
	struct trigger *trigger, *tmp;
	rlist_foreach_entry_safe_reverse(trigger, list, link, tmp)
		if (trigger->run(trigger, event) != 0)
			return -1;
	return 0;
}

void
trigger_fiber_run(struct rlist *list, void *event, double timeout)
{
	struct trigger *trigger, *tmp;
	unsigned trigger_count = 0;
	struct region *region = &fiber()->gc;
	RegionGuard guard(region);
	bool expired = false;
	struct ev_timer timer;
	ev_timer_init(&timer, trigger_fiber_run_timeout, timeout, 0);
	timer.data = &expired;
	/*
	 * We don't check if triggers are timed out during they launch
	 * since we want to give them all a chance to run. So in
	 * rlist_foreach_entry_safe loop, we run all the triggers,
	 * regardless of whether the timeout has expired or not.
	 */
	ev_timer_start(loop(), &timer);

	/*
	 * Calculating the total number of triggers.
	 */
	rlist_foreach_entry(trigger, list, link)
		trigger_count++;

	struct fiber **fibers = (struct fiber **)
		region_alloc(region, trigger_count * sizeof(struct fiber *));
	if (fibers == NULL) {
		say_error("Failed to allocate %lu bytes in region_alloc "
			  "for fiber", trigger_count * sizeof(struct fiber));
		ev_timer_stop(loop(), &timer);
		return;
	}

	unsigned current_fiber = 0;
	rlist_foreach_entry_safe(trigger, list, link, tmp) {
		char name[FIBER_NAME_INLINE];
		snprintf(name, FIBER_NAME_INLINE,
			 "trigger_fiber%d", current_fiber);
		fibers[current_fiber] = fiber_new(name, trigger_fiber_f);
		if (fibers[current_fiber] != NULL) {
			fiber_set_joinable(fibers[current_fiber], true);
			fiber_start(fibers[current_fiber], trigger, event);
			current_fiber++;
		} else {
			diag_log();
			diag_clear(diag_get());
			ev_timer_stop(loop(), &timer);
			return;
		}
	}

	/*
	 * Waiting for all triggers completion.
	 */
	for (unsigned int i = 0; i < current_fiber && ! expired; i++) {
		if (fiber_join_timeout(fibers[i], timeout) != 0) {
			assert(! diag_is_empty(diag_get()));
			diag_log();
			diag_clear(diag_get());
		}
	}
	if (expired) {
		say_error("on_shutdown triggers are timed out: "
			  "not all triggers might have finished yet");
	}
	ev_timer_stop(loop(), &timer);
}
