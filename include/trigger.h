#ifndef INCLUDES_TARANTOOL_TRIGGER_H
#define INCLUDES_TARANTOOL_TRIGGER_H
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
#include "rlist.h"
/**
 * Type of the callback which may be invoked
 * on an event.
 */
typedef void (*trigger_f)(struct trigger *trigger, void *event);

struct trigger
{
	struct rlist link;
	trigger_f run;
};

static inline void
trigger_run(struct rlist *list, void *event)
{
	struct trigger *trigger;
	rlist_foreach_entry(trigger, list, link)
		trigger->run(trigger, event);
}

static inline void
trigger_set(struct rlist *list, struct trigger *trigger)
{
	rlist_add_entry(list, trigger, link);
}

static inline void
trigger_clear(struct trigger *trigger)
{
	rlist_del_entry(trigger, link);
}

#endif /* INCLUDES_TARANTOOL_TRIGGER_H */
