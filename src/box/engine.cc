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
#include "engine.h"

#include "tuple.h"
#include "txn.h"
#include "space.h"
#include "exception.h"
#include "small/rlist.h"
#include "vclock.h"
#include <stdlib.h>
#include <string.h>
#include <errinj.h>

RLIST_HEAD(engines);

Engine::Engine(const char *engine_name)
	:name(engine_name),
	 id(-1),
	 link(RLIST_HEAD_INITIALIZER(link))
{}

void Engine::init()
{}

struct tuple_format *
Engine::createFormat(struct key_def **, uint32_t,
		     struct field_def *, uint32_t, uint32_t)
{
	return NULL;
}

void Engine::begin(struct txn *)
{}

void Engine::beginStatement(struct txn *)
{}

void Engine::prepare(struct txn *)
{}

void Engine::commit(struct txn *)
{}

void Engine::rollback(struct txn *)
{}

void Engine::rollbackStatement(struct txn *, struct txn_stmt *)
{}

void Engine::bootstrap()
{}

void Engine::beginInitialRecovery(const struct vclock *)
{
}

void Engine::beginFinalRecovery()
{}

void Engine::endRecovery()
{}

int
Engine::beginCheckpoint()
{
	return 0;
}

int
Engine::waitCheckpoint(struct vclock *vclock)
{
	(void) vclock;
	return 0;
}

void
Engine::commitCheckpoint(struct vclock *vclock)
{
	(void) vclock;
}

void
Engine::abortCheckpoint()
{
}

int
Engine::collectGarbage(int64_t lsn)
{
	(void) lsn;
	return 0;
}

int
Engine::backup(struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	(void) vclock;
	(void) cb;
	(void) cb_arg;
	return 0;
}

void
Engine::join(struct vclock *vclock, struct xstream *stream)
{
	(void) vclock;
	(void) stream;
}

void
Engine::checkSpaceDef(struct space_def * /* def */)
{
}

/* {{{ Engine API */

/** Register engine instance. */
void engine_register(Engine *engine)
{
	static int n_engines;
	rlist_add_tail_entry(&engines, engine, link);
	engine->id = n_engines++;
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
engine_bootstrap()
{
	Engine *engine;
	engine_foreach(engine) {
		engine->bootstrap();
	}
}

void
engine_begin_initial_recovery(const struct vclock *recovery_vclock)
{
	Engine *engine;
	engine_foreach(engine) {
		engine->beginInitialRecovery(recovery_vclock);
	}
}

void
engine_begin_final_recovery()
{
	Engine *engine;
	engine_foreach(engine)
		engine->beginFinalRecovery();
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
		engine->endRecovery();
}

int
engine_begin_checkpoint()
{
	Engine *engine;
	engine_foreach(engine) {
		if (engine->beginCheckpoint() < 0)
			return -1;
	}
	return 0;
}

int
engine_commit_checkpoint(struct vclock *vclock)
{
	Engine *engine;
	engine_foreach(engine) {
		if (engine->waitCheckpoint(vclock) < 0)
			return -1;
	}
	engine_foreach(engine) {
		engine->commitCheckpoint(vclock);
	}
	return 0;
}

void
engine_abort_checkpoint()
{
	Engine *engine;
	engine_foreach(engine)
		engine->abortCheckpoint();
}

int
engine_collect_garbage(int64_t lsn)
{
	Engine *engine;
	engine_foreach(engine) {
		if (engine->collectGarbage(lsn) < 0)
			return -1;
	}
	return 0;
}

int
engine_backup(struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	Engine *engine;
	engine_foreach(engine) {
		if (engine->backup(vclock, cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}

void
engine_join(struct vclock *vclock, struct xstream *stream)
{
	Engine *engine;
	engine_foreach(engine) {
		engine->join(vclock, stream);
	}
}

/* }}} Engine API */
