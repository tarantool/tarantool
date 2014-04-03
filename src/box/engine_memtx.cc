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
#include "tuple.h"
#include "engine.h"
#include "engine_memtx.h"
#include "index.h"
#include "hash_index.h"
#include "tree_index.h"
#include "bitset_index.h"
#include "space.h"
#include "exception.h"
#include "salad/rlist.h"
#include <stdlib.h>
#include <string.h>

struct Memtx: public Engine {
	Memtx(EngineFactory *e)
		: Engine(e)
	{ }
	virtual ~Memtx()
	{
		/* do nothing */
		/* factory->close(this); */
	}
};

/**
 * This is a vtab with which a newly created space which has no
 * keys is primed.
 * At first it is set to correctly work for spaces created during
 * recovery from snapshot. In process of recovery it is updated as
 * below:
 *
 * 1) after SNAP is loaded:
 *    recover = space_build_primary_key
 * 2) when all XLOGs are loaded:
 *    recover = space_build_all_keys
*/

static inline void
memtx_recovery_prepare(struct engine_recovery *r)
{
	r->state   = READY_NO_KEYS;
	r->recover = space_begin_build_primary_key;
	r->replace = space_replace_no_keys;
}

MemtxFactory::MemtxFactory()
	:EngineFactory("memtx")
{
	memtx_recovery_prepare(&recovery);
}

void
MemtxFactory::recoveryEvent(enum engine_recovery_event event)
{
	switch (event) {
	case END_RECOVERY_SNAPSHOT:
		recovery.recover = space_build_primary_key;
		break;
	case END_RECOVERY:
		recovery.recover = space_build_all_keys;
		break;
	}
}

Engine *MemtxFactory::open()
{
	return new Memtx(this);
}

Index *
MemtxFactory::createIndex(struct key_def *key_def)
{
	switch (key_def->type) {
	case HASH:
		return new HashIndex(key_def);
	case TREE:
		return new TreeIndex(key_def);
	case BITSET:
		return new BitsetIndex(key_def);
	default:
		assert(false);
		return NULL;
	}
}

void
MemtxFactory::keydefCheck(struct key_def *key_def)
{
	switch (key_def->type) {
	case HASH:
		if (! key_def->is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "HASH index must be unique");
		}
		break;
	case TREE:
		/* TREE index has no limitations. */
		break;
	case BITSET:
		if (key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "BITSET index key can not be multipart");
		}
		if (key_def->is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "BITSET can not be unique");
		}
		break;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id);
		break;
	}
}
