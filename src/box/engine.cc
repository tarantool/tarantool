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

engine::engine(const char *engine_name)
	:name(engine_name),
	 id(-1),
	 link(RLIST_HEAD_INITIALIZER(link))
{}

void engine::init()
{}

struct tuple_format *
engine::createFormat(struct key_def **, uint32_t,
		     struct field_def *, uint32_t, uint32_t)
{
	return NULL;
}

void engine::begin(struct txn *)
{}

void engine::beginStatement(struct txn *)
{}

void engine::prepare(struct txn *)
{}

void engine::commit(struct txn *)
{}

void engine::rollback(struct txn *)
{}

void engine::rollbackStatement(struct txn *, struct txn_stmt *)
{}

void engine::bootstrap()
{}

void engine::beginInitialRecovery(const struct vclock *)
{
}

void engine::beginFinalRecovery()
{}

void engine::endRecovery()
{}

int
engine::beginCheckpoint()
{
	return 0;
}

int
engine::waitCheckpoint(struct vclock *vclock)
{
	(void) vclock;
	return 0;
}

void
engine::commitCheckpoint(struct vclock *vclock)
{
	(void) vclock;
}

void
engine::abortCheckpoint()
{
}

int
engine::collectGarbage(int64_t lsn)
{
	(void) lsn;
	return 0;
}

int
engine::backup(struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	(void) vclock;
	(void) cb;
	(void) cb_arg;
	return 0;
}

void
engine::join(struct vclock *vclock, struct xstream *stream)
{
	(void) vclock;
	(void) stream;
}

void
engine::checkSpaceDef(struct space_def * /* def */)
{
}

/* {{{ Engine API */

/** Register engine instance. */
void engine_register(struct engine *engine)
{
	static int n_engines;
	rlist_add_tail_entry(&engines, engine, link);
	engine->id = n_engines++;
}

/** Find engine by name. */
struct engine *
engine_by_name(const char *name)
{
	struct engine *e;
	engine_foreach(e) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	return NULL;
}

/** Shutdown all engine factories. */
void engine_shutdown()
{
	struct engine *e, *tmp;
	rlist_foreach_entry_safe(e, &engines, link, tmp) {
		delete e;
	}
}

void
engine_bootstrap()
{
	struct engine *engine;
	engine_foreach(engine) {
		engine->bootstrap();
	}
}

void
engine_begin_initial_recovery(const struct vclock *recovery_vclock)
{
	struct engine *engine;
	engine_foreach(engine) {
		engine->beginInitialRecovery(recovery_vclock);
	}
}

void
engine_begin_final_recovery()
{
	struct engine *engine;
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
	struct engine *engine;
	engine_foreach(engine)
		engine->endRecovery();
}

int
engine_begin_checkpoint()
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->beginCheckpoint() < 0)
			return -1;
	}
	return 0;
}

int
engine_commit_checkpoint(struct vclock *vclock)
{
	struct engine *engine;
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
	struct engine *engine;
	engine_foreach(engine)
		engine->abortCheckpoint();
}

int
engine_collect_garbage(int64_t lsn)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->collectGarbage(lsn) < 0)
			return -1;
	}
	return 0;
}

int
engine_backup(struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->backup(vclock, cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}

void
engine_join(struct vclock *vclock, struct xstream *stream)
{
	struct engine *engine;
	engine_foreach(engine) {
		engine->join(vclock, stream);
	}
}

/* }}} Engine API */
