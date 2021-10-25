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

#include <stdint.h>
#include <string.h>
#include <small/rlist.h>

RLIST_HEAD(engines);

/**
 * For simplicity, assume that the engine count can't exceed
 * the value of this constant.
 */
enum { MAX_ENGINE_COUNT = 10 };

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

void
engine_shutdown(void)
{
	struct engine *engine, *tmp;
	rlist_foreach_entry_safe(engine, &engines, link, tmp) {
		engine->vtab->shutdown(engine);
	}
}

void
engine_switch_to_ro(void)
{
	struct engine *engine;
	engine_foreach(engine)
		engine->vtab->switch_to_ro(engine);
}

int
engine_bootstrap(void)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->bootstrap(engine) != 0)
			return -1;
	}
	return 0;
}

int
engine_begin_initial_recovery(const struct vclock *recovery_vclock)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->begin_initial_recovery(engine,
					recovery_vclock) != 0)
			return -1;
	}
	return 0;
}

int
engine_begin_final_recovery(void)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->begin_final_recovery(engine) != 0)
			return -1;
	}
	return 0;
}

int
engine_begin_hot_standby(void)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->begin_hot_standby(engine) != 0)
			return -1;
	}
	return 0;
}

int
engine_end_recovery(void)
{
	/*
	 * For all new spaces created after recovery is complete,
	 * when the primary key is added, enable all keys.
	 */
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->end_recovery(engine) != 0)
			return -1;
	}
	return 0;
}

int
engine_begin_checkpoint(bool is_scheduled)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->begin_checkpoint(engine, is_scheduled) < 0)
			return -1;
	}
	return 0;
}

int
engine_commit_checkpoint(const struct vclock *vclock)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->wait_checkpoint(engine, vclock) < 0)
			return -1;
	}
	engine_foreach(engine) {
		engine->vtab->commit_checkpoint(engine, vclock);
	}
	return 0;
}

void
engine_abort_checkpoint(void)
{
	struct engine *engine;
	engine_foreach(engine)
		engine->vtab->abort_checkpoint(engine);
}

void
engine_collect_garbage(const struct vclock *vclock)
{
	struct engine *engine;
	engine_foreach(engine)
		engine->vtab->collect_garbage(engine, vclock);
}

int
engine_backup(const struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->backup(engine, vclock, cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}

int
engine_prepare_join(struct engine_join_ctx *ctx)
{
	ctx->array = calloc(MAX_ENGINE_COUNT, sizeof(void *));
	if (ctx->array == NULL) {
		diag_set(OutOfMemory, MAX_ENGINE_COUNT * sizeof(void *),
			 "malloc", "engine join context");
		return -1;
	}
	int i = 0;
	struct engine *engine;
	engine_foreach(engine) {
		assert(i < MAX_ENGINE_COUNT);
		if (engine->vtab->prepare_join(engine, &ctx->array[i]) != 0)
			goto fail;
		i++;
	}
	return 0;
fail:
	engine_complete_join(ctx);
	return -1;
}

int
engine_join(struct engine_join_ctx *ctx, struct xstream *stream)
{
	int i = 0;
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->join(engine, ctx->array[i], stream) != 0)
			return -1;
		i++;
	}
	return 0;
}

void
engine_complete_join(struct engine_join_ctx *ctx)
{
	int i = 0;
	struct engine *engine;
	engine_foreach(engine) {
		if (ctx->array[i] != NULL)
			engine->vtab->complete_join(engine, ctx->array[i]);
		i++;
	}
	free(ctx->array);
}

void
engine_memory_stat(struct engine_memory_stat *stat)
{
	memset(stat, 0, sizeof(*stat));
	struct engine *engine;
	engine_foreach(engine)
		engine->vtab->memory_stat(engine, stat);
}

void
engine_reset_stat(void)
{
	struct engine *engine;
	engine_foreach(engine)
		engine->vtab->reset_stat(engine);
}

/* {{{ Virtual method stubs */

int
generic_engine_prepare_join(struct engine *engine, void **ctx)
{
	(void)engine;
	*ctx = NULL;
	return 0;
}

int
generic_engine_join(struct engine *engine, void *ctx, struct xstream *stream)
{
	(void)engine;
	(void)ctx;
	(void)stream;
	return 0;
}

void
generic_engine_complete_join(struct engine *engine, void *ctx)
{
	(void)engine;
	(void)ctx;
}

int
generic_engine_begin(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
	return 0;
}

int
generic_engine_begin_statement(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
	return 0;
}

int
generic_engine_prepare(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
	return 0;
}

void
generic_engine_commit(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
}

void
generic_engine_rollback_statement(struct engine *engine, struct txn *txn,
				  struct txn_stmt *stmt)
{
	(void)engine;
	(void)txn;
	(void)stmt;
}

void
generic_engine_rollback(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
}

void
generic_engine_switch_to_ro(struct engine *engine)
{
	(void)engine;
}

int
generic_engine_bootstrap(struct engine *engine)
{
	(void)engine;
	return 0;
}

int
generic_engine_begin_initial_recovery(struct engine *engine,
				      const struct vclock *vclock)
{
	(void)engine;
	(void)vclock;
	return 0;
}

int
generic_engine_begin_final_recovery(struct engine *engine)
{
	(void)engine;
	return 0;
}

int
generic_engine_begin_hot_standby(struct engine *engine)
{
	(void)engine;
	return 0;
}

int
generic_engine_end_recovery(struct engine *engine)
{
	(void)engine;
	return 0;
}

int
generic_engine_begin_checkpoint(struct engine *engine, bool is_scheduled)
{
	(void)engine;
	(void)is_scheduled;
	return 0;
}

int
generic_engine_wait_checkpoint(struct engine *engine,
			       const struct vclock *vclock)
{
	(void)engine;
	(void)vclock;
	return 0;
}

void
generic_engine_commit_checkpoint(struct engine *engine,
				 const struct vclock *vclock)
{
	(void)engine;
	(void)vclock;
}

void
generic_engine_abort_checkpoint(struct engine *engine)
{
	(void)engine;
}

void
generic_engine_collect_garbage(struct engine *engine,
			       const struct vclock *vclock)
{
	(void)engine;
	(void)vclock;
}

int
generic_engine_backup(struct engine *engine, const struct vclock *vclock,
		      engine_backup_cb cb, void *cb_arg)
{
	(void)engine;
	(void)vclock;
	(void)cb;
	(void)cb_arg;
	return 0;
}

void
generic_engine_memory_stat(struct engine *engine,
			   struct engine_memory_stat *stat)
{
	(void)engine;
	(void)stat;
}

void
generic_engine_reset_stat(struct engine *engine)
{
	(void)engine;
}

int
generic_engine_check_space_def(struct space_def *def)
{
	(void)def;
	return 0;
}

/* }}} */
