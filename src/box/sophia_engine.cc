/*
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
#include "sophia_engine.h"
#include "cfg.h"
#include "xrow.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "txn.h"
#include "index.h"
#include "sophia_index.h"
#include "space.h"
#include "salad/rlist.h"
#include <sophia.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

void sophia_raise(void *env)
{
	void *c = sp_ctl(env);
	void *o = sp_get(c, "sophia.error");
	char *error = (char *)sp_get(o, "value", NULL);
	auto scoped_guard =
		make_scoped_guard([=] { sp_destroy(o); });
	tnt_raise(ClientError, ER_SOPHIA, error);
}

void sophia_info(void (*callback)(const char*, const char*, void*), void *arg)
{
	SophiaEngine *engine = (SophiaEngine*)engine_find("sophia");
	void *env = engine->env;
	void *c = sp_ctl(env);
	void *o, *cur = sp_cursor(c);
	if (cur == NULL)
		sophia_raise(env);
	while ((o = sp_get(cur))) {
		const char *k = (const char *)sp_get(o, "key", NULL);
		const char *v = (const char *)sp_get(o, "value", NULL);
		callback(k, v, arg);
	}
	sp_destroy(cur);
}

struct SophiaSpace: public Handler {
	SophiaSpace(Engine*);
};

SophiaSpace::SophiaSpace(Engine *e)
	:Handler(e)
{ }

static void
sophia_recovery_end(struct space *space)
{
	engine_recovery *r = &space->handler->recovery;
	r->state   = READY_ALL_KEYS;
	r->replace = sophia_replace;
	r->recover = space_noop;
	/*
	sophia_complete_recovery(space);
	*/
}

static void
sophia_recovery_end_snapshot(struct space *space)
{
	engine_recovery *r = &space->handler->recovery;
	r->state   = READY_PRIMARY_KEY;
	r->recover = sophia_recovery_end;
}

static void
sophia_recovery_begin_snapshot(struct space *space)
{
	engine_recovery *r = &space->handler->recovery;
	r->recover = sophia_recovery_end_snapshot;
}

SophiaEngine::SophiaEngine()
	:Engine("sophia")
	 ,m_prev_checkpoint_lsn(-1)
	 ,m_checkpoint_lsn(-1)
{
	flags = 0;
	env = NULL;
	recovery.state   = READY_NO_KEYS;
	recovery.recover = sophia_recovery_begin_snapshot;
	recovery.replace = sophia_replace_recover;
}

void
SophiaEngine::init()
{
	env = sp_env();
	if (env == NULL)
		panic("failed to create sophia environment");
	void *c = sp_ctl(env);
	sp_set(c, "sophia.path", cfg_gets("sophia_dir"));
	sp_set(c, "sophia.path_create", "0");
	sp_set(c, "scheduler.threads", cfg_gets("sophia.threads"));
	sp_set(c, "memory.limit", cfg_gets("sophia.memory_limit"));
	sp_set(c, "compaction.node_size", cfg_gets("sophia.node_size"));
	sp_set(c, "compaction.page_size", cfg_gets("sophia.page_size"));
	sp_set(c, "log.enable", "0");
	sp_set(c, "log.two_phase_recover", "1");
	sp_set(c, "log.commit_lsn", "1");
	int rc = sp_open(env);
	if (rc == -1)
		sophia_raise(env);
}

Handler *
SophiaEngine::open()
{
	return new SophiaSpace(this);
}

void
SophiaEngine::end_recover_snapshot()
{
	recovery.replace = sophia_replace_recover;
	recovery.recover = sophia_recovery_end_snapshot;
}

static inline void
sophia_snapshot_recover(void *env, int64_t lsn);

void
SophiaEngine::end_recovery()
{
	/* create snapshot reference after tarantool
	 * recovery, to ensure correct ref
	 * counting. */
	if (m_checkpoint_lsn >= 0) {
		sophia_snapshot_recover(env, m_checkpoint_lsn);
		m_prev_checkpoint_lsn = m_checkpoint_lsn;
		m_checkpoint_lsn = -1;
	}
	/* complete two-phase recovery */
	int rc = sp_open(env);
	if (rc == -1)
		sophia_raise(env);
	recovery.state   = READY_NO_KEYS;
	recovery.replace = sophia_replace;
	recovery.recover = space_noop;
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
	SophiaIndex *i = (SophiaIndex*)index;
	/* schedule asynchronous drop */
	int rc = sp_drop(i->db);
	if (rc == -1)
		sophia_raise(env);
	/* unref db object */
	rc = sp_destroy(i->db);
	if (rc == -1)
		sophia_raise(env);
	/* maybe start asynchronous database
	 * shutdown: last snapshot might hold a
	 * db pointer. */
	rc = sp_destroy(i->db);
	if (rc == -1)
		sophia_raise(env);
	i->db  = NULL;
	i->env = NULL;
}

void
SophiaEngine::keydefCheck(struct space *space, struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE:
		if (! key_def->is_unique) {
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
		if (key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "Sophia TREE index key can not be multipart");
		}
		if (key_def->parts[0].type != NUM &&
		    key_def->parts[0].type != STRING) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "Sophia TREE index field type must be STR or NUM");
		}
		break;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  key_def->name,
			  space_name(space));
		break;
	}
}

void
SophiaEngine::begin(struct txn *txn, struct space *space)
{
	assert(space->handler->engine == this);
	if (txn->n_stmts == 1) {
		assert(txn->engine_tx == NULL);
		SophiaIndex *index = (SophiaIndex *)index_find(space, 0);
		(void) index;
		assert(index->db != NULL);
		txn->engine_tx = sp_begin(env);
		if (txn->engine_tx == NULL)
			sophia_raise(env);
		return;
	}
	assert(txn->engine_tx != NULL);
}

void
SophiaEngine::commit(struct txn *txn)
{
	if (txn->engine_tx == NULL)
		return;
	/* free involved tuples */
	struct txn_stmt *stmt;
	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if (! stmt->new_tuple)
			continue;
		assert(stmt->new_tuple->refs >= 1 &&
		       stmt->new_tuple->refs < UINT8_MAX);
		tuple_unref(stmt->new_tuple);
	}
	auto scoped_guard = make_scoped_guard([=] {
		txn->engine_tx = NULL;
	});
	/* commit transaction using transaction
	 * commit signature */
	assert(txn->signature >= 0);
	int rc = sp_prepare(txn->engine_tx, txn->signature);
	assert(rc == 0);
	if (rc == -1)
		sophia_raise(env);
	rc = sp_commit(txn->engine_tx);
	if (rc == -1)
		sophia_raise(env);
	assert(rc == 0);
}

void
SophiaEngine::rollback(struct txn *txn)
{
	if (txn->engine_tx == NULL)
		return;
	sp_destroy(txn->engine_tx);
	txn->engine_tx = NULL;
}

static inline void
sophia_snapshot(void *env, int64_t lsn)
{
	/* start asynchronous checkpoint */
	void *c = sp_ctl(env);
	int rc = sp_set(c, "scheduler.checkpoint");
	if (rc == -1)
		sophia_raise(env);
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "snapshot.%" PRIu64, lsn);
	/* ensure snapshot is not already exists */
	void *o = sp_get(c, snapshot);
	if (o) {
		return;
	}
	snprintf(snapshot, sizeof(snapshot), "%" PRIu64, lsn);
	rc = sp_set(c, "snapshot", snapshot);
	if (rc == -1)
		sophia_raise(env);
}

static inline void
sophia_snapshot_recover(void *env, int64_t lsn)
{
	/* recovered snapshot lsn is >= then last
	 * engine lsn */
	char snapshot_lsn[32];
	snprintf(snapshot_lsn, sizeof(snapshot_lsn), "%" PRIu64, lsn);
	void *c = sp_ctl(env);
	int rc = sp_set(c, "snapshot", snapshot_lsn);
	if (rc == -1)
		sophia_raise(env);
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "snapshot.%" PRIu64 ".lsn", lsn);
	rc = sp_set(c, snapshot, snapshot_lsn);
	if (rc == -1)
		sophia_raise(env);
}

static inline int
sophia_snapshot_ready(void *env, int64_t lsn)
{
	/* get sophia lsn associated with snapshot */
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "snapshot.%" PRIu64 ".lsn", lsn);
	void *c = sp_ctl(env);
	void *o = sp_get(c, snapshot);
	if (o == NULL) {
		if (sp_error(env))
			sophia_raise(env);
		panic("sophia snapshot %" PRIu64 " does not exist", lsn);
	}
	char *pe;
	char *p = (char *)sp_get(o, "value", NULL);
	int64_t snapshot_start_lsn = strtoull(p, &pe, 10);
	sp_destroy(o);

	/* compare with a latest completed checkpoint lsn */
	o = sp_get(c, "scheduler.checkpoint_lsn_last");
	if (o == NULL)
		sophia_raise(env);
	p = (char *)sp_get(o, "value", NULL);
	int64_t last_lsn = strtoull(p, &pe, 10);
	sp_destroy(o);
	return last_lsn >= snapshot_start_lsn;
}

static inline void
sophia_delete_checkpoint(void *env, int64_t lsn)
{
	char snapshot[128];
	snprintf(snapshot, sizeof(snapshot), "snapshot.%" PRIu64, lsn);
	void *c = sp_ctl(env);
	void *s = sp_get(c, snapshot);
	if (s == NULL) {
		if (sp_error(env))
			sophia_raise(env);
		panic("sophia snapshot %" PRIu64 " does not exist", lsn);
	}
	int rc = sp_destroy(s);
	if (rc == -1)
		sophia_raise(env);
}

void
SophiaEngine::begin_recover_snapshot(int64_t lsn)
{
	m_checkpoint_lsn = lsn;
}

int
SophiaEngine::begin_checkpoint(int64_t lsn)
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
SophiaEngine::wait_checkpoint()
{
	assert(m_checkpoint_lsn != -1);
	while (! sophia_snapshot_ready(env, m_checkpoint_lsn))
		fiber_yield_timeout(.020);
	return 0;
}

void
SophiaEngine::commit_checkpoint()
{
	if (m_prev_checkpoint_lsn >= 0)
		sophia_delete_checkpoint(env, m_prev_checkpoint_lsn);
	m_prev_checkpoint_lsn = m_checkpoint_lsn;
	m_checkpoint_lsn = -1;
}

void
SophiaEngine::abort_checkpoint()
{
	if (m_checkpoint_lsn >= 0) {
		sophia_delete_checkpoint(env, m_checkpoint_lsn);
		m_checkpoint_lsn = -1;
	}
}

int sophia_schedule(void)
{
	SophiaEngine *engine = (SophiaEngine *)engine_find("sophia");
	assert(engine->env != NULL);
	void *c = sp_ctl(engine->env);
	return sp_set(c, "scheduler.run");
}
