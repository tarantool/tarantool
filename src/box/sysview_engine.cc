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
#include "sysview_engine.h"
#include "sysview_index.h"
#include "schema.h"
#include "space.h"

static void
sysview_space_destroy(struct space *space)
{
	free(space);
}

static size_t
sysview_space_bsize(struct space *)
{
	return 0;
}

static int
sysview_space_apply_initial_join_row(struct space *, struct request *)
{
	unreachable();
	return 0;
}

static int
sysview_space_execute_replace(struct space *space, struct txn *,
			      struct request *, struct tuple **)
{
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static int
sysview_space_execute_delete(struct space *space, struct txn *,
			     struct request *, struct tuple **)
{
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static int
sysview_space_execute_update(struct space *space, struct txn *,
			     struct request *, struct tuple **)
{
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static int
sysview_space_execute_upsert(struct space *space, struct txn *,
			     struct request *)
{
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static void
sysview_init_system_space(struct space *)
{
	unreachable();
}

static int
sysview_space_check_index_def(struct space *, struct index_def *)
{
	return 0;
}

static struct index *
sysview_space_create_index(struct space *space, struct index_def *index_def)
{
	return (struct index *)sysview_index_new(index_def, space_name(space));
}

static int
sysview_space_add_primary_key(struct space *)
{
	return 0;
}

static void
sysview_space_drop_primary_key(struct space *)
{
}

static int
sysview_space_build_secondary_key(struct space *, struct space *,
				  struct index *)
{
	return 0;
}

static int
sysview_space_prepare_truncate(struct space *, struct space *)
{
	return 0;
}

static void
sysview_space_commit_truncate(struct space *, struct space *)
{
}

static int
sysview_space_prepare_alter(struct space *, struct space *)
{
	return 0;
}

static void
sysview_space_commit_alter(struct space *, struct space *)
{
}

static int
sysview_space_check_format(struct space *, struct space *)
{
	unreachable();
	return 0;
}

static const struct space_vtab sysview_space_vtab = {
	/* .destroy = */ sysview_space_destroy,
	/* .bsize = */ sysview_space_bsize,
	/* .apply_initial_join_row = */ sysview_space_apply_initial_join_row,
	/* .execute_replace = */ sysview_space_execute_replace,
	/* .execute_delete = */ sysview_space_execute_delete,
	/* .execute_update = */ sysview_space_execute_update,
	/* .execute_upsert = */ sysview_space_execute_upsert,
	/* .execute_select = */ generic_space_execute_select,
	/* .init_system_space = */ sysview_init_system_space,
	/* .check_index_def = */ sysview_space_check_index_def,
	/* .create_index = */ sysview_space_create_index,
	/* .add_primary_key = */ sysview_space_add_primary_key,
	/* .drop_primary_key = */ sysview_space_drop_primary_key,
	/* .check_format = */ sysview_space_check_format,
	/* .build_secondary_key = */ sysview_space_build_secondary_key,
	/* .prepare_truncate = */ sysview_space_prepare_truncate,
	/* .commit_truncate = */ sysview_space_commit_truncate,
	/* .prepare_alter = */ sysview_space_prepare_alter,
	/* .commit_alter = */ sysview_space_commit_alter,
};

static void
sysview_engine_shutdown(struct engine *engine)
{
	free(engine);
}

static struct space *
sysview_engine_create_space(struct engine *engine, struct space_def *def,
			    struct rlist *key_list)
{
	struct space *space = (struct space *)calloc(1, sizeof(*space));
	if (space == NULL)
		tnt_raise(OutOfMemory, sizeof(*space),
			  "malloc", "struct space");
	if (space_create(space, engine, &sysview_space_vtab,
			 def, key_list, NULL) != 0) {
		free(space);
		diag_raise();
	}
	return space;
}

static void
sysview_engine_begin(struct engine *, struct txn *)
{
}

static void
sysview_engine_begin_statement(struct engine *, struct txn *)
{
}

static void
sysview_engine_prepare(struct engine *, struct txn *)
{
}

static void
sysview_engine_commit(struct engine *, struct txn *)
{
}

static void
sysview_engine_rollback(struct engine *, struct txn *)
{
}

static void
sysview_engine_rollback_statement(struct engine *, struct txn *,
				  struct txn_stmt *)
{
}

static void
sysview_engine_bootstrap(struct engine *)
{
}

static void
sysview_engine_begin_initial_recovery(struct engine *, const struct vclock *)
{
}

static void
sysview_engine_begin_final_recovery(struct engine *)
{
}

static void
sysview_engine_end_recovery(struct engine *)
{
}

static void
sysview_engine_join(struct engine *, struct vclock *,
		    struct xstream *)
{
}

static int
sysview_engine_begin_checkpoint(struct engine *)
{
	return 0;
}

static int
sysview_engine_wait_checkpoint(struct engine *, struct vclock *)
{
	return 0;
}

static void
sysview_engine_commit_checkpoint(struct engine *, struct vclock *)
{
}

static void
sysview_engine_abort_checkpoint(struct engine *)
{
}

static int
sysview_engine_collect_garbage(struct engine *, int64_t)
{
	return 0;
}

static int
sysview_engine_backup(struct engine *, struct vclock *,
		      engine_backup_cb, void *)
{
	return 0;
}

static void
sysview_engine_check_space_def(struct space_def *)
{
}

static const struct engine_vtab sysview_engine_vtab = {
	/* .shutdown = */ sysview_engine_shutdown,
	/* .create_space = */ sysview_engine_create_space,
	/* .join = */ sysview_engine_join,
	/* .begin = */ sysview_engine_begin,
	/* .begin_statement = */ sysview_engine_begin_statement,
	/* .prepare = */ sysview_engine_prepare,
	/* .commit = */ sysview_engine_commit,
	/* .rollback_statement = */ sysview_engine_rollback_statement,
	/* .rollback = */ sysview_engine_rollback,
	/* .bootstrap = */ sysview_engine_bootstrap,
	/* .begin_initial_recovery = */ sysview_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ sysview_engine_begin_final_recovery,
	/* .end_recovery = */ sysview_engine_end_recovery,
	/* .begin_checkpoint = */ sysview_engine_begin_checkpoint,
	/* .wait_checkpoint = */ sysview_engine_wait_checkpoint,
	/* .commit_checkpoint = */ sysview_engine_commit_checkpoint,
	/* .abort_checkpoint = */ sysview_engine_abort_checkpoint,
	/* .collect_garbage = */ sysview_engine_collect_garbage,
	/* .backup = */ sysview_engine_backup,
	/* .check_space_def = */ sysview_engine_check_space_def,
};

struct sysview_engine *
sysview_engine_new(void)
{
	struct sysview_engine *sysview =
		(struct sysview_engine *)calloc(1, sizeof(*sysview));
	if (sysview == NULL) {
		tnt_raise(OutOfMemory, sizeof(*sysview),
			  "malloc", "struct sysview_engine");
	}

	sysview->base.vtab = &sysview_engine_vtab;
	sysview->base.name = "sysview";
	return sysview;
}
