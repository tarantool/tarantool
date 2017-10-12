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
#include "vinyl_engine.h"
#include "xrow.h"
#include "txn.h"
#include "vinyl.h"
#include "tuple.h"
#include "iproto_constants.h"
#include "scoped_guard.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void
vinyl_space_destroy(struct space *space)
{
	free(space);
}

static size_t
vinyl_space_bsize(struct space *)
{
	return 0;
}

/* {{{ DML */

static void
vinyl_space_apply_initial_join_row(struct space *space, struct request *request)
{
	assert(request->header != NULL);
	struct vy_env *env = ((VinylEngine *)space->engine)->env;

	struct vy_tx *tx = vy_begin(env);
	if (tx == NULL)
		diag_raise();

	int64_t signature = request->header->lsn;

	struct txn_stmt stmt;
	memset(&stmt, 0, sizeof(stmt));

	int rc;
	switch (request->type) {
	case IPROTO_REPLACE:
		rc = vy_replace(env, tx, &stmt, space, request);
		break;
	case IPROTO_UPSERT:
		rc = vy_upsert(env, tx, &stmt, space, request);
		break;
	case IPROTO_DELETE:
		rc = vy_delete(env, tx, &stmt, space, request);
		break;
	default:
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			  (uint32_t) request->type);
	}
	if (rc != 0)
		diag_raise();

	if (stmt.old_tuple)
		tuple_unref(stmt.old_tuple);
	if (stmt.new_tuple)
		tuple_unref(stmt.new_tuple);

	if (vy_prepare(env, tx)) {
		vy_rollback(env, tx);
		diag_raise();
	}
	vy_commit(env, tx, signature);
}

/*
 * Four cases:
 *  - insert in one index
 *  - insert in multiple indexes
 *  - replace in one index
 *  - replace in multiple indexes.
 */
static struct tuple *
vinyl_space_execute_replace(struct space *space, struct txn *txn,
			    struct request *request)
{
	assert(request->index_id == 0);
	VinylEngine *engine = (VinylEngine *) space->engine;
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);

	if (vy_replace(engine->env, tx, stmt, space, request))
		diag_raise();
	return stmt->new_tuple;
}

static struct tuple *
vinyl_space_execute_delete(struct space *space, struct txn *txn,
			   struct request *request)
{
	VinylEngine *engine = (VinylEngine *) space->engine;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	if (vy_delete(engine->env, tx, stmt, space, request))
		diag_raise();
	/*
	 * Delete may or may not set stmt->old_tuple, but we
	 * always return NULL.
	 */
	return NULL;
}

static struct tuple *
vinyl_space_execute_update(struct space *space, struct txn *txn,
			   struct request *request)
{
	VinylEngine *engine = (VinylEngine *) space->engine;
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_update(engine->env, tx, stmt, space, request) != 0)
		diag_raise();
	return stmt->new_tuple;
}

static void
vinyl_space_execute_upsert(struct space *space, struct txn *txn,
                           struct request *request)
{
	VinylEngine *engine = (VinylEngine *) space->engine;
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_upsert(engine->env, tx, stmt, space, request) != 0)
		diag_raise();
}

/* }}} DML */

/* {{{ DDL */

static void
vinyl_init_system_space(struct space *)
{
	unreachable();
}

void
vinyl_space_check_format(struct space *new_space, struct space *old_space)
{
	VinylEngine *engine = (VinylEngine *) new_space->engine;
	if (vy_check_format(engine->env, old_space) != 0)
		diag_raise();
}

static void
vinyl_space_check_index_def(struct space *space, struct index_def *index_def)
{
	if (index_def->type != TREE) {
		tnt_raise(ClientError, ER_INDEX_TYPE,
		          index_def->name,
			  space_name(space));
	}
	if (index_def->key_def->is_nullable && index_def->iid == 0)
		tnt_raise(ClientError, ER_NULLABLE_PRIMARY, space_name(space));
	/* Check that there are no ANY, ARRAY, MAP parts */
	for (uint32_t i = 0; i < index_def->key_def->part_count; i++) {
		struct key_part *part = &index_def->key_def->parts[i];
		if (part->type <= FIELD_TYPE_ANY ||
		    part->type >= FIELD_TYPE_ARRAY) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  tt_sprintf("field type '%s' is not supported",
					     field_type_strs[part->type]));
		}
	}
	if (key_def_has_collation(index_def->key_def)) {
		tnt_raise(ClientError, ER_MODIFY_INDEX, index_def->name,
			  space_name(space), "vinyl does not support collation");
	}
}

static struct index *
vinyl_space_create_index(struct space *space, struct index_def *index_def)
{
	VinylEngine *engine = (VinylEngine *) space->engine;
	if (index_def->type != TREE) {
		unreachable();
		return NULL;
	}
	struct vy_index *pk = NULL;
	if (index_def->iid > 0) {
		pk = vy_index(space_index(space, 0));
		assert(pk != NULL);
	}
	return new VinylIndex(engine->env, index_def, space->format, pk);
}

static void
vinyl_space_add_primary_key(struct space *space)
{
	VinylIndex *pk = (VinylIndex *) index_find_xc(space, 0);
	pk->open();
}

static void
vinyl_space_drop_primary_key(struct space *)
{
}

static void
vinyl_space_build_secondary_key(struct space *old_space,
				struct space *new_space,
				struct index *new_index)
{
	(void)old_space;
	(void)new_space;
	((VinylIndex *)new_index)->open();
	/*
	 * Unlike Memtx, Vinyl does not need building of a secondary index.
	 * This is true because of two things:
	 * 1) Vinyl does not support alter of non-empty spaces
	 * 2) During recovery a Vinyl index already has all needed data on disk.
	 * And there are 3 cases:
	 * I. The secondary index is added in snapshot. Then Vinyl was
	 * snapshotted too and all necessary for that moment data is on disk.
	 * II. The secondary index is added in WAL. That means that vinyl
	 * space had no data at that point and had nothing to build. The
	 * index actually could contain recovered data, but it will handle it
	 * by itself during WAL recovery.
	 * III. Vinyl is online. The space is definitely empty and there's
	 * nothing to build.
	 *
	 * When we start to implement alter of non-empty vinyl spaces, it
	 *  seems that we should call here:
	 *   Engine::buildSecondaryKey(old_space, new_space, new_index_arg);
	 *  but aware of three cases mentioned above.
	 */
}

static void
vinyl_space_prepare_truncate(struct space *old_space,
			     struct space *new_space)
{
	VinylEngine *engine = (VinylEngine *) old_space->engine;
	if (vy_prepare_truncate_space(engine->env, old_space, new_space) != 0)
		diag_raise();
}

static void
vinyl_space_commit_truncate(struct space *old_space,
			    struct space *new_space)
{
	VinylEngine *engine = (VinylEngine *) old_space->engine;
	vy_commit_truncate_space(engine->env, old_space, new_space);
}

static void
vinyl_space_prepare_alter(struct space *old_space, struct space *new_space)
{
	VinylEngine *engine = (VinylEngine *) old_space->engine;
	if (vy_prepare_alter_space(engine->env, old_space, new_space) != 0)
		diag_raise();
}

static void
vinyl_space_commit_alter(struct space *old_space, struct space *new_space)
{
	(void) old_space;
	VinylEngine *engine = (VinylEngine *) old_space->engine;
	if (new_space == NULL || new_space->index_count == 0) {
		/* This is a drop space. */
		return;
	}
	if (vy_commit_alter_space(engine->env, new_space,
				  new_space->format) != 0)
		diag_raise();
}

/* }}} DDL */

const struct space_vtab vinyl_space_vtab = {
	/* .destroy = */ vinyl_space_destroy,
	/* .bsize = */ vinyl_space_bsize,
	/* .apply_initial_join_row = */ vinyl_space_apply_initial_join_row,
	/* .execute_replace = */ vinyl_space_execute_replace,
	/* .execute_delete = */ vinyl_space_execute_delete,
	/* .execute_update = */ vinyl_space_execute_update,
	/* .execute_upsert = */ vinyl_space_execute_upsert,
	/* .execute_select = */ generic_space_execute_select,
	/* .init_system_space = */ vinyl_init_system_space,
	/* .check_index_def = */ vinyl_space_check_index_def,
	/* .create_index = */ vinyl_space_create_index,
	/* .add_primary_key = */ vinyl_space_add_primary_key,
	/* .drop_primary_key = */ vinyl_space_drop_primary_key,
	/* .check_format = */ vinyl_space_check_format,
	/* .build_secondary_key = */ vinyl_space_build_secondary_key,
	/* .prepare_truncate = */ vinyl_space_prepare_truncate,
	/* .commit_truncate = */ vinyl_space_commit_truncate,
	/* .prepare_alter = */ vinyl_space_prepare_alter,
	/* .commit_alter = */ vinyl_space_commit_alter,
};
