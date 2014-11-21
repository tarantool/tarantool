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
#include "engine_sophia.h"
#include "txn.h"
#include "index.h"
#include "sophia_index.h"
#include "space.h"
#include "exception.h"
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
	void *o, *cur = sp_cursor(c, ">=", NULL);
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
	sophia_complete_recovery(space);
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
	env   = NULL;
	tx    = NULL;
	tx_db = NULL;
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
SophiaFactory::recoveryEvent(enum engine_recovery_event event)
{
	switch (event) {
	case END_RECOVERY_SNAPSHOT:
		recovery.replace = sophia_replace_recover;
		recovery.recover = sophia_recovery_end_snapshot;
		break;
	case END_RECOVERY:
		recovery.state   = READY_NO_KEYS;
		recovery.replace = sophia_replace;
		recovery.recover = space_noop;
		break;
	}
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
		break;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id);
		break;
	}
}

void
SophiaFactory::begin(struct txn * txn, struct space *space)
{
	assert(space->engine->factory == this);
	if (txn->n_stmts == 1) {
		assert(tx == NULL);
		SophiaIndex *index = (SophiaIndex *)index_find(space, 0);
		assert(index->db != NULL);
		tx = sp_begin(index->db);
		if (tx == NULL)
			sophia_raise(env);
		tx_db = index->db;
		return;
	}
	assert(tx != NULL);
	SophiaIndex *index = (SophiaIndex *)index_find(space, 0);
	if (index->db != tx_db) {
		tnt_raise(ClientError, ER_SOPHIA,
		           "only one sophia space can be used in "
		           "a multi-statement transaction");
	}
}

void
SophiaFactory::commit(struct txn *txn)
{
	if (tx == NULL)
		return;
	auto scoped_guard = make_scoped_guard([=] {
		tx = NULL;
		tx_db = NULL;
	});

	/* a. prepare transaction for commit */
	int rc = sp_prepare(tx);
	if (rc == -1)
		sophia_raise(env);
	assert(rc == 0);

	/* b. create transaction log cursor and
	 *    forge each statement's LSN number.
	*/
	void *lc = sp_ctl(tx, "log_cursor");
	if (lc == NULL) {
		sp_rollback(tx);
		sophia_raise(env);
	}
	struct txn_stmt *stmt;
	rlist_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->new_tuple == NULL && stmt->old_tuple == NULL)
			continue;
		void *v = sp_get(lc);
		assert(v != NULL);
		sp_set(v, "lsn", stmt->row->lsn);
		/* remove tuple reference */
		if (stmt->new_tuple) {
			/* 2 refs: iproto case */
			/* 3 refs: lua case */
			assert(stmt->new_tuple->refs >= 2);
			tuple_unref(stmt->new_tuple);
		}
	}
	assert(sp_get(lc) == NULL);
	sp_destroy(lc);

	/* c. commit transaction */
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
		tx_db = NULL;
	});
	sp_rollback(tx);
}
