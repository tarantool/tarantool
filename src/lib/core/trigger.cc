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
#include <small/mempool.h>

/**
 * A single link in a list of triggers scheduled for execution. Can't be part of
 * struct trigger, because one trigger might be part of an unlimited number of
 * run lists.
 */
struct run_link {
	/** A link in the run list belonging to one trigger_run() call.  */
	struct rlist in_run;
	/**
	 * A link in a list of all links referencing that trigger. Used to
	 * remove the trigger from whatever list it's on during trigger_clear().
	 */
	struct rlist in_trigger;
	/** The trigger to be executed. */
	struct trigger *trigger;
};

/** A memory pool for trigger run link allocation. */
static __thread struct mempool run_link_pool;

/** Add the trigger to the run list. */
static int
run_list_put_trigger(struct rlist *list, struct trigger *trigger)
{
	struct run_link *run_link =
		(struct run_link *)mempool_alloc(&run_link_pool);
	if (run_link == NULL) {
		diag_set(OutOfMemory, sizeof(struct run_link),
			 "mempool_alloc", "trigger run link");
		return -1;
	}
	run_link->trigger = trigger;
	rlist_add_tail_entry(list, run_link, in_run);
	rlist_add_tail_entry(&trigger->run_links, run_link, in_trigger);
	return 0;
}

/** Take the next trigger from the run list and free the corresponding link. */
static struct trigger *
run_list_take_trigger(struct rlist *list)
{
	struct run_link *link = rlist_shift_entry(list, struct run_link,
						  in_run);
	struct trigger *trigger = link->trigger;
	rlist_del_entry(link, in_trigger);
	mempool_free(&run_link_pool, link);
	return trigger;
}

/** Empty the run list and free all the links allocated for it. */
static void
run_list_clear(struct rlist *list)
{
	while (!rlist_empty(list))
		run_list_take_trigger(list);
}

/** Execute the triggers in an order specified by \a list. */
static int
trigger_run_list(struct rlist *list, void *event)
{
	while (!rlist_empty(list)) {
		struct trigger *trigger = run_list_take_trigger(list);
		if (trigger->run(trigger, event) != 0) {
			run_list_clear(list);
			return -1;
		}
	}
	return 0;
}

int
trigger_run(struct rlist *list, void *event)
{
	/*
	 * A list holding all triggers scheduled for execution. It's important
	 * to save all triggers in a separate run_list and iterate over it
	 * instead of the passed list, because the latter might change anyhow
	 * while the triggers are run:
	 *  * the current element might be removed from the list
	 *    (rlist_foreach_entry_safe helps with that)
	 *  * the next element might be removed from the list
	 *    (rlist_foreach_entry_safe fails to handle that, but plain
	 *    rlist_foreach_entry works just fine)
	 *  * trigger lists might be swapped (for example see
	 *    space_swap_triggers() in alter.cc), in which case neither
	 *    rlist_foreach_entry nor rlist_foreach_entry_safe can help
	 */
	RLIST_HEAD(run_list);
	struct trigger *trigger;
	rlist_foreach_entry(trigger, list, link) {
		if (run_list_put_trigger(&run_list, trigger) != 0) {
			run_list_clear(&run_list);
			return -1;
		}
	}
	return trigger_run_list(&run_list, event);
}

int
trigger_run_reverse(struct rlist *list, void *event)
{
	RLIST_HEAD(run_list);
	struct trigger *trigger;
	rlist_foreach_entry_reverse(trigger, list, link) {
		if (run_list_put_trigger(&run_list, trigger) != 0) {
			run_list_clear(&run_list);
			return -1;
		}
	}
	return trigger_run_list(&run_list, event);
}

void
trigger_clear(struct trigger *trigger)
{
	rlist_del_entry(trigger, link);
	struct run_link *link, *tmp;
	rlist_foreach_entry_safe(link, &trigger->run_links, in_trigger, tmp) {
		rlist_del_entry(link, in_run);
		rlist_del_entry(link, in_trigger);
		mempool_free(&run_link_pool, link);
	}
}

void
trigger_init_in_thread(void)
{
	mempool_create(&run_link_pool, &cord()->slabc, sizeof(struct run_link));
}

void
trigger_free_in_thread(void)
{
	mempool_destroy(&run_link_pool);
}
