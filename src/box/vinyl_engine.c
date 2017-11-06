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
#include "vinyl_engine.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <small/mempool.h>

#include "trivia/util.h"
#include "vinyl_space.h"
#include "xrow.h"
#include "tuple.h"
#include "txn.h"
#include "space.h"
#include "vinyl.h"

/* Used by lua/info.c */
struct vy_env *
vinyl_engine_get_env(void)
{
	struct vinyl_engine *vinyl;
	vinyl = (struct vinyl_engine *)engine_by_name("vinyl");
	assert(vinyl != NULL);
	return vinyl->env;
}

static void
vinyl_engine_shutdown(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	if (mempool_is_initialized(&vinyl->iterator_pool))
		mempool_destroy(&vinyl->iterator_pool);
	vy_env_delete(vinyl->env);
	free(vinyl);
}

static int
vinyl_engine_bootstrap(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_bootstrap(vinyl->env);
}

static int
vinyl_engine_begin_initial_recovery(struct engine *engine,
				    const struct vclock *recovery_vclock)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_begin_initial_recovery(vinyl->env, recovery_vclock);
}

static int
vinyl_engine_begin_final_recovery(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_begin_final_recovery(vinyl->env);
}

static int
vinyl_engine_end_recovery(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_end_recovery(vinyl->env);
}

static struct space *
vinyl_engine_create_space(struct engine *engine, struct space_def *def,
			  struct rlist *key_list)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vinyl_space_new(vinyl, def, key_list);
}

static int
vinyl_engine_join(struct engine *engine, struct vclock *vclock,
		  struct xstream *stream)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_join(vinyl->env, vclock, stream);
}

static int
vinyl_engine_begin(struct engine *engine, struct txn *txn)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	assert(txn->engine_tx == NULL);
	txn->engine_tx = vy_begin(vinyl->env);
	if (txn->engine_tx == NULL)
		return -1;
	return 0;
}

static int
vinyl_engine_begin_statement(struct engine *engine, struct txn *txn)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	struct vy_tx *tx = (struct vy_tx *)(txn->engine_tx);
	struct txn_stmt *stmt = txn_current_stmt(txn);
	stmt->engine_savepoint = vy_savepoint(vinyl->env, tx);
	return 0;
}

static int
vinyl_engine_prepare(struct engine *engine, struct txn *txn)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	return vy_prepare(vinyl->env, tx);
}

static inline void
txn_stmt_unref_tuples(struct txn_stmt *stmt)
{
	if (stmt->old_tuple)
		tuple_unref(stmt->old_tuple);
	if (stmt->new_tuple)
		tuple_unref(stmt->new_tuple);
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
}

static void
vinyl_engine_commit(struct engine *engine, struct txn *txn)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		txn_stmt_unref_tuples(stmt);
	}
	if (tx) {
		vy_commit(vinyl->env, tx, txn->signature);
		txn->engine_tx = NULL;
	}
}

static void
vinyl_engine_rollback(struct engine *engine, struct txn *txn)
{
	if (txn->engine_tx == NULL)
		return;

	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	vy_rollback(vinyl->env, tx);
	txn->engine_tx = NULL;
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		txn_stmt_unref_tuples(stmt);
	}
}

static void
vinyl_engine_rollback_statement(struct engine *engine, struct txn *txn,
				struct txn_stmt *stmt)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	txn_stmt_unref_tuples(stmt);
	vy_rollback_to_savepoint(vinyl->env, (struct vy_tx *)txn->engine_tx,
				 stmt->engine_savepoint);
}

static int
vinyl_engine_begin_checkpoint(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_begin_checkpoint(vinyl->env);
}

static int
vinyl_engine_wait_checkpoint(struct engine *engine, struct vclock *vclock)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_wait_checkpoint(vinyl->env, vclock);
}

static void
vinyl_engine_commit_checkpoint(struct engine *engine, struct vclock *vclock)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	vy_commit_checkpoint(vinyl->env, vclock);
}

static void
vinyl_engine_abort_checkpoint(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	vy_abort_checkpoint(vinyl->env);
}

static int
vinyl_engine_collect_garbage(struct engine *engine, int64_t lsn)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	vy_collect_garbage(vinyl->env, lsn);
	return 0;
}

static int
vinyl_engine_backup(struct engine *engine, struct vclock *vclock,
		    engine_backup_cb cb, void *arg)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	return vy_backup(vinyl->env, vclock, cb, arg);
}

static int
vinyl_engine_check_space_def(struct space_def *def)
{
	if (def->opts.temporary) {
		diag_set(ClientError, ER_ALTER_SPACE,
			 def->name, "engine does not support temporary flag");
		return -1;
	}
	return 0;
}

static const struct engine_vtab vinyl_engine_vtab = {
	/* .shutdown = */ vinyl_engine_shutdown,
	/* .create_space = */ vinyl_engine_create_space,
	/* .join = */ vinyl_engine_join,
	/* .begin = */ vinyl_engine_begin,
	/* .begin_statement = */ vinyl_engine_begin_statement,
	/* .prepare = */ vinyl_engine_prepare,
	/* .commit = */ vinyl_engine_commit,
	/* .rollback_statement = */ vinyl_engine_rollback_statement,
	/* .rollback = */ vinyl_engine_rollback,
	/* .bootstrap = */ vinyl_engine_bootstrap,
	/* .begin_initial_recovery = */ vinyl_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ vinyl_engine_begin_final_recovery,
	/* .end_recovery = */ vinyl_engine_end_recovery,
	/* .begin_checkpoint = */ vinyl_engine_begin_checkpoint,
	/* .wait_checkpoint = */ vinyl_engine_wait_checkpoint,
	/* .commit_checkpoint = */ vinyl_engine_commit_checkpoint,
	/* .abort_checkpoint = */ vinyl_engine_abort_checkpoint,
	/* .collect_garbage = */ vinyl_engine_collect_garbage,
	/* .backup = */ vinyl_engine_backup,
	/* .check_space_def = */ vinyl_engine_check_space_def,
};

struct vinyl_engine *
vinyl_engine_new(const char *dir, size_t memory, size_t cache,
		 int read_threads, int write_threads, double timeout,
		 bool force_recovery)
{
	struct vinyl_engine *vinyl = calloc(1, sizeof(*vinyl));
	if (vinyl == NULL) {
		diag_set(OutOfMemory, sizeof(*vinyl),
			 "malloc", "struct vinyl_engine");
		return NULL;
	}

	vinyl->env = vy_env_new(dir, memory, cache, read_threads,
				write_threads, timeout, force_recovery);
	if (vinyl->env == NULL) {
		free(vinyl);
		return NULL;
	}

	vinyl->base.vtab = &vinyl_engine_vtab;
	vinyl->base.name = "vinyl";
	return vinyl;
}

void
vinyl_engine_set_max_tuple_size(struct vinyl_engine *vinyl, size_t max_size)
{
	vy_set_max_tuple_size(vinyl->env, max_size);
}

void
vinyl_engine_set_timeout(struct vinyl_engine *vinyl, double timeout)
{
	vy_set_timeout(vinyl->env, timeout);
}
