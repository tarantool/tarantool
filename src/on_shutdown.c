/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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

#include "on_shutdown.h"

#include "box/box.h"
#include "fiber.h"
#include "say.h"

#include <stdlib.h>
#include <small/rlist.h>
#include <errno.h>
#include <core/event.h>
#include <core/func_adapter.h>
#include <core/trigger.h>

struct on_shutdown_trigger {
	struct trigger trigger;
	/** Shutdown trigger function */
	int (*handler)(void *);
	/** Trigger function argument */
	void *arg;
	/** link for on_shutdown_trigger_list */
	struct rlist link;
};

static struct rlist on_shutdown_trigger_list =
	RLIST_HEAD_INITIALIZER(on_shutdown_trigger_list);

static int
trigger_commom_f(struct trigger *trigger, MAYBE_UNUSED void *event)
{
	struct on_shutdown_trigger *on_shutdown_trigger =
		container_of(trigger, struct on_shutdown_trigger, trigger);
	return on_shutdown_trigger->handler(on_shutdown_trigger->arg);
}

static int
on_shutdown_trigger_create(int (*handler)(void *), void *arg)
{
	struct on_shutdown_trigger *trigger = (struct on_shutdown_trigger *)
		malloc(sizeof(struct on_shutdown_trigger));
	if (trigger == NULL)
		return -1;
	trigger_create(&trigger->trigger, trigger_commom_f, NULL, NULL);
	trigger->handler = handler;
	trigger->arg = arg;
	trigger_add(&box_on_shutdown_trigger_list, &trigger->trigger);
	rlist_add_entry(&on_shutdown_trigger_list, trigger, link);
	return 0;
}

API_EXPORT int
box_on_shutdown(void *arg, int (*new_handler)(void *),
		int (*old_handler)(void *))
{
	struct on_shutdown_trigger *trigger;
	if (old_handler == NULL) {
		if (new_handler == NULL) {
			/*
			 * Invalid function params, old_handler or new_handler
			 * must be set
			 */
			say_error("Invalid function argument: old_handler and "
				  "new_handler cannot be equal to zero at the "
				  "same time.");
			errno = EINVAL;
			return -1;
		}
		return on_shutdown_trigger_create(new_handler, arg);
	}

	rlist_foreach_entry(trigger, &on_shutdown_trigger_list, link) {
		if (trigger->handler == old_handler) {
			if (new_handler != NULL) {
				/*
				 * Change on_shutdown trigger handler, and arg
				 */
				trigger->handler = new_handler;
				trigger->arg = arg;
			} else {
				/*
				 * In case new_handler == NULL
				 * Remove old on_shutdown trigger and
				 * destroy it
				 */
				trigger_clear(&trigger->trigger);
				rlist_del_entry(trigger, link);
				free(trigger);
			}
			return 0;
		}
	}

	/*
	 * Here we are in case when we not find on_shutdown trigger,
	 * which we want to destroy return -1.
	 */
	say_error("Invalid function argument: previously registered trigger "
		  "with handler == old_handler not found.");
	errno = EINVAL;
	return -1;
}

/**
 * Callback that is fired when ev_timer is expired.
 */
static void
on_shutdown_run_triggers_timeout(ev_loop *loop, ev_timer *watcher, int revents)
{
	(void)loop;
	(void)revents;

	bool *expired = (bool *)watcher->data;
	assert(expired != NULL);
	*expired = true;
}

/**
 * A wrapper over trigger->run to call in a separate fiber.
 */
static int
on_shutdown_trigger_fiber_f(va_list ap)
{
	struct trigger *trigger = va_arg(ap, struct trigger *);
	int rc = trigger->run(trigger, NULL);
	if (trigger->destroy != NULL)
		trigger->destroy(trigger);
	return rc;
}

/**
 * Runs trigger from event, passed with va_list.
 */
static int
on_shutdown_event_trigger_fiber_f(va_list ap)
{
	struct func_adapter *trigger = va_arg(ap, struct func_adapter *);
	struct func_adapter_ctx ctx;
	func_adapter_begin(trigger, &ctx);
	int rc = func_adapter_call(trigger, &ctx);
	func_adapter_end(trigger, &ctx);
	return rc;
}

int
on_shutdown_run_triggers(void)
{
	/* Save on_shutdown triggers - no new triggers will be added then. */
	RLIST_HEAD(triggers);
	rlist_splice(&triggers, &box_on_shutdown_trigger_list);

	double timeout = on_shutdown_trigger_timeout;
	struct event *event = box_on_shutdown_event;
	int rc = 0;
	struct trigger *trigger;
	struct region *region = &fiber()->gc;
	uint32_t region_svp = region_used(region);
	unsigned trigger_count = event->trigger_count;

	/* Calculating the total number of triggers. */
	rlist_foreach_entry(trigger, &triggers, link)
		trigger_count++;

	struct fiber **fibers =
		xregion_alloc_array(region, struct fiber *, trigger_count);

	bool expired = false;
	struct ev_timer timer;
	ev_timer_init(&timer, on_shutdown_run_triggers_timeout, timeout, 0);
	timer.data = &expired;
	/*
	 * We don't check if triggers are timed out during they launch
	 * since we want to give them all a chance to run. So in
	 * rlist_foreach_entry_safe loop, we run all the triggers,
	 * regardless of whether the timeout has expired or not.
	 */
	ev_timer_start(loop(), &timer);

	unsigned current_fiber = 0;
	char name[FIBER_NAME_INLINE];
	while (!rlist_empty(&triggers)) {
		/*
		 * Since the triggers will not be used later, we can
		 * pop them instead of iteration. It is safe against
		 * underlying trigger's removal.
		 */
		trigger = rlist_shift_entry(&triggers, struct trigger, link);
		snprintf(name, FIBER_NAME_INLINE,
			 "trigger_fiber%d", current_fiber);
		fibers[current_fiber] =
			fiber_new(name, on_shutdown_trigger_fiber_f);
		if (fibers[current_fiber] != NULL) {
			fiber_set_joinable(fibers[current_fiber], true);
			fiber_start(fibers[current_fiber], trigger);
			current_fiber++;
		} else {
			rc = -1;
			goto out;
		}
	}

	const char *trigger_name = NULL;
	struct func_adapter *func = NULL;
	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	/*
	 * Since new event triggers can appear, let's run the triggers
	 * until we've run out of fibers.
	 */
	while (event_trigger_iterator_next(&it, &func, &trigger_name) &&
	       current_fiber < trigger_count) {
		snprintf(name, FIBER_NAME_INLINE,
			 "trigger_fiber_%s", trigger_name);
		fibers[current_fiber] =
			fiber_new(name, on_shutdown_event_trigger_fiber_f);
		if (fibers[current_fiber] != NULL) {
			fiber_set_joinable(fibers[current_fiber], true);
			fiber_start(fibers[current_fiber], func);
			current_fiber++;
		} else {
			rc = -1;
			goto out;
		}
	}
	event_trigger_iterator_destroy(&it);

	/*
	 * Waiting for all triggers completion.
	 */
	for (unsigned int i = 0; i < current_fiber && !expired; i++) {
		if (fiber_join_timeout(fibers[i], timeout) != 0) {
			assert(!diag_is_empty(diag_get()));
			diag_log();
			diag_clear(diag_get());
		}
	}
	if (expired) {
		diag_set(TimedOut);
		rc = -1;
		goto out;
	}
out:
	ev_timer_stop(loop(), &timer);
	region_truncate(region, region_svp);
	return rc;
}
