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

#include "trivia/util.h"
#include "cfg.h"
#include "scoped_guard.h"

#include "vinyl_index.h"
#include "vinyl_space.h"
#include "xrow.h"
#include "tuple.h"
#include "txn.h"
#include "space.h"
#include "schema.h"
#include "iproto_constants.h"
#include "vinyl.h"
#include "vy_stmt.h"

/* Used by lua/info.c */
extern "C" struct vy_env *
vinyl_engine_get_env()
{
	VinylEngine *e = (VinylEngine *)engine_find("vinyl");
	return e->env;
}

VinylEngine::VinylEngine()
	:Engine("vinyl")
{
	env = NULL;
}

VinylEngine::~VinylEngine()
{
	if (env)
		vy_env_delete(env);
}

void
VinylEngine::init()
{
	env = vy_env_new(cfg_gets("vinyl_dir"),
			 cfg_geti64("vinyl_memory"),
			 cfg_geti64("vinyl_cache"),
			 cfg_geti("vinyl_read_threads"),
			 cfg_geti("vinyl_write_threads"),
			 cfg_getd("vinyl_timeout"));
	if (env == NULL)
		panic("failed to create vinyl environment");
}

void
VinylEngine::bootstrap()
{
	if (vy_bootstrap(env) != 0)
		diag_raise();
}

void
VinylEngine::beginInitialRecovery(const struct vclock *recovery_vclock)
{
	if (vy_begin_initial_recovery(env, recovery_vclock) != 0)
		diag_raise();
}

void
VinylEngine::beginFinalRecovery()
{
	if (vy_begin_final_recovery(env) != 0)
		diag_raise();
}

void
VinylEngine::endRecovery()
{
	/* complete two-phase recovery */
	if (vy_end_recovery(env) != 0)
		diag_raise();
}

Handler *
VinylEngine::createSpace(struct rlist *key_list, struct field_def *fields,
			 uint32_t field_count, uint32_t index_count,
			 uint32_t exact_field_count)
{
	struct index_def *index_def;
	uint32_t key_no = 0;
	struct key_def **keys =
		(struct key_def **)region_alloc_xc(&fiber()->gc,
						   sizeof(*keys) * index_count);

	rlist_foreach_entry(index_def, key_list, link)
			keys[key_no++] = index_def->key_def;

	struct tuple_format *format =
		tuple_format_new(&vy_tuple_format_vtab, keys, index_count, 0,
				 fields, field_count);
	if (format == NULL)
		diag_raise();
	tuple_format_ref(format);
	format->exact_field_count = exact_field_count;
	auto format_guard = make_scoped_guard([=] { tuple_format_unref(format); });
	return new VinylSpace(this, format);
}

void
VinylEngine::join(struct vclock *vclock, struct xstream *stream)
{
	if (vy_join(env, vclock, stream) != 0)
		diag_raise();
}


void
VinylEngine::begin(struct txn *txn)
{
	assert(txn->engine_tx == NULL);
	txn->engine_tx = vy_begin(env);
	if (txn->engine_tx == NULL)
		diag_raise();
}

void
VinylEngine::beginStatement(struct txn *txn)
{
	assert(txn != NULL);
	struct vy_tx *tx = (struct vy_tx *)(txn->engine_tx);
	struct txn_stmt *stmt = txn_current_stmt(txn);
	stmt->engine_savepoint = vy_savepoint(env, tx);
}

void
VinylEngine::prepare(struct txn *txn)
{
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;

	if (vy_prepare(env, tx))
		diag_raise();
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

void
VinylEngine::commit(struct txn *txn)
{
	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		txn_stmt_unref_tuples(stmt);
	}
	if (tx) {
		vy_commit(env, tx, txn->signature);
		txn->engine_tx = NULL;
	}
}

void
VinylEngine::rollback(struct txn *txn)
{
	if (txn->engine_tx == NULL)
		return;

	struct vy_tx *tx = (struct vy_tx *) txn->engine_tx;
	vy_rollback(env, tx);
	txn->engine_tx = NULL;
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		txn_stmt_unref_tuples(stmt);
	}
}

void
VinylEngine::rollbackStatement(struct txn *txn, struct txn_stmt *stmt)
{
	txn_stmt_unref_tuples(stmt);
	vy_rollback_to_savepoint(env, (struct vy_tx *)txn->engine_tx,
				 stmt->engine_savepoint);
}


int
VinylEngine::beginCheckpoint()
{
	return vy_begin_checkpoint(env);
}

int
VinylEngine::waitCheckpoint(struct vclock *vclock)
{
	return vy_wait_checkpoint(env, vclock);
}

void
VinylEngine::commitCheckpoint(struct vclock *vclock)
{
	vy_commit_checkpoint(env, vclock);
}

void
VinylEngine::abortCheckpoint()
{
	vy_abort_checkpoint(env);
}

int
VinylEngine::collectGarbage(int64_t lsn)
{
	vy_collect_garbage(env, lsn);
	return 0;
}

int
VinylEngine::backup(struct vclock *vclock, engine_backup_cb cb, void *arg)
{
	return vy_backup(env, vclock, cb, arg);
}

void
VinylEngine::setMaxTupleSize(size_t max_size)
{
	if (vy_set_max_tuple_size(env, max_size) != 0)
		diag_raise();
}

void
VinylEngine::setTimeout(double timeout)
{
	if (vy_set_timeout(env, timeout) != 0)
		diag_raise();
}

void
VinylEngine::checkSpaceDef(struct space_def *def)
{
	if (def->opts.temporary) {
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  def->name, "engine does not support temporary flag");
	}
}
