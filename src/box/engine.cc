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
#include "evio.h"
#include "replication.h"
#include "engine.h"
#include "space.h"
#include "exception.h"
#include "schema.h"
#include "salad/rlist.h"
#include <stdlib.h>
#include <string.h>

RLIST_HEAD(engines);

uint32_t engine_flags[BOX_ENGINE_MAX];
int n_engines;

Engine::Engine(const char *engine_name)
	:name(engine_name),
	 link(RLIST_INITIALIZER(link))
{}

void Engine::init()
{}

void Engine::begin(struct txn*, struct space*)
{}

void Engine::commit(struct txn*)
{}

void Engine::rollback(struct txn*)
{}

Handler::Handler(Engine *f)
	:engine(f)
{
	/* derive recovery state from engine */
	initRecovery();
}

/** Register engine instance. */
void engine_register(Engine *engine)
{
	rlist_add_tail_entry(&engines, engine, link);
	engine->id = ++n_engines;
	engine_flags[engine->id] = engine->flags;
}

/** Find engine by name. */
Engine *
engine_find(const char *name)
{
	Engine *e;
	engine_foreach(e) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	tnt_raise(LoggedError, ER_NO_SUCH_ENGINE, name);
}

/** Shutdown all engine factories. */
void engine_shutdown()
{
	Engine *e, *tmp;
	rlist_foreach_entry_safe(e, &engines, link, tmp) {
		delete e;
	}
}

void
engine_begin_recover_snapshot(int64_t snapshot_lsn)
{
	/* recover engine snapshot */
	Engine *engine;
	engine_foreach(engine) {
		engine->begin_recover_snapshot(snapshot_lsn);
	}
}

void
engine_begin_join()
{
	/* recover engine snapshot */
	Engine *engine;
	engine_foreach(engine) {
		engine->begin_join();
	}
}

static void
do_one_recover_step(struct space *space, void * /* param */)
{
	if (space_index(space, 0)) {
		space->handler->recover(space);
	} else {
		/* in case of space has no primary index,
		 * derive it's engine handler recovery state from
		 * the global one. */
		space->handler->initRecovery();
	}
}

void
engine_end_recover_snapshot()
{
	/*
	 * For all new spaces created from now on, when the
	 * PRIMARY key is added, enable it right away.
	 */
	Engine *engine;
	engine_foreach(engine) {
		engine->end_recover_snapshot();
	}
	space_foreach(do_one_recover_step, NULL);
}

void
engine_end_recovery()
{
	/*
	 * For all new spaces created after recovery is complete,
	 * when the primary key is added, enable all keys.
	 */
	Engine *engine;
	engine_foreach(engine)
		engine->end_recovery();

	space_foreach(do_one_recover_step, NULL);
}

int
engine_checkpoint(int64_t checkpoint_id)
{
	static bool snapshot_is_in_progress = false;
	if (snapshot_is_in_progress)
		return EINPROGRESS;

	snapshot_is_in_progress = true;

	/* create engine snapshot */
	Engine *engine;
	engine_foreach(engine) {
		if (engine->begin_checkpoint(checkpoint_id))
			goto error;
	}

	/* wait for engine snapshot completion */
	engine_foreach(engine) {
		if (engine->wait_checkpoint())
			goto error;
	}

	/* remove previous snapshot reference */
	engine_foreach(engine) {
		engine->commit_checkpoint();
	}
	snapshot_is_in_progress = false;
	return 0;
error:
	int save_errno = errno;
	/* rollback snapshot creation */
	engine_foreach(engine)
		engine->abort_checkpoint();
	snapshot_is_in_progress = false;
	return save_errno;
}

void
engine_join(Relay *relay)
{
	Engine *engine;
	engine_foreach(engine) {
		engine->join(relay);
	}
}
