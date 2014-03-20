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
#include "tuple.h"
#include "engine.h"
#include "space.h"
#include "exception.h"
#include "salad/rlist.h"
#include <stdlib.h>
#include <string.h>

RLIST_HEAD(engines);

/** Register engine instance. */
void engine_register(struct engine *engine)
{
	rlist_add_entry(&engines, engine, link);
}

/** Find engine by name. */
struct engine*
engine_find(const char *name)
{
	struct engine *e;
	rlist_foreach_entry(e, &engines, link) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	tnt_raise(LoggedError, ER_NO_SUCH_ENGINE, name);
}

/** Init engine instance. */
void engine_init(struct engine *instance, const char *name)
{
	struct engine *origin = engine_find(name);
	*instance = *origin;
	instance->origin = origin;
	instance->link = RLIST_INITIALIZER(instance->link);
	instance->init(instance);
}

/** Shutdown all engines. */
void engine_shutdown()
{
	struct engine *e;
	rlist_foreach_entry(e, &engines, link)
		e->free(e);
}

/** Call a visitor function on every registered engine. */
void engine_foreach(void (*func)(struct engine *engine, void *udata), void *udata)
{
	struct engine *e;
	rlist_foreach_entry(e, &engines, link)
		func(e, udata);
}

/**
 * Derive recovery state from a parent engine
 * handler.
 */
void engine_derive(struct engine *engine)
{
	engine->state   = engine->origin->state;
	engine->recover = engine->origin->recover;
	engine->replace = engine->origin->replace;
}

static inline void
memtx_init(struct engine *engine __attribute((unused)))
{ }

static inline void
memtx_free(struct engine *engine __attribute((unused)))
{ }

/**
 * This is a vtab with which a newly created space which has no
 * keys is primed.
 * At first it is set to correctly work for spaces created during
 * recovery from snapshot. In process of recovery it is updated as
 * below:
 *
 * 1) after SNAP is loaded:
 *    recover = space_build_primary_key
 * 2) when all XLOGs are loaded:
 *    recover = space_build_all_keys
 */
struct engine engine_memtx = {
	.name    = "memtx",
	.origin  = NULL,
	.state   = READY_NO_KEYS,
	.recover = space_begin_build_primary_key,
	.replace = space_replace_no_keys,
	.init    = memtx_init,
	.free    = memtx_free,
	.link    = RLIST_INITIALIZER(engine_memtx.link)
};
