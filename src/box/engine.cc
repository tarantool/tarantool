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
#include "port.h"
#include "space.h"
#include "exception.h"
#include "schema.h"
#include "small/rlist.h"
#include "scoped_guard.h"
#include "vclock.h"
#include <stdlib.h>
#include <string.h>
#include <errinj.h>

RLIST_HEAD(engines);

Engine::Engine(const char *engine_name, struct tuple_format_vtab *format_arg)
	:name(engine_name),
	 id(-1),
	 link(RLIST_HEAD_INITIALIZER(link)),
	 format(format_arg)
{}

void Engine::init()
{}

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

/** {{{ DML */

Handler::Handler(Engine *f)
	:engine(f)
{
}

void
Handler::applyInitialJoinRow(struct space *, struct request *)
{
	tnt_raise(ClientError, ER_UNSUPPORTED, engine->name,
		  "applySnapshotRow");
}

struct tuple *
Handler::executeReplace(struct txn *, struct space *,
                        struct request *)
{
	tnt_raise(ClientError, ER_UNSUPPORTED, engine->name, "replace");
}

struct tuple *
Handler::executeDelete(struct txn*, struct space *, struct request *)
{
	tnt_raise(ClientError, ER_UNSUPPORTED, engine->name, "delete");
}

struct tuple *
Handler::executeUpdate(struct txn*, struct space *, struct request *)
{
	tnt_raise(ClientError, ER_UNSUPPORTED, engine->name, "update");
}

void
Handler::executeUpsert(struct txn *, struct space *, struct request *)
{
	tnt_raise(ClientError, ER_UNSUPPORTED, engine->name, "upsert");
}

void
Handler::executeSelect(struct txn *, struct space *space,
		       uint32_t index_id, uint32_t iterator,
		       uint32_t offset, uint32_t limit,
		       const char *key, const char * /* key_end */,
		       struct port *port)
{
	Index *index = index_find_xc(space, index_id);

	uint32_t found = 0;
	if (iterator >= iterator_type_MAX)
		tnt_raise(IllegalParams, "Invalid iterator type");
	enum iterator_type type = (enum iterator_type) iterator;

	uint32_t part_count = key ? mp_decode_array(&key) : 0;
	if (key_validate(index->index_def, type, key, part_count))
		diag_raise();

	struct iterator *it = index->allocIterator();
	IteratorGuard guard(it);
	index->initIterator(it, type, key, part_count);

	struct tuple *tuple;
	while ((tuple = it->next(it)) != NULL) {
		if (offset > 0) {
			offset--;
			continue;
		}
		if (limit == found++)
			break;
		port_add_tuple_xc(port, tuple);
	}
}

/** }}} DML */

/* {{{ DDL  */

void
Handler::checkIndexDef(struct space *space, struct index_def *index_def)
{
	(void) space;
	(void) index_def;
	/*
	 * Don't bother checking index_def to match the view requirements.
	 * Index::initIterator() must check key on each call.
	 */
}

void
Handler::initSystemSpace(struct space * /* space */)
{
	panic("not implemented");
}

void
Handler::addPrimaryKey(struct space * /* space */)
{
}

void
Handler::dropPrimaryKey(struct space * /* space */)
{
}

void
Handler::buildSecondaryKey(struct space *, struct space *, Index *)
{
	tnt_raise(ClientError, ER_UNSUPPORTED, engine->name, "buildSecondaryKey");
}

void
Handler::prepareTruncateSpace(struct space *, struct space *)
{
}

void
Handler::commitTruncateSpace(struct space *, struct space *)
{
}

void
Handler::prepareAlterSpace(struct space *, struct space *)
{
}

void
Handler::commitAlterSpace(struct space *, struct space *)
{
}

/* }}} DDL */

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
