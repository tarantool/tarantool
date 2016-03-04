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
#include "sophia_index.h"
#include "sophia_engine.h"
#include "coeio.h"
#include "coio.h"
#include "cfg.h"
#include "xrow.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "txn.h"
#include "index.h"
#include "recovery.h"
#include "relay.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "request.h"
#include "iproto_constants.h"
#include "small/rlist.h"
#include "small/pmatomic.h"
#include <errinj.h>
#include <sophia.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

struct cord *worker_pool;
static int worker_pool_size;
static volatile int worker_pool_run;

static void*
sophia_worker(void *env)
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
sophia_workers_start(void *env)
{
	if (worker_pool_run)
		return;
	/* prepare worker pool */
	worker_pool = NULL;
	worker_pool_size = cfg_geti("sophia.threads");
	if (worker_pool_size > 0) {
		worker_pool = (struct cord *)calloc(worker_pool_size, sizeof(struct cord));
		if (worker_pool == NULL)
			panic("failed to allocate sophia worker pool");
	}
	worker_pool_run = 1;
	for (int i = 0; i < worker_pool_size; i++)
		cord_start(&worker_pool[i], "sophia", sophia_worker, env);
}

static void
sophia_workers_stop(void)
{
	if (! worker_pool_run)
		return;
	pm_atomic_store_explicit(&worker_pool_run, 0, pm_memory_order_relaxed);
	for (int i = 0; i < worker_pool_size; i++)
		cord_join(&worker_pool[i]);
	free(worker_pool);
}

void sophia_error(void *env)
{
	char *error = (char *)sp_getstring(env, "sophia.error", NULL);
	char msg[512];
	snprintf(msg, sizeof(msg), "%s", error);
	tnt_raise(ClientError, ER_SOPHIA, msg);
}

int sophia_info(const char *name, sophia_info_f cb, void *arg)
{
	SophiaEngine *e = (SophiaEngine *)engine_find("sophia");
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

static struct mempool sophia_read_pool;

struct sophia_read_task {
	struct coio_task base;
	void *dest;
	void *key;
	void *result;
};

static ssize_t
sophia_read_cb(struct coio_task *ptr)
{
	struct sophia_read_task *task =
		(struct sophia_read_task *) ptr;
	task->result = sp_get(task->dest, task->key);
	return 0;
}

static ssize_t
sophia_read_free_cb(struct coio_task *ptr)
{
	struct sophia_read_task *task =
		(struct sophia_read_task *) ptr;
	if (task->result != NULL)
		sp_destroy(task->result);
	mempool_free(&sophia_read_pool, task);
	return 0;
}

void *
sophia_read(void *dest, void *key)
{
	struct sophia_read_task *task =
		(struct sophia_read_task *) mempool_alloc(&sophia_read_pool);
	if (task == NULL)
		return NULL;
	task->dest = dest;
	task->key = key;
	task->result = NULL;
	if (coio_task(&task->base, sophia_read_cb, sophia_read_free_cb,
	              TIMEOUT_INFINITY) == -1) {
		return NULL;
	}
	void *result = task->result;
	mempool_free(&sophia_read_pool, task);
	return result;
}

struct SophiaSpace: public Handler {
	SophiaSpace(Engine*);
	virtual struct tuple *
	executeReplace(struct txn*, struct space *space,
	               struct request *request);
	virtual struct tuple *
	executeDelete(struct txn*, struct space *space,
	              struct request *request);
	virtual struct tuple *
	executeUpdate(struct txn*, struct space *space,
	              struct request *request);
	virtual void
	executeUpsert(struct txn*, struct space *space,
	              struct request *request);
};

struct tuple *
SophiaSpace::executeReplace(struct txn *txn, struct space *space,
                            struct request *request)
{
	(void) txn;

	SophiaIndex *index = (SophiaIndex *)index_find(space, 0);

	space_validate_tuple_raw(space, request->tuple);

	int size = request->tuple_end - request->tuple;
	const char *key =
		tuple_field_raw(request->tuple, size,
		                index->key_def->parts[0].fieldno);
	primary_key_validate(index->key_def, key, index->key_def->part_count);

	/* Switch from INSERT to REPLACE during recovery.
	 *
	 * Database might hold newer key version than currenly
	 * recovered log record.
	 */
	enum dup_replace_mode mode = DUP_REPLACE_OR_INSERT;
	if (request->type == IPROTO_INSERT) {
		SophiaEngine *engine = (SophiaEngine *)space->handler->engine;
		if (engine->recovery_complete)
			mode = DUP_INSERT;
	}
	index->replace_or_insert(request->tuple, request->tuple_end, mode);
	return NULL;
}

struct tuple *
SophiaSpace::executeDelete(struct txn *txn, struct space *space,
                           struct request *request)
{
	(void) txn;

	SophiaIndex *index = (SophiaIndex *)index_find(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);
	index->remove(key);
	return NULL;
}

struct tuple *
SophiaSpace::executeUpdate(struct txn *txn, struct space *space,
                           struct request *request)
{
	(void) txn;

	/* Try to find the tuple by unique key */
	SophiaIndex *index = (SophiaIndex *)index_find(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);
	struct tuple *old_tuple = index->findByKey(key, part_count);

	if (old_tuple == NULL)
		return NULL;
	/* Sophia always yields a zero-ref tuple, GC it here. */
	TupleRef old_ref(old_tuple);

	/* Do tuple update */
	struct tuple *new_tuple =
		tuple_update(space->format,
		             region_aligned_alloc_xc_cb,
		             &fiber()->gc,
		             old_tuple, request->tuple,
		             request->tuple_end,
		             request->index_base);
	TupleRef ref(new_tuple);

	space_validate_tuple(space, new_tuple);
	space_check_update(space, old_tuple, new_tuple);

	index->replace_or_insert(new_tuple->data,
	                         new_tuple->data + new_tuple->bsize,
	                         DUP_REPLACE);
	return NULL;
}

void
SophiaSpace::executeUpsert(struct txn *txn, struct space *space,
                           struct request *request)
{
	(void) txn;
	SophiaIndex *index = (SophiaIndex *)index_find(space, request->index_id);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);
	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	index->upsert(request->ops,
	              request->ops_end,
	              request->tuple,
	              request->tuple_end,
	              request->index_base);
}

SophiaSpace::SophiaSpace(Engine *e)
	:Handler(e)
{
}

SophiaEngine::SophiaEngine()
	:Engine("sophia")
	 ,m_prev_commit_lsn(-1)
	 ,m_prev_checkpoint_lsn(-1)
	 ,m_checkpoint_lsn(-1)
	 ,recovery_complete(0)
{
	flags = 0;
	env = NULL;
}

SophiaEngine::~SophiaEngine()
{
	sophia_workers_stop();
	if (env)
		sp_destroy(env);
}

void
SophiaEngine::init()
{
	worker_pool_run = 0;
	worker_pool_size = 0;
	worker_pool = NULL;
	/* destroyed with cord() */
	mempool_create(&sophia_read_pool, &cord()->slabc,
	               sizeof(struct sophia_read_task));
	/* prepare worker pool */
	env = sp_env();
	if (env == NULL)
		panic("failed to create sophia environment");
	worker_pool_size = cfg_geti("sophia.threads");
	sp_setint(env, "sophia.path_create", 0);
	sp_setint(env, "sophia.recover", 2);
	sp_setstring(env, "sophia.path", cfg_gets("sophia_dir"), 0);
	sp_setint(env, "scheduler.threads", 0);
	sp_setint(env, "memory.limit", cfg_geti64("sophia.memory_limit"));
	sp_setint(env, "compaction.0.async", 1);
	sp_setint(env, "compaction.0.compact_wm", cfg_geti("sophia.compact_wm"));
	sp_setint(env, "compaction.0.branch_prio", cfg_geti("sophia.branch_prio"));
	sp_setint(env, "compaction.0.branch_age", cfg_geti("sophia.branch_age"));
	sp_setint(env, "compaction.0.branch_age_wm", cfg_geti("sophia.branch_age_wm"));
	sp_setint(env, "compaction.0.branch_age_period", cfg_geti("sophia.branch_age_period"));
	sp_setint(env, "compaction.0.snapshot_period", cfg_geti("sophia.snapshot_period"));
	sp_setint(env, "log.enable", 0);
	int rc = sp_open(env);
	if (rc == -1)
		sophia_error(env);
}

void
SophiaEngine::endRecovery()
{
	if (recovery_complete)
		return;
	/* complete two-phase recovery */
	int rc = sp_open(env);
	if (rc == -1)
		sophia_error(env);
	recovery_complete = 1;
}

Handler *
SophiaEngine::open()
{
	return new SophiaSpace(this);
}

static inline void
sophia_send_row(struct relay *relay, uint32_t space_id, char *tuple,
                uint32_t tuple_size)
{
	struct recovery *r = relay->r;
	struct request_replace_body body;
	body.m_body = 0x82; /* map of two elements. */
	body.k_space_id = IPROTO_SPACE_ID;
	body.m_space_id = 0xce; /* uint32 */
	body.v_space_id = mp_bswap_u32(space_id);
	body.k_tuple = IPROTO_TUPLE;
	struct xrow_header row;
	row.type = IPROTO_INSERT;
	row.server_id = 0;
	row.lsn = vclock_inc(&r->vclock, row.server_id);
	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = tuple;
	row.body[1].iov_len = tuple_size;
	relay_send(relay, &row);
}

static inline struct key_def *
sophia_join_key_def(void *env, void *db)
{
	uint32_t id = sp_getint(db, "id");
	uint32_t count = sp_getint(db, "key-count");
	struct key_def *key_def;
	struct key_opts key_opts = key_opts_default;
	key_def = key_def_new(id, 0, "sophia_join", TREE, &key_opts, count);
	unsigned i = 0;
	while (i < count) {
		char path[64];
		int len = snprintf(path, sizeof(path), "db.%d.index.key", id);
		if (i > 0) {
			snprintf(path + len, sizeof(path) - len, "_%d", i);
		}
		char *type = (char *)sp_getstring(env, path, NULL);
		assert(type != NULL);
		if (strcmp(type, "string") == 0)
			key_def->parts[i].type = STRING;
		else
		if (strcmp(type, "u64") == 0)
			key_def->parts[i].type = NUM;
		free(type);
		key_def->parts[i].fieldno = i;
		i++;
	}
	return key_def;
}

/**
 * Relay all data that should be present in the snapshot
 * to the replica.
 */
void
SophiaEngine::join(struct relay *relay)
{
	struct vclock *res = vclockset_last(&relay->r->snap_dir.index);
	if (res == NULL)
		tnt_raise(ClientError, ER_MISSING_SNAPSHOT);
	int64_t signt = vclock_sum(res);

	/* get snapshot object */
	char id[128];
	snprintf(id, sizeof(id), "view.%" PRIu64, signt);
	void *snapshot = sp_getobject(env, id);
	assert(snapshot != NULL);

	/* iterate through a list of databases which took a
	 * part in the snapshot */
	void *db;
	void *db_cursor = sp_getobject(snapshot, "db");
	if (db_cursor == NULL)
		sophia_error(env);

	while ((db = sp_get(db_cursor, NULL)))
	{
		/* prepare space schema */
		struct key_def *key_def;
		try {
			key_def = sophia_join_key_def(env, db);
		} catch (Exception *e) {
			sp_destroy(db_cursor);
			throw;
		}
		/* send database */
		void *cursor = sp_cursor(snapshot);
		if (cursor == NULL) {
			sp_destroy(db_cursor);
			key_def_delete(key_def);
			sophia_error(env);
		}
		void *obj = sp_document(db);
		while ((obj = sp_get(cursor, obj)))
		{
			uint32_t tuple_size;
			char *tuple = (char *)sophia_tuple_new(obj, key_def, NULL, &tuple_size);
			try {
				sophia_send_row(relay, key_def->space_id, tuple, tuple_size);
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
SophiaEngine::createIndex(struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE: return new SophiaIndex(key_def);
	default:
		assert(false);
		return NULL;
	}
}

void
SophiaEngine::dropIndex(Index *index)
{
	SophiaIndex *i = (SophiaIndex *)index;
	/* schedule asynchronous drop */
	int rc = sp_drop(i->db);
	if (rc == -1)
		sophia_error(env);
	/* unref db object */
	rc = sp_destroy(i->db);
	if (rc == -1)
		sophia_error(env);
	i->db  = NULL;
	i->env = NULL;
}

void
SophiaEngine::keydefCheck(struct space *space, struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE: {
		if (! key_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "Sophia TREE index must be unique");
		}
		if (key_def->iid != 0) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "Sophia TREE secondary indexes are not supported");
		}
		const uint32_t keypart_limit = 8;
		if (key_def->part_count > keypart_limit) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
			          key_def->name,
			          space_name(space),
			          "Sophia TREE index too many key-parts (8 max)");
		}
		unsigned i = 0;
		while (i < key_def->part_count) {
			struct key_part *part = &key_def->parts[i];
			if (part->type != NUM && part->type != STRING) {
				tnt_raise(ClientError, ER_MODIFY_INDEX,
				          key_def->name,
				          space_name(space),
				          "Sophia TREE index field type must be STR or NUM");
			}
			if (part->fieldno != i) {
				tnt_raise(ClientError, ER_MODIFY_INDEX,
				          key_def->name,
				          space_name(space),
				          "Sophia TREE key-parts must follow first and cannot be sparse");
			}
			i++;
		}
		break;
	}
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  key_def->name,
			  space_name(space));
		break;
	}
}

void
SophiaEngine::begin(struct txn *txn)
{
	assert(txn->engine_tx == NULL);
	txn->engine_tx = sp_begin(env);
	if (txn->engine_tx == NULL)
		sophia_error(env);
}

void
SophiaEngine::prepare(struct txn *txn)
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
		sophia_error(env);
		break;
	}
}

void
SophiaEngine::commit(struct txn *txn, int64_t signature)
{
	if (txn->engine_tx == NULL)
		return;

	if (txn->n_rows > 0) {
		/* commit transaction using transaction commit signature */
		assert(signature >= 0);

		if (m_prev_commit_lsn == signature) {
			panic("sophia commit panic: m_prev_commit_lsn == signature = %"
			      PRIu64, signature);
		}
		/* Set tx id in Sophia only if tx has WRITE requests */
		sp_setint(txn->engine_tx, "lsn", signature);
		m_prev_commit_lsn = signature;
	}

	int rc = sp_commit(txn->engine_tx);
	if (rc == -1) {
		panic("sophia commit failed: txn->signature = %"
		      PRIu64, signature);
	}
	txn->engine_tx = NULL;
}

void
SophiaEngine::rollbackStatement(struct txn_stmt* /* stmt */)
{
	say_info("SophiaEngine::rollbackStatement()");
}

void
SophiaEngine::rollback(struct txn *txn)
{
	if (txn->engine_tx) {
		sp_destroy(txn->engine_tx);
		txn->engine_tx = NULL;
	}
}

void
SophiaEngine::beginJoin()
{
	/* put engine to recovery-complete state to
	 * correctly support join */
	endRecovery();
}

static inline void
sophia_snapshot(void *env, int64_t lsn)
{
	/* start asynchronous checkpoint */
	int rc = sp_setint(env, "scheduler.checkpoint", 0);
	if (rc == -1)
		sophia_error(env);
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "view.%" PRIu64, lsn);
	/* ensure snapshot is not already exists */
	void *o = sp_getobject(env, snapshot);
	if (o) {
		return;
	}
	snprintf(snapshot, sizeof(snapshot), "%" PRIu64, lsn);
	rc = sp_setstring(env, "view", snapshot, 0);
	if (rc == -1)
		sophia_error(env);
}

static inline void
sophia_reference_checkpoint(void *env, int64_t lsn)
{
	/* recovered snapshot lsn is >= then last
	 * engine lsn */
	char checkpoint_id[32];
	snprintf(checkpoint_id, sizeof(checkpoint_id), "%" PRIu64, lsn);
	int rc = sp_setstring(env, "view", checkpoint_id, 0);
	if (rc == -1)
		sophia_error(env);
	char snapshot[128];
	/* update lsn */
	snprintf(snapshot, sizeof(snapshot), "view.%" PRIu64 ".lsn", lsn);
	rc = sp_setint(env, snapshot, lsn);
	if (rc == -1)
		sophia_error(env);
}

static inline int
sophia_snapshot_ready(void *env, int64_t lsn)
{
	/* get sophia lsn associated with snapshot */
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "view.%" PRIu64 ".lsn", lsn);
	int64_t snapshot_start_lsn = sp_getint(env, snapshot);
	if (snapshot_start_lsn == -1) {
		if (sp_error(env))
			sophia_error(env);
		panic("sophia snapshot %" PRIu64 " does not exist", lsn);
	}
	/* compare with a latest completed checkpoint lsn */
	int64_t last_lsn = sp_getint(env, "scheduler.checkpoint_lsn_last");
	return last_lsn >= snapshot_start_lsn;
}

static inline void
sophia_delete_checkpoint(void *env, int64_t lsn)
{
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "view.%" PRIu64, lsn);
	void *s = sp_getobject(env, snapshot);
	if (s == NULL) {
		if (sp_error(env))
			sophia_error(env);
		panic("sophia snapshot %" PRIu64 " does not exist", lsn);
	}
	int rc = sp_destroy(s);
	if (rc == -1)
		sophia_error(env);
}

void
SophiaEngine::recoverToCheckpoint(int64_t checkpoint_id)
{
	/*
	 * Create a reference to the "current" snapshot,
	 * to ensure correct reference counting when a new
	 * snapshot is created.
	 * Sophia doesn't store snapshot references persistently,
	 * so, after recovery, we remember the reference to the
	 * "previous" snapshot, so that when the next snapshot is
	 * taken, this reference is garbage collected. This
	 * will also prevent this snapshot from accidental
	 * garbage collection before a new snapshot is created,
	 * and thus ensure correct crash recovery in case of crash
	 * in the period between startup and creation of the first
	 * snapshot.
	 */
	sophia_reference_checkpoint(env, checkpoint_id);
	m_prev_checkpoint_lsn = checkpoint_id;
}

int
SophiaEngine::beginCheckpoint(int64_t lsn)
{
	assert(m_checkpoint_lsn == -1);
	if (lsn != m_prev_checkpoint_lsn) {
		sophia_snapshot(env, lsn);
		m_checkpoint_lsn = lsn;
		return 0;
	}
	errno = EEXIST;
	return -1;
}

int
SophiaEngine::waitCheckpoint()
{
	assert(m_checkpoint_lsn != -1);
	if (! worker_pool_run)
		return 0;
	while (! sophia_snapshot_ready(env, m_checkpoint_lsn))
		fiber_yield_timeout(.020);
	return 0;
}

void
SophiaEngine::commitCheckpoint()
{
	if (m_prev_checkpoint_lsn >= 0)
		sophia_delete_checkpoint(env, m_prev_checkpoint_lsn);
	m_prev_checkpoint_lsn = m_checkpoint_lsn;
	m_checkpoint_lsn = -1;
}

void
SophiaEngine::abortCheckpoint()
{
	if (m_checkpoint_lsn >= 0) {
		sophia_delete_checkpoint(env, m_checkpoint_lsn);
		m_checkpoint_lsn = -1;
	}
}
