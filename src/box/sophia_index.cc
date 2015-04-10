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
#include "sophia_index.h"
#include "say.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "errinj.h"
#include "schema.h"
#include "space.h"
#include "txn.h"
#include "cfg.h"
#include "sophia_engine.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sophia.h>
#include <stdio.h>
#include <inttypes.h>

static inline int
sophia_index_stmt(void *tx, void *db, int del, struct key_def *key_def, struct tuple *tuple)
{
	const char *key = tuple_field(tuple, key_def->parts[0].fieldno);
	const char *keyptr = key;
	mp_next(&keyptr);
	size_t keysize = keyptr - key;
	void *o = sp_object(db);
	if (o == NULL)
		return -1;
	sp_set(o, "key", key, keysize);
	if (del) {
		return sp_delete(tx, o);
	}
	sp_set(o, "value", tuple->data, tuple->bsize);
	return sp_set(tx, o);
}

static struct tuple*
sophia_index_get(void *env, void *db, void *tx, const char *key, size_t keysize,
                 struct tuple_format *format)
{
	void *o = sp_object(db);
	if (o == NULL)
		sophia_raise(env);
	sp_set(o, "key", key, keysize);
	void *result = sp_get((tx) ? tx: db, o);
	if (result == NULL)
		return NULL;
	auto scoped_guard =
		make_scoped_guard([=] { sp_destroy(result); });
	int valuesize = 0;
	void *value = sp_get(result, "value", &valuesize);
	return tuple_new(format, (char*)value, (char*)value + valuesize);
}

static inline int
sophia_index_compare(char *a, size_t asz __attribute__((unused)),
                     char *b, size_t bsz __attribute__((unused)),
                     void *arg)
{
	struct key_def *key_def = (struct key_def*)arg;

	int rc = tuple_compare_field(a, b, key_def->parts[0].type);
	return (rc == 0) ? 0 :
	       ((rc > 0) ? 1 : -1);
}

static inline void*
sophia_configure(struct space *space, struct key_def *key_def)
{
	SophiaEngine *engine =
		(SophiaEngine*)space->handler->engine;
	void *env = engine->env;
	void *c = sp_ctl(env);
	char pointer[128];
	char pointer_arg[128];
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32, key_def->space_id);
	sp_set(c, "db", name);
	snprintf(name, sizeof(name), "db.%" PRIu32 ".index.cmp",
	         key_def->space_id);
	snprintf(pointer, sizeof(pointer), "pointer: %p", (void*)sophia_index_compare);
	snprintf(pointer_arg, sizeof(pointer_arg), "pointer: %p", (void*)key_def);
	sp_set(c, name, pointer, pointer_arg);
	snprintf(name, sizeof(name), "db.%" PRIu32 ".compression", key_def->space_id);
	sp_set(c, name, cfg_gets("sophia.compression"));
	snprintf(name, sizeof(name), "db.%" PRIu32, key_def->space_id);
	void *db = sp_get(c, name);
	if (db == NULL)
		sophia_raise(env);
	return db;
}

SophiaIndex::SophiaIndex(struct key_def *key_def_arg __attribute__((unused)))
	: Index(key_def_arg)
{
	struct space *space = space_cache_find(key_def->space_id);
	SophiaEngine *engine =
		(SophiaEngine*)space->handler->engine;
	env = engine->env;
	db = sophia_configure(space, key_def);
	if (db == NULL)
		sophia_raise(env);
	/* start two-phase recovery for a space:
	 * a. created after snapshot recovery
	 * b. created during log recovery
	*/
	int rc = sp_open(db);
	if (rc == -1)
		sophia_raise(env);
	tuple_format_ref(space->format, 1);
}

SophiaIndex::~SophiaIndex()
{
	if (m_position != NULL) {
		m_position->free(m_position);
		m_position = NULL;
	}
	if (db) {
		int rc = sp_destroy(db);
		if (rc == 0)
			return;
		void *c = sp_ctl(env);
		void *o = sp_get(c, "sophia.error");
		char *error = (char *)sp_get(o, "value", NULL);
		say_info("sophia space %d close error: %s",
		         key_def->space_id, error);
		sp_destroy(o);
	}
}

struct tuple *
SophiaIndex::random(uint32_t rnd) const
{
	void *o = sp_object(db);
	if (o == NULL)
		sophia_raise(env);
	sp_set(o, "key", &rnd, sizeof(rnd));
	sp_set(o, "order", "random");
	void *c = sp_cursor(db, o);
	if (c == NULL)
		sophia_raise(env);
	auto scoped_guard =
		make_scoped_guard([=] { sp_destroy(c); });
	o = sp_get(c);
	if (o == NULL)
		return NULL;
	struct space *space = space_cache_find(key_def->space_id);
	int valuesize;
	void *value = sp_get(o, "value", &valuesize);
	return tuple_new(space->format, (char*)value,
	                 (char*)value + valuesize);
}

size_t
SophiaIndex::size() const
{
	void *c = sp_ctl(env);
	char name[128];
	snprintf(name, sizeof(name), "db.%" PRIu32 ".index.count",
	         key_def->space_id);
	void *o = sp_get(c, name);
	if (o == NULL)
		sophia_raise(env);
	uint64_t count = atoi((const char *)sp_get(o, "value", NULL));
	sp_destroy(o);
	return count;
}

size_t
SophiaIndex::memsize() const
{
	void *c = sp_ctl(env);
	char name[128];
	snprintf(name, sizeof(name), "db.%" PRIu32 ".index.memory_used",
	         key_def->space_id);
	void *o = sp_get(c, name);
	if (o == NULL)
		sophia_raise(env);
	uint64_t used = atoi((const char *)sp_get(o, "value", NULL));
	sp_destroy(o);
	return used;
}

struct tuple *
SophiaIndex::findByKey(const char *key, uint32_t part_count) const
{
	(void) part_count;
	assert(part_count == 1);
	assert(key_def->is_unique && part_count == key_def->part_count);
	const char *keyptr = key;
	mp_next(&keyptr);
	size_t keysize = keyptr - key;
	struct space *space = space_cache_find(key_def->space_id);
	void *tx = in_txn() ? in_txn()->engine_tx : NULL;
	return sophia_index_get(env, db, tx, key, keysize, space->format);
}

struct tuple *
SophiaIndex::replace(struct tuple *old_tuple, struct tuple *new_tuple,
                     enum dup_replace_mode mode)
{
	struct space *space = space_cache_find(key_def->space_id);
	struct txn *txn = in_txn();
	assert(txn != NULL && txn->engine_tx != NULL);

	/* do not involve in tarantool transaction regarding old_tuple,
	 * always return NULL.
	*/

	/* delete */
	int rc;
	if (old_tuple && new_tuple == NULL) {
		assert(old_tuple != NULL);
		rc = sophia_index_stmt(txn->engine_tx, db, 1, key_def,
				       old_tuple);
		if (rc == -1)
			sophia_raise(env);
		return NULL;
	}

	/* update */
	if (old_tuple && new_tuple) {
		/* assume no primary key update is supported */
		rc = sophia_index_stmt(txn->engine_tx, db, 0, key_def,
				       new_tuple);
		if (rc == -1)
			sophia_raise(env);
		return NULL;
	}

	/* insert or replace */
	switch (mode) {
	case DUP_INSERT: {
		const char *key = tuple_field(new_tuple, key_def->parts[0].fieldno);
		const char *keyptr = key;
		mp_next(&keyptr);
		size_t keysize = keyptr - key;
		struct tuple *dup_tuple =
			sophia_index_get(env, db, txn->engine_tx, key,
					 keysize, space->format);
		if (dup_tuple) {
			tuple_ref(dup_tuple);
			int error = 0;
			if (tuple_compare(dup_tuple, new_tuple, key_def) == 0)
				error = 1;
			tuple_unref(dup_tuple);
			if (error) {
				struct space *sp =
					space_cache_find(key_def->space_id);
				tnt_raise(ClientError, ER_TUPLE_FOUND,
					  index_name(this), space_name(sp));
			}
		}
	}
	case DUP_REPLACE_OR_INSERT:
		rc = sophia_index_stmt(txn->engine_tx, db, 0, key_def,
				       new_tuple);
		if (rc == -1)
			sophia_raise(env);
		break;
	case DUP_REPLACE:
	default:
		assert(0);
		break;
	}
	return NULL;
}

struct sophia_iterator {
	struct iterator base;
	const char *key;
	int keysize;
	uint32_t part_count;
	struct space *space;
	void *env;
	void *db;
	void *cursor;
	void *tx;
};

void
sophia_iterator_free(struct iterator *ptr)
{
	assert(ptr->free == sophia_iterator_free);
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	if (it->cursor)
		sp_destroy(it->cursor);
	free(ptr);
}

void
sophia_iterator_close(struct iterator *ptr)
{
	assert(ptr->free == sophia_iterator_free);
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	if (it->cursor) {
		sp_destroy(it->cursor);
		it->cursor = NULL;
	}
}

struct tuple *
sophia_iterator_next(struct iterator *ptr)
{
	assert(ptr->next == sophia_iterator_next);
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	assert(it->cursor != NULL);
	void *o = sp_get(it->cursor);
	if (o == NULL)
		return NULL;
	int valuesize = 0;
	const char *value = (const char*)sp_get(o, "value", &valuesize);
	return tuple_new(it->space->format, value, value + valuesize);
}

struct tuple *
sophia_iterator_last(struct iterator *ptr __attribute__((unused)))
{
	return NULL;
}

struct tuple *
sophia_iterator_eq(struct iterator *ptr)
{
	ptr->next = sophia_iterator_last;
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	assert(it->cursor == NULL);
	return sophia_index_get(it->env, it->db, it->tx, it->key, it->keysize,
	                        it->space->format);
}

struct iterator *
SophiaIndex::allocIterator() const
{
	struct sophia_iterator *it =
		(struct sophia_iterator *) calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
		          sizeof(struct sophia_iterator), "SophiaIndex",
		          "iterator");
	}
	it->base.next  = sophia_iterator_next;
	it->base.close = sophia_iterator_close;
	it->base.free  = sophia_iterator_free;
	it->cursor = NULL;
	return (struct iterator *) it;
}

void
SophiaIndex::initIterator(struct iterator *ptr,
                          enum iterator_type type,
                          const char *key, uint32_t part_count) const
{
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	assert(it->cursor == NULL);
	size_t keysize;
	if (part_count > 0) {
		const char *keyptr = key;
		mp_next(&keyptr);
		keysize = keyptr - key;
	} else {
		keysize = 0;
		key = NULL;
	}
	it->key = key;
	it->keysize = keysize;
	it->part_count = part_count;
	it->env = env;
	it->db = db;
	it->space = space_cache_find(key_def->space_id);
	it->tx = NULL;
	const char *compare;
	switch (type) {
	case ITER_EQ:
		it->base.next = sophia_iterator_eq;
		it->tx = in_txn() ? in_txn()->engine_tx : NULL;
		return;
	case ITER_ALL:
	case ITER_GE: compare = ">=";
		break;
	case ITER_GT: compare = ">";
		break;
	case ITER_LE: compare = "<=";
		break;
	case ITER_LT: compare = "<";
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
		          "SophiaIndex", "requested iterator type");
	}
	it->base.next = sophia_iterator_next;
	void *o = sp_object(db);
	if (o == NULL)
		sophia_raise(env);
	sp_set(o, "order", compare);
	if (key)
		sp_set(o, "key", key, keysize);
	it->cursor = sp_cursor(db, o);
	if (it->cursor == NULL)
		sophia_raise(env);
}
