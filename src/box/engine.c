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

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errinj.h"
#include "fiber.h"

struct engine *engines[MAX_ENGINE_COUNT + 1];
enum recovery_state recovery_state = RECOVERY_NOT_STARTED;

/** Number of registered engines. */
static int engine_count;

void
engine_register(struct engine *engine)
{
	assert(engine_count < MAX_ENGINE_COUNT);
	assert((engine->flags & ENGINE_BYPASS_TX) != 0 ||
	       engine_count < MAX_TX_ENGINE_COUNT);
	engine->id = engine_count++;
	engines[engine->id] = engine;
}

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
	struct engine *engine;
	engine_foreach(engine)
		engine->vtab->shutdown(engine);
}

void
engine_free(void)
{
	struct engine *engine;
	for (int i = 0; i < engine_count; i++) {
		engine = engines[i];
		engines[i] = NULL;
		engine->vtab->free(engine);
	}
	engine_count = 0;
}

int
engine_bootstrap(void)
{
	recovery_state = INITIAL_RECOVERY;
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->bootstrap(engine) != 0)
			return -1;
	}
	recovery_state = FINISHED_RECOVERY;
	return 0;
}

int
engine_begin_initial_recovery(const struct vclock *recovery_vclock)
{
	recovery_state = INITIAL_RECOVERY;
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
	recovery_state = FINAL_RECOVERY;
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
	recovery_state = FINISHED_RECOVERY;
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
	ctx->data = xcalloc(MAX_ENGINE_COUNT, sizeof(void *));
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->prepare_join(engine, ctx) != 0)
			goto fail;
	}
	return 0;
fail:
	engine_complete_join(ctx);
	return -1;
}

int
engine_join(struct engine_join_ctx *ctx, struct xstream *stream)
{
	ERROR_INJECT_YIELD(ERRINJ_ENGINE_JOIN_DELAY);

	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->join(engine, ctx, stream) != 0)
			return -1;
	}
	return 0;
}

void
engine_complete_join(struct engine_join_ctx *ctx)
{
	struct engine *engine;
	engine_foreach(engine) {
		engine->vtab->complete_join(engine, ctx);
	}
	free(ctx->data);
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

struct engine_read_view *
generic_engine_create_read_view(struct engine *engine,
				const struct read_view_opts *opts)
{
	(void)engine;
	(void)opts;
	unreachable();
	return NULL;
}

int
generic_engine_prepare_join(struct engine *engine, struct engine_join_ctx *ctx)
{
	ctx->data[engine->id] = NULL;
	return 0;
}

int
generic_engine_join(struct engine *engine, struct engine_join_ctx *ctx,
		    struct xstream *stream)
{
	(void)engine;
	(void)ctx;
	(void)stream;
	return 0;
}

void
generic_engine_complete_join(struct engine *engine, struct engine_join_ctx *ctx)
{
	(void)engine;
	(void)ctx;
}

void
generic_engine_begin(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
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
generic_engine_send_to_read_view(struct engine *engine, struct txn *txn,
				 int64_t psn)
{
	(void)engine;
	(void)txn;
	(void)psn;
	unreachable();
}

void
generic_engine_abort_with_conflict(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
	unreachable();
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

void
generic_engine_shutdown(struct engine *engine)
{
	(void)engine;
}

/* }}} */
