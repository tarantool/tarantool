/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "blackhole.h"

#include <small/rlist.h>

#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "engine.h"
#include "space.h"
#include "txn.h"
#include "tuple.h"
#include "xrow.h"

static void
blackhole_space_destroy(struct space *space)
{
	free(space);
}

static int
blackhole_space_execute_replace(struct space *space, struct txn *txn,
				struct request *request, struct tuple **result)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	stmt->new_tuple = tuple_new(space->format, request->tuple,
				    request->tuple_end);
	if (stmt->new_tuple == NULL)
		return -1;
	tuple_ref(stmt->new_tuple);
	*result = stmt->new_tuple;
	return 0;
}

static int
blackhole_space_execute_delete(struct space *space, struct txn *txn,
			       struct request *request, struct tuple **result)
{
	(void)space;
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_UNSUPPORTED, "Blackhole", "delete()");
	return -1;
}

static int
blackhole_space_execute_update(struct space *space, struct txn *txn,
			       struct request *request, struct tuple **result)
{
	(void)space;
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_UNSUPPORTED, "Blackhole", "update()");
	return -1;
}

static int
blackhole_space_execute_upsert(struct space *space, struct txn *txn,
			       struct request *request)
{
	(void)space;
	(void)txn;
	(void)request;
	diag_set(ClientError, ER_UNSUPPORTED, "Blackhole", "upsert()");
	return -1;
}

static struct index *
blackhole_space_create_index(struct space *space, struct index_def *def)
{
	(void)space;
	(void)def;
	/* See blackhole_engine_create_space(). */
	unreachable();
	return NULL;
}

static const struct space_vtab blackhole_space_vtab = {
	/* .destroy = */ blackhole_space_destroy,
	/* .bsize = */ generic_space_bsize,
	/* .execute_replace = */ blackhole_space_execute_replace,
	/* .execute_delete = */ blackhole_space_execute_delete,
	/* .execute_update = */ blackhole_space_execute_update,
	/* .execute_upsert = */ blackhole_space_execute_upsert,
	/* .ephemeral_replace = */ generic_space_ephemeral_replace,
	/* .ephemeral_delete = */ generic_space_ephemeral_delete,
	/* .ephemeral_rowid_next = */ generic_space_ephemeral_rowid_next,
	/* .init_system_space = */ generic_init_system_space,
	/* .init_ephemeral_space = */ generic_init_ephemeral_space,
	/* .check_index_def = */ generic_space_check_index_def,
	/* .create_index = */ blackhole_space_create_index,
	/* .add_primary_key = */ generic_space_add_primary_key,
	/* .drop_primary_key = */ generic_space_drop_primary_key,
	/* .check_format = */ generic_space_check_format,
	/* .build_index = */ generic_space_build_index,
	/* .swap_index = */ generic_space_swap_index,
	/* .prepare_alter = */ generic_space_prepare_alter,
	/* .invalidate = */ generic_space_invalidate,
};

static void
blackhole_engine_shutdown(struct engine *engine)
{
	free(engine);
}

static struct space *
blackhole_engine_create_space(struct engine *engine, struct space_def *def,
			      struct rlist *key_list)
{
	if (!rlist_empty(key_list)) {
		diag_set(ClientError, ER_UNSUPPORTED, "Blackhole", "indexes");
		return NULL;
	}

	struct space *space = (struct space *)calloc(1, sizeof(*space));
	if (space == NULL) {
		diag_set(OutOfMemory, sizeof(*space),
			 "malloc", "struct space");
		return NULL;
	}

	/* Allocate tuples on runtime arena, but check space format. */
	struct tuple_format *format;
	format = tuple_format_new(&tuple_format_runtime->vtab, NULL, NULL, 0,
				  def->fields, def->field_count,
				  def->exact_field_count, def->dict, false,
				  false);
	if (format == NULL) {
		free(space);
		return NULL;
	}
	tuple_format_ref(format);

	if (space_create(space, engine, &blackhole_space_vtab,
			 def, key_list, format) != 0) {
		tuple_format_unref(format);
		free(space);
		return NULL;
	}
	return space;
}

static const struct engine_vtab blackhole_engine_vtab = {
	/* .shutdown = */ blackhole_engine_shutdown,
	/* .create_space = */ blackhole_engine_create_space,
	/* .prepare_join = */ generic_engine_prepare_join,
	/* .join = */ generic_engine_join,
	/* .complete_join = */ generic_engine_complete_join,
	/* .begin = */ generic_engine_begin,
	/* .begin_statement = */ generic_engine_begin_statement,
	/* .prepare = */ generic_engine_prepare,
	/* .commit = */ generic_engine_commit,
	/* .rollback_statement = */ generic_engine_rollback_statement,
	/* .rollback = */ generic_engine_rollback,
	/* .switch_to_ro = */ generic_engine_switch_to_ro,
	/* .bootstrap = */ generic_engine_bootstrap,
	/* .begin_initial_recovery = */ generic_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ generic_engine_begin_final_recovery,
	/* .begin_hot_standby = */ generic_engine_begin_hot_standby,
	/* .end_recovery = */ generic_engine_end_recovery,
	/* .begin_checkpoint = */ generic_engine_begin_checkpoint,
	/* .wait_checkpoint = */ generic_engine_wait_checkpoint,
	/* .commit_checkpoint = */ generic_engine_commit_checkpoint,
	/* .abort_checkpoint = */ generic_engine_abort_checkpoint,
	/* .collect_garbage = */ generic_engine_collect_garbage,
	/* .backup = */ generic_engine_backup,
	/* .memory_stat = */ generic_engine_memory_stat,
	/* .reset_stat = */ generic_engine_reset_stat,
	/* .check_space_def = */ generic_engine_check_space_def,
};

struct engine *
blackhole_engine_new(void)
{
	struct engine *engine = calloc(1, sizeof(*engine));
	if (engine == NULL) {
		diag_set(OutOfMemory, sizeof(*engine),
			 "malloc", "struct engine");
		return NULL;
	}

	engine->vtab = &blackhole_engine_vtab;
	engine->name = "blackhole";
	engine->flags = ENGINE_BYPASS_TX;
	return engine;
}
