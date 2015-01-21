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
#include "cfg.h"
#include "xrow.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "engine.h"
#include "sophia_engine.h"
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
	SophiaFactory *factory = (SophiaFactory*)engine_find("sophia");
	void *env = factory->env;
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

struct Sophia: public Engine {
	Sophia(EngineFactory*);
};

Sophia::Sophia(EngineFactory *e)
	:Engine(e)
{ }

static void
sophia_recovery_end(struct space *space)
{
	engine_recovery *r = &space->engine->recovery;
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
	engine_recovery *r = &space->engine->recovery;
	r->state   = READY_PRIMARY_KEY;
	r->recover = sophia_recovery_end;
}

static void
sophia_recovery_begin_snapshot(struct space *space)
{
	engine_recovery *r = &space->engine->recovery;
	r->recover = sophia_recovery_end_snapshot;
}

SophiaFactory::SophiaFactory()
	:EngineFactory("sophia")
{
	flags = ENGINE_TRANSACTIONAL;
	env = NULL;
	tx  = NULL;
	recovery.state   = READY_NO_KEYS;
	recovery.recover = sophia_recovery_begin_snapshot;
	recovery.replace = sophia_replace_recover;
}

void
SophiaFactory::init()
{
	env = sp_env();
	if (env == NULL)
		panic("failed to create sophia environment");
	void *c = sp_ctl(env);
	sp_set(c, "sophia.path", cfg_gets("sophia_dir"));
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

Engine*
SophiaFactory::open()
{
	return new Sophia(this);
}

void
SophiaFactory::end_recover_snapshot()
{
	recovery.replace = sophia_replace_recover;
	recovery.recover = sophia_recovery_end_snapshot;
}


void
SophiaFactory::end_recovery()
{
	/* complete two-phase recovery */
	int rc = sp_open(env);
	if (rc == -1)
		sophia_raise(env);
	recovery.state   = READY_NO_KEYS;
	recovery.replace = sophia_replace;
	recovery.recover = space_noop;
}

Index*
SophiaFactory::createIndex(struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE: return new SophiaIndex(key_def);
	default:
		assert(false);
		return NULL;
	}
}

static inline int
drop_repository(char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL)
		return -1;
	char file[1024];
	struct dirent *de;
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(file, sizeof(file), "%s/%s", path, de->d_name);
		int rc = unlink(file);
		if (rc == -1) {
			closedir(dir);
			return -1;
		}
	}
	closedir(dir);
	return rmdir(path);
}

void
SophiaFactory::dropIndex(Index *index)
{
	SophiaIndex *i = (SophiaIndex*)index;
	int rc = sp_destroy(i->db);
	if (rc == -1)
		sophia_raise(env);
	i->db  = NULL;
	i->env = NULL;
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%" PRIu32,
	         cfg_gets("sophia_dir"), index->key_def->space_id);
	drop_repository(path);
}

void
SophiaFactory::keydefCheck(struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE:
		if (! key_def->is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "Sophia TREE index must be unique");
		}
		if (key_def->iid != 0) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "Sophia TREE secondary indexes are not supported");
		}
		if (key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "Sophia TREE index key can not be multipart");
		}
		if (key_def->parts[0].type != NUM &&
		    key_def->parts[0].type != STRING) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "Sophia TREE index field type must be STR or NUM");
		}
		break;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id);
		break;
	}
}

void
SophiaFactory::begin(struct txn *txn, struct space *space)
{
	assert(space->engine->factory == this);
	if (txn->n_stmts == 1) {
		assert(tx == NULL);
		SophiaIndex *index = (SophiaIndex *)index_find(space, 0);
		(void) index;
		assert(index->db != NULL);
		tx = sp_begin(env);
		if (tx == NULL)
			sophia_raise(env);
		return;
	}
	assert(tx != NULL);
}

void
SophiaFactory::commit(struct txn *txn)
{
	if (tx == NULL)
		return;
	auto scoped_guard = make_scoped_guard([=] {
		tx = NULL;
	});

	/* a. get max lsn for commit */
	int64_t lsn = 0;
	struct txn_stmt *stmt;
	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->row->lsn > lsn)
			lsn = stmt->row->lsn;
	}

	/* b. commit transaction */
	int rc = sp_prepare(tx, lsn);
	assert(rc == 0);
	if (rc == -1)
		sophia_raise(env);
	rc = sp_commit(tx);
	if (rc == -1)
		sophia_raise(env);
	assert(rc == 0);
}

void
SophiaFactory::rollback(struct txn *)
{
	if (tx == NULL)
		return;
	auto scoped_guard = make_scoped_guard([=] {
		tx = NULL;
	});
	sp_rollback(tx);
}

static inline void
sophia_snapshot(void *env, int64_t lsn)
{
	/* start asynchronous checkpoint */
	void *c = sp_ctl(env);
	int rc = sp_set(c, "scheduler.checkpoint");
	if (rc == -1)
		sophia_raise(env);
	char snapshot[32];
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
	char snapshot[32];
	snprintf(snapshot, sizeof(snapshot), "snapshot.%" PRIu64 ".lsn", lsn);
	rc = sp_set(c, snapshot, snapshot_lsn);
	if (rc == -1)
		sophia_raise(env);
}

static inline int
sophia_snapshot_ready(void *env, int64_t lsn)
{
	/* get sophia lsn associated with snapshot */
	char snapshot[32];
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
sophia_snapshot_delete(void *env, int64_t lsn)
{
	char snapshot[32];
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
SophiaFactory::begin_recover_snapshot(int64_t lsn)
{
	sophia_snapshot_recover(env, lsn);
}

void
SophiaFactory::snapshot(enum engine_snapshot_event e, int64_t lsn)
{
	switch (e) {
	case SNAPSHOT_START:
		sophia_snapshot(env, lsn);
		break;
	case SNAPSHOT_WAIT:
		while (! sophia_snapshot_ready(env, lsn))
			fiber_yield_timeout(.020);
		break;
	case SNAPSHOT_DELETE:
		sophia_snapshot_delete(env, lsn);
		break;
	}
}
