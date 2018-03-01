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
engine_begin_checkpoint(void)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->begin_checkpoint(engine) < 0)
			return -1;
	}
	return 0;
}

int
engine_commit_checkpoint(struct vclock *vclock)
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

int
engine_collect_garbage(int64_t lsn)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->collect_garbage(engine, lsn) < 0)
			return -1;
	}
	return 0;
}

int
engine_backup(struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->backup(engine, vclock, cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}

int
engine_join(struct vclock *vclock, struct xstream *stream)
{
	struct engine *engine;
	engine_foreach(engine) {
		if (engine->vtab->join(engine, vclock, stream) != 0)
			return -1;
	}
	return 0;
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
