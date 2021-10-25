/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "service_engine.h"
#include "tuple.h"
#include "schema.h"

extern const struct space_vtab session_settings_space_vtab;

static void
service_engine_shutdown(struct engine *engine)
{
	free(engine);
}

static struct space *
service_engine_create_space(struct engine *engine, struct space_def *def,
			    struct rlist *key_list)
{
	/*
	 * At the moment the only space that have this engine is
	 * _session_sessings.
	 */
	if (def->id != BOX_SESSION_SETTINGS_ID) {
		diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
			 "non-system space with 'service' engine.");
		return NULL;
	}
	const struct space_vtab *space_vtab = &session_settings_space_vtab;

	struct space *space = (struct space *)calloc(1, sizeof(*space));
	if (space == NULL) {
		diag_set(OutOfMemory, sizeof(*space), "calloc", "space");
		return NULL;
	}
	int key_count = 0;
	struct key_def **keys = index_def_to_key_def(key_list, &key_count);
	if (keys == NULL) {
		free(space);
		return NULL;
	}
	struct tuple_format *format =
		tuple_format_new(&tuple_format_runtime->vtab, NULL, keys,
				 key_count, def->fields, def->field_count,
				 def->exact_field_count, def->dict,
				 def->opts.is_temporary,
				 def->opts.is_ephemeral);
	if (format == NULL) {
		free(space);
		return NULL;
	}
	tuple_format_ref(format);
	int rc = space_create(space, engine, space_vtab, def, key_list, format);
	/*
	 * Format is now referenced by the space if space has beed
	 * created.
	 */
	tuple_format_unref(format);
	if (rc != 0) {
		free(space);
		return NULL;
	}
	return space;
}

static const struct engine_vtab service_engine_vtab = {
	/* .shutdown = */ service_engine_shutdown,
	/* .create_space = */ service_engine_create_space,
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
service_engine_new(void)
{
	struct engine *service_engine = calloc(1, sizeof(*service_engine));
	if (service_engine == NULL) {
		diag_set(OutOfMemory, sizeof(*service_engine), "calloc",
			 "service_engine");
		return NULL;
	}

	service_engine->vtab = &service_engine_vtab;
	service_engine->name = "service";
	service_engine->flags = ENGINE_BYPASS_TX;
	return service_engine;
}
