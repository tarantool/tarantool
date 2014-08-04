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
#include "txn.h"
#include "tuple.h"
#include "engine.h"
#include "engine_sophia.h"
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
#include <errno.h>

struct Sophia: public Engine {
	Sophia(EngineFactory*);
};

Sophia::Sophia(EngineFactory *e)
	:Engine(e)
{ }

static struct tuple *
sophia_replace_noop(struct space*,
                    struct tuple*, struct tuple*,
                    enum dup_replace_mode)
{
	return NULL;
}

static void
sophia_end_build_primary_key(struct space *space)
{
	engine_recovery *r = &space->engine->recovery;
	/* enable replace */
	r->state = READY_ALL_KEYS;
	r->replace = space_replace_primary_key;
	r->recover = space_noop;
}

static void
sophia_begin_build_primary_key(struct space *space)
{
	engine_recovery *r = &space->engine->recovery;
	r->replace = sophia_replace_noop;
	r->recover = sophia_end_build_primary_key;
}

static inline void
sophia_recovery_prepare(struct engine_recovery *r)
{
	r->state   = READY_NO_KEYS;
	r->recover = sophia_begin_build_primary_key;
	r->replace = space_replace_no_keys;
}

SophiaFactory::SophiaFactory()
	:EngineFactory("sophia")
{
	sophia_recovery_prepare(&recovery);
}

void
SophiaFactory::init()
{
	env = sp_env();
	if (env == NULL)
		panic("failed to create sophia environment");
	void *conf = sp_ctl(env, "conf");
	sp_set(conf, "env.dir", "sophia");
	int rc = sp_open(env);
	if (rc == -1)
		panic("sophia recovery failed");
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
		recovery.replace = sophia_replace_noop;
		break;
	case END_RECOVERY:
		recovery.state   = READY_NO_KEYS;
		recovery.replace = space_replace_primary_key;
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

void
SophiaFactory::dropIndex(Index *index)
{
	(void)index;
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
SophiaFactory::txnFinish(struct txn *txn)
{
	/**
	 * @todo: support multi-statement transactions
	 * here when sophia supports them.
	 */

	/* single-stmt case:
	 *
	 * no need to unref tuple here, since it will be done by
	 * TupleGuard in execute_replace().
	*/
	(void)txn;
#if 0
	struct txn_stmt *stmt = txn_stmt(txn);
	if (stmt->new_tuple)
		tuple_ref(stmt->new_tuple, -1);
#endif
}
