/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tuple.h"
#include "txn.h"
#include "port.h"
#include "request.h"
#include "engine.h"
#include "space.h"
#include "exception.h"
#include "schema.h"
#include "small/rlist.h"
#include "scoped_guard.h"
#include <stdlib.h>
#include <string.h>
#include <latch.h>
#include <errinj.h>

RLIST_HEAD(engines);

extern bool snapshot_in_progress;
extern struct latch schema_lock;

Engine::Engine(const char *engine_name)
	:name(engine_name),
	 link(RLIST_HEAD_INITIALIZER(link))
{}

void Engine::init()
{}

void Engine::begin(struct txn *)
{}

void Engine::prepare(struct txn *)
{}

void Engine::commit(struct txn *, int64_t)
{}

void Engine::rollback(struct txn *)
{}

void Engine::rollbackStatement(struct txn_stmt *)
{}

void Engine::initSystemSpace(struct space * /* space */)
{
	panic("not implemented");
}

void
Engine::addPrimaryKey(struct space * /* space */)
{
}

void
Engine::dropPrimaryKey(struct space * /* space */)
{
}

bool Engine::needToBuildSecondaryKey(struct space * /* space */)
{
	return true;
}

void
Engine::beginJoin()
{
}

int
Engine::beginCheckpoint(int64_t lsn)
{
	(void) lsn;
	return 0;
}

int
Engine::waitCheckpoint()
{
	return 0;
}

void
Engine::commitCheckpoint()
{
}

void
Engine::abortCheckpoint()
{
}

void
Engine::endRecovery()
{
}

void
Engine::recoverToCheckpoint(int64_t /* lsn */)
{
}

void
Engine::join(struct relay *relay)
{
	(void) relay;
}

void
Engine::dropIndex(Index *index)
{
	(void) index;
}

void
Engine::keydefCheck(struct space *space, struct key_def *key_def)
{
	(void) space;
	(void) key_def;
	/*
	 * Don't bother checking key_def to match the view requirements.
	 * Index::initIterator() must check key on each call.
	 */
}

Handler::Handler(Engine *f)
	:engine(f)
{
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
Handler::onAlter(Handler *)
{
}

void
Handler::executeSelect(struct txn *, struct space *space,
		       uint32_t index_id, uint32_t iterator,
		       uint32_t offset, uint32_t limit,
		       const char *key, const char * /* key_end */,
		       struct port *port)
{
	Index *index = index_find(space, index_id);

	uint32_t found = 0;
	if (iterator >= iterator_type_MAX)
		tnt_raise(IllegalParams, "Invalid iterator type");
	enum iterator_type type = (enum iterator_type) iterator;

	uint32_t part_count = key ? mp_decode_array(&key) : 0;
	key_validate(index->key_def, type, key, part_count);

	struct iterator *it = index->allocIterator();
	IteratorGuard guard(it);
	index->initIterator(it, type, key, part_count);

	struct tuple *tuple;
	while ((tuple = it->next(it)) != NULL) {
		/*
		 * This is for Sophia, which returns a tuple
		 * with zero refs from the iterator, expecting
		 * the caller to GC it after use.
		 */
		TupleRef tuple_gc(tuple);
		if (offset > 0) {
			offset--;
			continue;
		}
		if (limit == found++)
			break;
		port_add_tuple(port, tuple);
	}
}

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
engine_recover_to_checkpoint(int64_t checkpoint_id)
{
	/* recover engine snapshot */
	Engine *engine;
	engine_foreach(engine) {
		engine->recoverToCheckpoint(checkpoint_id);
	}
}

void
engine_begin_join()
{
	/* recover engine snapshot */
	Engine *engine;
	engine_foreach(engine) {
		engine->beginJoin();
	}
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
engine_checkpoint(int64_t checkpoint_id)
{
	if (snapshot_in_progress)
		return EINPROGRESS;

	snapshot_in_progress = true;
	latch_lock(&schema_lock);

	/* create engine snapshot */
	Engine *engine;
	engine_foreach(engine) {
		if (engine->beginCheckpoint(checkpoint_id))
			goto error;
	}

	/* wait for engine snapshot completion */
	engine_foreach(engine) {
		if (engine->waitCheckpoint())
			goto error;
	}

	/* remove previous snapshot reference */
	engine_foreach(engine) {
		engine->commitCheckpoint();
	}
	latch_unlock(&schema_lock);
	snapshot_in_progress = false;
	return 0;
error:
	int save_errno = errno;
	/* rollback snapshot creation */
	engine_foreach(engine)
		engine->abortCheckpoint();
	latch_unlock(&schema_lock);
	snapshot_in_progress = false;
	return save_errno;
}

void
engine_join(struct relay *relay)
{
	Engine *engine;
	engine_foreach(engine) {
		engine->join(relay);
	}
}
