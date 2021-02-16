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
#include "say.h"

#include <stdlib.h>
#include <small/rlist.h>
#include <errno.h>
#include <trigger.h>

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


