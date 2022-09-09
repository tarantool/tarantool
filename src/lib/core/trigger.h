#ifndef TARANTOOL_LIB_CORE_TRIGGER_H_INCLUDED
#define TARANTOOL_LIB_CORE_TRIGGER_H_INCLUDED
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
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */
/**
 * Type of the callback which may be invoked
 * on an event.
 */
struct trigger;
typedef int (*trigger_f)(struct trigger *trigger, void *event);
typedef void (*trigger_f0)(struct trigger *trigger);

struct trigger
{
	struct rlist link;
	trigger_f run;
	/**
	 * Lua ref in case the trigger is used in Lua,
	 * or other trigger context.
	 */
	void *data;
	/**
	 * Cleanup function, called when the trigger is removed
	 * or the object containing the trigger is destroyed.
	 */
	trigger_f0 destroy;
};

static inline void
trigger_create(struct trigger *trigger, trigger_f run, void *data,
	       trigger_f0 destroy)
{
	rlist_create(&trigger->link);
	trigger->run = run;
	trigger->data = data;
	trigger->destroy = destroy;
}

static inline void
trigger_add(struct rlist *list, struct trigger *trigger)
{
	/*
	 * New triggers are pushed to the beginning of the list.
	 * This ensures that they are not fired right away if
	 * pushed from within a trigger. This also ensures that
	 * the trigger which was set first is fired last.
	 * Alter space code depends on this order.
	 * @todo in future, allow triggers to be pushed
	 * to an arbitrary position on the list.
	 */
	rlist_add_entry(list, trigger, link);
}

static inline void
trigger_add_unique(struct rlist *list, struct trigger *trigger)
{
	struct trigger *trg;
	rlist_foreach_entry(trg, list, link) {
		if (trg->data == trigger->data && trg->run == trigger->run)
			return;
	}
	trigger_add(list, trigger);
}

static inline void
trigger_clear(struct trigger *trigger)
{
	rlist_del_entry(trigger, link);
}


static inline void
trigger_destroy(struct rlist *list)
{
	struct trigger *trigger, *tmp;
	rlist_foreach_entry_safe(trigger, list, link, tmp) {
		trigger_clear(trigger);
		if (trigger->destroy)
			trigger->destroy(trigger);
	}
}

/**
 * Run registered triggers. Stop and return an error if
 * a trigger fails.
 *
 * Note, since triggers are added to the list head, this
 * function first runs triggers that were added last
 */
int
trigger_run(struct rlist *list, void *event);

/**
 * Same as trigger_run(), but runs triggers in the reverse
 * order, i.e. it runs triggers in the same order they were
 * added.
 */
int
trigger_run_reverse(struct rlist *list, void *event);

/**
 * Runs registered triggers in fibers.
 * Note, since triggers are added to the list head, this
 * function first runs triggers that were added last. Waits
 * for their completion for a specified period of time.
 * If timeout has expired and function immediately stops,
 * without waiting for other triggers completion.
 */
int
trigger_fiber_run(struct rlist *list, void *event, double timeout);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline void
trigger_run_xc(struct rlist *list, void *event)
{
	if (trigger_run(list, event) != 0)
		diag_raise();
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_TRIGGER_H_INCLUDED */
