/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "phia_engine.h"
#include "phia_index.h"
#include "phia_space.h"
#include "coeio.h"
#include "coio.h"
#include "cfg.h"
#include "xrow.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "txn.h"
#include "index.h"
#include "relay.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "request.h"
#include "iproto_constants.h"
#include "small/pmatomic.h"
#include "phia.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct cord *worker_pool;
static int worker_pool_size;
static volatile int worker_pool_run;

static inline uint32_t
phia_get_parts(struct key_def *key_def, void *obj, void *value, int valuesize,
		 struct iovec *parts, uint32_t *field_count)
{
	/* prepare keys */
	int size = 0;
	assert(key_def->part_count <= 8);
	static const char *PARTNAMES[] = {
		"key_0", "key_1", "key_2", "key_3",
		"key_4", "key_5", "key_6", "key_7"
	};
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		int len = 0;
		parts[i].iov_base = sp_getstring(obj, PARTNAMES[i], &len);
		parts[i].iov_len = len;
		assert(parts[i].iov_base != NULL);
		switch (key_def->parts[i].type) {
		case STRING:
			size += mp_sizeof_str(len);
			break;
		case NUM:
			size += mp_sizeof_uint(load_u64(parts[i].iov_base));
			break;
		default:
			assert(0);
		}
	}
	int count = key_def->part_count;
	void *valueend = (char *) value + valuesize;
	while (value < valueend) {
		count++;
		mp_next((const char **)&value);
	}
	size += mp_sizeof_array(count);
	size += valuesize;
	*field_count = count;
	return size;
}

static inline char *
phia_write_parts(struct key_def *key_def, void *value, int valuesize,
		   struct iovec *parts, char *p)
{
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		switch (key_def->parts[i].type) {
		case STRING:
			p = mp_encode_str(p, (const char *) parts[i].iov_base,
					  parts[i].iov_len);
			break;
		case NUM:
			p = mp_encode_uint(p, load_u64(parts[i].iov_base));
			break;
		default:
			assert(0);
		}
	}
	memcpy(p, value, valuesize);
	return p + valuesize;
}

struct tuple *
phia_tuple_new(void *obj, struct key_def *key_def,
		 struct tuple_format *format)
{
	assert(format);
	assert(key_def->part_count <= 8);
	struct iovec parts[8];
	int valuesize = 0;
	void *value = sp_getstring(obj, "value", &valuesize);
	uint32_t field_count = 0;
	size_t size = phia_get_parts(key_def, obj, value, valuesize, parts,
				       &field_count);
	struct tuple *tuple = tuple_alloc(format, size);
	char *d = tuple->data;
	d = mp_encode_array(d, field_count);
	d = phia_write_parts(key_def, value, valuesize, parts, d);
	assert(tuple->data + size == d);
	try {
		tuple_init_field_map(format, tuple);
	} catch (Exception *e) {
		tuple_delete(tuple);
		throw;
	}
	return tuple;
}

static char *
phia_tuple_data_new(void *obj, struct key_def *key_def, uint32_t *bsize)
{
	assert(key_def->part_count <= 8);
	struct iovec parts[8];
	int valuesize = 0;
	void *value = sp_getstring(obj, "value", &valuesize);
	uint32_t field_count = 0;
	size_t size = phia_get_parts(key_def, obj, value, valuesize, parts,
				       &field_count);
	char *tuple_data = (char *) malloc(size);
	if (tuple_data == NULL)
		tnt_raise(OutOfMemory, size, "malloc", "tuple");
	char *d = tuple_data;
	d = mp_encode_array(d, field_count);
	d = phia_write_parts(key_def, value, valuesize, parts, d);
	assert(tuple_data + size == d);
	*bsize = size;
	return tuple_data;
}

static void*
phia_worker(void *env)
{
	while (pm_atomic_load_explicit(&worker_pool_run,
				       pm_memory_order_relaxed)) {
		int rc = sp_service(env);
		if (rc == -1)
			break;
		if (rc == 0)
			usleep(10000); /* 10ms */
	}
	return NULL;
}

void
phia_workers_start(void *env)
{
	if (worker_pool_run)
		return;
	/* prepare worker pool */
	worker_pool = NULL;
	worker_pool_size = cfg_geti("phia.threads");
	if (worker_pool_size > 0) {
		worker_pool = (struct cord *)calloc(worker_pool_size, sizeof(struct cord));
		if (worker_pool == NULL)
			panic("failed to allocate phia worker pool");
	}
	worker_pool_run = 1;
	for (int i = 0; i < worker_pool_size; i++)
		cord_start(&worker_pool[i], "phia", phia_worker, env);
}

static void
phia_workers_stop(void)
{
	if (! worker_pool_run)
		return;
	pm_atomic_store_explicit(&worker_pool_run, 0, pm_memory_order_relaxed);
	for (int i = 0; i < worker_pool_size; i++)
		cord_join(&worker_pool[i]);
	free(worker_pool);
}

void phia_error(void *env)
{
	char *error = (char *)sp_getstring(env, "phia.error", NULL);
	char msg[512];
	snprintf(msg, sizeof(msg), "%s", error);
	tnt_raise(ClientError, ER_PHIA, msg);
}

int phia_info(const char *name, phia_info_f cb, void *arg)
{
	PhiaEngine *e = (PhiaEngine *)engine_find("phia");
	void *cursor = sp_getobject(e->env, NULL);
	void *o = NULL;
	if (name) {
		while ((o = sp_get(cursor, o))) {
			char *key = (char *)sp_getstring(o, "key", 0);
			if (name && strcmp(key, name) != 0)
				continue;
			char *value = (char *)sp_getstring(o, "value", 0);
			cb(key, value, arg);
			return 1;
		}
		sp_destroy(cursor);
		return 0;
	}
	while ((o = sp_get(cursor, o))) {
		char *key = (char *)sp_getstring(o, "key", 0);
		char *value = (char *)sp_getstring(o, "value", 0);
		cb(key, value, arg);
	}
	sp_destroy(cursor);
	return 0;
}

static struct mempool phia_read_pool;

struct phia_read_task {
	struct coio_task base;
	void *dest;
	void *key;
	void *result;
};

static ssize_t
phia_read_cb(struct coio_task *ptr)
{
	struct phia_read_task *task =
		(struct phia_read_task *) ptr;
	task->result = sp_get(task->dest, task->key);
	return 0;
}

static ssize_t
phia_read_free_cb(struct coio_task *ptr)
{
	struct phia_read_task *task =
		(struct phia_read_task *) ptr;
	if (task->result != NULL)
		sp_destroy(task->result);
	mempool_free(&phia_read_pool, task);
	return 0;
}

void *
phia_read(void *dest, void *key)
{
	struct phia_read_task *task =
		(struct phia_read_task *) mempool_alloc(&phia_read_pool);
	if (task == NULL)
		return NULL;
	task->dest = dest;
	task->key = key;
	task->result = NULL;
	if (coio_task(&task->base, phia_read_cb, phia_read_free_cb,
	              TIMEOUT_INFINITY) == -1) {
		return NULL;
	}
	void *result = task->result;
	mempool_free(&phia_read_pool, task);
	return result;
}

PhiaEngine::PhiaEngine()
	:Engine("phia")
	 ,m_prev_commit_lsn(-1)
	 ,recovery_complete(0)
{
	flags = 0;
	env = NULL;
}

PhiaEngine::~PhiaEngine()
{
	phia_workers_stop();
	if (env)
		sp_destroy(env);
}

void
PhiaEngine::init()
{
	worker_pool_run = 0;
	worker_pool_size = 0;
	worker_pool = NULL;
	/* destroyed with cord() */
	mempool_create(&phia_read_pool, &cord()->slabc,
	               sizeof(struct phia_read_task));
	/* prepare worker pool */
	env = sp_env();
	if (env == NULL)
		panic("failed to create phia environment");
	worker_pool_size = cfg_geti("phia.threads");
	sp_setint(env, "phia.path_create", 0);
	sp_setint(env, "phia.recover", 2);
	sp_setstring(env, "phia.path", cfg_gets("phia_dir"), 0);
	sp_setint(env, "memory.limit", cfg_geti64("phia.memory_limit"));
	sp_setint(env, "compaction.0.async", 1);
	sp_setint(env, "compaction.0.compact_wm", cfg_geti("phia.compact_wm"));
	sp_setint(env, "compaction.0.branch_prio", cfg_geti("phia.branch_prio"));
	sp_setint(env, "compaction.0.branch_age", cfg_geti("phia.branch_age"));
	sp_setint(env, "compaction.0.branch_age_wm", cfg_geti("phia.branch_age_wm"));
	sp_setint(env, "compaction.0.branch_age_period", cfg_geti("phia.branch_age_period"));
	sp_setint(env, "compaction.0.snapshot_period", cfg_geti("phia.snapshot_period"));
	sp_setint(env, "compaction.0.expire_period", cfg_geti("phia.expire_period"));
	sp_setint(env, "compaction.0.expire_prio", cfg_geti("phia.expire_prio"));
	sp_setint(env, "log.enable", 0);
	sp_setint(env, "phia.recover", 3);
	int rc = sp_open(env);
	if (rc == -1)
		phia_error(env);
}

void
PhiaEngine::endRecovery()
{
	if (recovery_complete)
		return;
	/* complete two-phase recovery */
	int rc = sp_open(env);
	if (rc == -1)
		phia_error(env);
	recovery_complete = 1;
}

Handler *
PhiaEngine::open()
{
	return new PhiaSpace(this);
}

static inline void
phia_send_row(struct xstream *stream, uint32_t space_id, char *tuple,
                uint32_t tuple_size, int64_t lsn)
{
	struct request_replace_body body;
	body.m_body = 0x82; /* map of two elements. */
	body.k_space_id = IPROTO_SPACE_ID;
	body.m_space_id = 0xce; /* uint32 */
	body.v_space_id = mp_bswap_u32(space_id);
	body.k_tuple = IPROTO_TUPLE;
	struct xrow_header row;
	row.type = IPROTO_INSERT;
	row.server_id = 0;
	row.lsn = lsn;
	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = tuple;
	row.body[1].iov_len = tuple_size;
	xstream_write(stream, &row);
}

static inline struct key_def *
phia_join_key_def(void *env, void *db)
{
	uint32_t id = sp_getint(db, "id");
	uint32_t count = sp_getint(db, "key-count");
	struct key_def *key_def;
	struct key_opts key_opts = key_opts_default;
	key_def = key_def_new(id, 0, "phia_join", TREE, &key_opts, count);
	unsigned i = 0;
	while (i < count) {
		char path[64];
		snprintf(path, sizeof(path), "db.%d:0.scheme.key_%d", id, i);
		char *type = (char *)sp_getstring(env, path, NULL);
		assert(type != NULL);
		if (strncmp(type, "string", 6) == 0)
			key_def->parts[i].type = STRING;
		else
		if (strncmp(type, "u64", 3) == 0)
			key_def->parts[i].type = NUM;
		free(type);
		key_def->parts[i].fieldno = i;
		i++;
	}
	return key_def;
}

/**
 * Relay all data currently stored in Phia engine
 * to the replica.
 */
void
PhiaEngine::join(struct xstream *stream)
{
	/* iterate through a list of databases currently used
	 * in Phia engine */
	void *db;
	void *db_cursor = sp_getobject(env, "db");
	if (db_cursor == NULL)
		phia_error(env);
	while ((db = sp_get(db_cursor, NULL)))
	{
		/* prepare space schema */
		struct key_def *key_def;
		try {
			key_def = phia_join_key_def(env, db);
		} catch (Exception *e) {
			sp_destroy(db_cursor);
			throw;
		}
		/* send database */
		void *cursor = sp_cursor(env);
		if (cursor == NULL) {
			sp_destroy(db_cursor);
			key_def_delete(key_def);
			phia_error(env);
		}
		/* tell cursor not to hold a transaction, which
		 * in result enables compaction process
		 * for a duplicates */
		sp_setint(cursor, "read_commited", 1);

		void *obj = sp_document(db);
		while ((obj = sp_get(cursor, obj)))
		{
			int64_t lsn = sp_getint(obj, "lsn");
			uint32_t tuple_size;
			char *tuple = phia_tuple_data_new(obj, key_def,
								 &tuple_size);
			try {
				phia_send_row(stream, key_def->space_id,
						tuple, tuple_size, lsn);
			} catch (Exception *e) {
				key_def_delete(key_def);
				free(tuple);
				sp_destroy(obj);
				sp_destroy(cursor);
				sp_destroy(db_cursor);
				throw;
			}
			free(tuple);
		}
		sp_destroy(cursor);
		key_def_delete(key_def);
	}
	sp_destroy(db_cursor);
}

Index*
PhiaEngine::createIndex(struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE: return new PhiaIndex(key_def);
	default:
		assert(false);
		return NULL;
	}
}

void
PhiaEngine::dropIndex(Index *index)
{
	PhiaIndex *i = (PhiaIndex *)index;
	/* schedule asynchronous drop */
	int rc = sp_drop(i->db);
	if (rc == -1)
		phia_error(env);
	/* unref db object */
	rc = sp_destroy(i->db);
	if (rc == -1)
		phia_error(env);
	i->db  = NULL;
	i->env = NULL;
}

void
PhiaEngine::keydefCheck(struct space *space, struct key_def *key_def)
{
	if (key_def->type != TREE) {
		tnt_raise(ClientError, ER_INDEX_TYPE,
		          key_def->name,
		          space_name(space));
	}
	if (! key_def->opts.is_unique) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  key_def->name,
			  space_name(space),
			  "Phia index must be unique");
	}
	if (key_def->iid != 0) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  key_def->name,
			  space_name(space),
			  "Phia secondary indexes are not supported");
	}
	const uint32_t keypart_limit = 8;
	if (key_def->part_count > keypart_limit) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "Phia index key has too many parts (8 max)");
	}
	unsigned i = 0;
	while (i < key_def->part_count) {
		struct key_part *part = &key_def->parts[i];
		if (part->type != NUM && part->type != STRING) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
					  key_def->name,
					  space_name(space),
					  "Phia index field type must be STR or NUM");
		}
		if (part->fieldno != i) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
					  key_def->name,
					  space_name(space),
					  "Phia key parts must follow first and cannot be sparse");
		}
		i++;
	}
}

void
PhiaEngine::begin(struct txn *txn)
{
	assert(txn->engine_tx == NULL);
	txn->engine_tx = sp_begin(env);
	if (txn->engine_tx == NULL)
		phia_error(env);
}

void
PhiaEngine::prepare(struct txn *txn)
{
	/* A half committed transaction is no longer
	 * being part of concurrent index, but still can be
	 * commited or rolled back.
	 *
	 * This mode disables conflict resolution for 'prepared'
	 * transactions and solves the issue with concurrent
	 * write-write conflicts during wal write/yield.
	 *
	 * It is important to maintain correct serial
	 * commit order by wal_writer.
	 */
	sp_setint(txn->engine_tx, "half_commit", 1);

	int rc = sp_commit(txn->engine_tx);
	switch (rc) {
	case 1: /* rollback */
		txn->engine_tx = NULL;
	case 2: /* lock */
		tnt_raise(ClientError, ER_TRANSACTION_CONFLICT);
		break;
	case -1:
		phia_error(env);
		break;
	}
}

void
PhiaEngine::commit(struct txn *txn, int64_t signature)
{
	if (txn->engine_tx == NULL)
		return;

	if (txn->n_rows > 0) {
		/* commit transaction using transaction commit signature */
		assert(signature >= 0);

		if (m_prev_commit_lsn == signature) {
			panic("phia commit panic: m_prev_commit_lsn == signature = %"
			      PRIu64, signature);
		}
		/* Set tx id in Phia only if tx has WRITE requests */
		sp_setint(txn->engine_tx, "lsn", signature);
		m_prev_commit_lsn = signature;
	}

	int rc = sp_commit(txn->engine_tx);
	if (rc == -1) {
		panic("phia commit failed: txn->signature = %"
		      PRIu64, signature);
	}
	txn->engine_tx = NULL;
}

void
PhiaEngine::rollback(struct txn *txn)
{
	if (txn->engine_tx) {
		sp_destroy(txn->engine_tx);
		txn->engine_tx = NULL;
	}
}

void
PhiaEngine::beginWalRecovery()
{
	int rc = sp_open(env);
	if (rc == -1)
		phia_error(env);
}

int
PhiaEngine::beginCheckpoint()
{
	/* do not initiate checkpoint during bootstrap,
	 * thread pool is not up yet */
	if (! worker_pool_run)
		return 0;

	int rc = sp_setint(env, "scheduler.checkpoint", 0);
	if (rc == -1)
		phia_error(env);
	return 0;
}

int
PhiaEngine::waitCheckpoint(struct vclock*)
{
	if (! worker_pool_run)
		return 0;
	for (;;) {
		int64_t is_active = sp_getint(env, "scheduler.checkpoint_active");
		if (! is_active)
			break;
		fiber_yield_timeout(.020);
	}
	return 0;
}
