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
#include "vinyl_space.h"
#include "vinyl_index.h"
#include "xrow.h"
#include "txn.h"
#include "vinyl.h"
#include "vy_stmt.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

VinylSpace::VinylSpace(Engine *e)
	:Handler(e)
{}

void
VinylSpace::applyInitialJoinRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	assert(request->header != NULL);
	struct vy_env *env = ((VinylEngine *)space->handler->engine)->env;

	/* Check the tuple fields. */
	if (tuple_validate_raw(space->format, request->tuple))
		diag_raise();

	struct vy_tx *tx = vy_begin(env);
	if (tx == NULL)
		diag_raise();

	int64_t signature = request->header->lsn;

	if (vy_replace(tx, NULL, space, request))
		diag_raise();

	if (vy_prepare(env, tx)) {
		vy_rollback(env, tx);
		diag_raise();
	}
	if (vy_commit(env, tx, signature))
		panic("failed to commit vinyl transaction");
}

/*
 * Four cases:
 *  - insert in one index
 *  - insert in multiple indexes
 *  - replace in one index
 *  - replace in multiple indexes.
 */
struct tuple *
VinylSpace::executeReplace(struct txn *txn, struct space *space,
			   struct request *request)
{
	assert(request->index_id == 0);
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);

	if (vy_replace(tx, stmt, space, request))
		diag_raise();

	assert(stmt->new_tuple != NULL);
	return stmt->new_tuple;
}

struct tuple *
VinylSpace::executeDelete(struct txn *txn, struct space *space,
                          struct request *request)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	if (vy_delete(tx, stmt, space, request))
		diag_raise();
	/*
	 * Delete may or may not set stmt->old_tuple, but we
	 * always return NULL.
	 */
	return NULL;
}

struct tuple *
VinylSpace::executeUpdate(struct txn *txn, struct space *space,
                          struct request *request)
{
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_update(tx, stmt, space, request) != 0)
		diag_raise();
	return stmt->new_tuple;
}

void
VinylSpace::executeUpsert(struct txn *txn, struct space *space,
                           struct request *request)
{
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_upsert(tx, stmt, space, request) != 0)
		diag_raise();
}

Index *
VinylSpace::createIndex(struct space *space, struct key_def *key_def)
{
	(void) space;
	VinylEngine *engine = (VinylEngine *) this->engine;
	if (key_def->type != TREE) {
		unreachable();
		return NULL;
	}
	return new VinylIndex(engine->env, key_def);
}

void
VinylSpace::dropIndex(Index *index)
{
	VinylIndex *i = (VinylIndex *)index;
	/* schedule asynchronous drop */
	vy_index_drop(i->db);
	i->db  = NULL;
	i->env = NULL;
}

void
VinylSpace::prepareAlterSpace(struct space *old_space, struct space *new_space)
{
	if (old_space->index_count &&
	    old_space->index_count <= new_space->index_count) {
		VinylEngine *engine = (VinylEngine *)old_space->handler->engine;
		Index *primary_index = index_find_xc(old_space, 0);
		if (vy_status(engine->env) == VINYL_ONLINE &&
		    primary_index->min(NULL, 0)) {
			/**
			 * If the space is not empty, then forbid new
			 * index create.
			 */
			tnt_raise(ClientError, ER_UNSUPPORTED, "Vinyl",
				  "altering not empty space");
		}
	}
}

void
VinylSpace::commitAlterSpace(struct space *old_space, struct space *new_space)
{
	if (new_space == NULL || new_space->index_count == 0) {
		/* This is a drop space. */
		return;
	}
	if (vy_commit_alter_space(old_space, new_space) != 0)
		diag_raise();
}
