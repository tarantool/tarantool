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
#include "say.h"
#include "tuple.h"
#include "tuple_update.h"
#include "scoped_guard.h"
#include "errinj.h"
#include "schema.h"
#include "space.h"
#include "txn.h"
#include "cfg.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sophia.h>
#include <stdio.h>
#include <inttypes.h>
#include <bit/bit.h> /* load/store */

void*
sophia_tuple_new(void *obj, struct key_def *key_def,
                 struct tuple_format *format,
                 uint32_t *bsize)
{
	int valuesize = 0;
	char *value = (char *)sp_getstring(obj, "value", &valuesize);
	char *valueend = value + valuesize;

	assert(key_def->part_count <= 8);
	struct {
		const char *part;
		int size;
	} parts[8];

	/* prepare keys */
	int size = 0;
	uint32_t i = 0;
	while (i < key_def->part_count) {
		char partname[32];
		int len = snprintf(partname, sizeof(partname), "key");
		if (i > 0)
			 snprintf(partname + len, sizeof(partname) - len, "_%d", i);
		parts[i].part = (const char *)sp_getstring(obj, partname, &parts[i].size);
		assert(parts[i].part != NULL);
		if (key_def->parts[i].type == STRING) {
			size += mp_sizeof_str(parts[i].size);
		} else {
			size += mp_sizeof_uint(load_u64(parts[i].part));
		}
		i++;
	}
	int count = key_def->part_count;
	char *p = value;
	while (p < valueend) {
		count++;
		mp_next((const char **)&p);
	}
	size += mp_sizeof_array(count);
	size += valuesize;
	if (bsize) {
		*bsize = size;
	}

	/* build tuple */
	struct tuple *tuple;
	char *raw = NULL;
	if (format) {
		tuple = tuple_alloc(format, size);
		p = tuple->data;
	} else {
		raw = (char *)malloc(size);
		if (raw == NULL)
			tnt_raise(OutOfMemory, size, "malloc", "tuple");
		p = raw;
	}
	p = mp_encode_array(p, count);
	for (i = 0; i < key_def->part_count; i++) {
		if (key_def->parts[i].type == STRING)
			p = mp_encode_str(p, parts[i].part, parts[i].size);
		else
			p = mp_encode_uint(p, load_u64(parts[i].part));
	}
	memcpy(p, value, valuesize);
	if (format) {
		try {
			tuple_init_field_map(format, tuple, (uint32_t *)tuple);
		} catch (Exception *e) {
			tuple_delete(tuple);
			throw;
		}
		return tuple;
	}
	return raw;
}

static uint64_t num_parts[8];

void*
SophiaIndex::createDocument(const char *key, bool async, const char **keyend)
{
	assert(key_def->part_count <= 8);
	void *obj = sp_document(db);
	if (obj == NULL)
		sophia_error(env);
	if (async)
		sp_setint(obj, "async", 1);
	sp_setstring(obj, "arg", fiber(), 0);
	if (key == NULL)
		return obj;
	uint32_t i = 0;
	while (i < key_def->part_count) {
		char partname[32];
		int len = snprintf(partname, sizeof(partname), "key");
		if (i > 0)
			 snprintf(partname + len, sizeof(partname) - len, "_%d", i);
		const char *part;
		uint32_t partsize;
		if (key_def->parts[i].type == STRING) {
			part = mp_decode_str(&key, &partsize);
		} else {
			num_parts[i] = mp_decode_uint(&key);
			part = (char *)&num_parts[i];
			partsize = sizeof(uint64_t);
		}
		if (partsize == 0)
			part = "";
		if (sp_setstring(obj, partname, part, partsize) == -1)
			sophia_error(env);
		i++;
	}
	if (keyend) {
		*keyend = key;
	}
	return obj;
}

static int
sophia_upsert_callback(char **result,
                       char **key, int *key_size, int key_count,
                       char *src, int src_size,
                       char *upsert, int upsert_size,
                       void *arg);

static inline void*
sophia_configure(struct space *space, struct key_def *key_def)
{
	SophiaEngine *engine =
		(SophiaEngine *)space->handler->engine;
	void *env = engine->env;
	/* create database */
	char path[128];
	snprintf(path, sizeof(path), "%" PRIu32, key_def->space_id);
	sp_setstring(env, "db", path, 0);
	/* db.id */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".id",
	         key_def->space_id);
	sp_setint(env, path, key_def->space_id);
	/* apply space schema */
	uint32_t i = 0;
	while (i < key_def->part_count)
	{
		char *type;
		if (key_def->parts[i].type == NUM)
			type = (char *)"u64";
		else
			type = (char *)"string";
		char part[32];
		if (i == 0) {
			snprintf(part, sizeof(part), "key");
		} else {
			/* create key-part */
			snprintf(path, sizeof(path), "db.%" PRIu32 ".index",
			         key_def->space_id);
			snprintf(part, sizeof(part), "key_%d", i);
			sp_setstring(env, path, part, 0);
		}
		/* set key-part type */
		snprintf(path, sizeof(path), "db.%" PRIu32 ".index.%s",
		         key_def->space_id, part);
		sp_setstring(env, path, type, 0);
		i++;
	}
	/* db.upsert */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".index.upsert", key_def->space_id);
	sp_setstring(env, path, (const void *)(uintptr_t)sophia_upsert_callback, 0);
	/* db.upsert_arg */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".index.upsert_arg", key_def->space_id);
	sp_setstring(env, path, (const void *)key_def, 0);
	/* db.compression */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".compression", key_def->space_id);
	sp_setstring(env, path, cfg_gets("sophia.compression"), 0);
	/* db.compression_branch */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".compression_branch", key_def->space_id);
	sp_setstring(env, path, cfg_gets("sophia.compression"), 0);
	/* db.compression_key */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".compression_key", key_def->space_id);
	sp_setint(env, path, cfg_geti("sophia.compression_key"));
	/* db.path_fail_on_drop */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".path_fail_on_drop", key_def->space_id);
	sp_setint(env, path, 0);
	/* db */
	snprintf(path, sizeof(path), "db.%" PRIu32, key_def->space_id);
	void *db = sp_getobject(env, path);
	if (db == NULL)
		sophia_error(env);
	return db;
}

SophiaIndex::SophiaIndex(struct key_def *key_def_arg)
	: Index(key_def_arg)
{
	struct space *space = space_cache_find(key_def->space_id);
	SophiaEngine *engine =
		(SophiaEngine *)space->handler->engine;
	env = engine->env;
	db = sophia_configure(space, key_def);
	if (db == NULL)
		sophia_error(env);
	/* start two-phase recovery for a space:
	 * a. created after snapshot recovery
	 * b. created during log recovery
	*/
	int rc = sp_open(db);
	if (rc == -1)
		sophia_error(env);
	format = space->format;
	tuple_format_ref(format, 1);
}

SophiaIndex::~SophiaIndex()
{
	if (db == NULL)
		return;
	int rc = sp_destroy(db);
	if (rc == 0)
		return;
	char *error = (char *)sp_getstring(env, "sophia.error", 0);
	say_info("sophia space %d close error: %s",
			 key_def->space_id, error);
	free(error);
}

size_t
SophiaIndex::size() const
{
	char name[128];
	snprintf(name, sizeof(name), "db.%" PRIu32 ".index.count",
	         key_def->space_id);
	return sp_getint(env, name);
}

size_t
SophiaIndex::bsize() const
{
	char name[128];
	snprintf(name, sizeof(name), "db.%" PRIu32 ".index.memory_used",
	         key_def->space_id);
	return sp_getint(env, name);
}

struct tuple *
SophiaIndex::findByKey(const char *key, uint32_t part_count = 0) const
{
	(void)part_count;
	void *obj = ((SophiaIndex *)this)->createDocument(key, true, NULL);
	void *transaction = db;
	/* engine_tx might be empty, even if we are in txn context.
	 *
	 * This can happen on a first-read statement. */
	if (in_txn())
		transaction = in_txn()->engine_tx;
	obj = sp_get(transaction, obj);
	if (obj == NULL)
		return NULL;
	int rc = sp_getint(obj, "status");
	if (rc == 0) {
		sp_destroy(obj);
		fiber_yield();
		obj = fiber_get_key(fiber(), FIBER_KEY_MSG);
		if (obj == NULL)
			return NULL;
		rc = sp_getint(obj, "status");
		if (rc <= 0 || rc == 2) {
			sp_destroy(obj);
			return NULL;
		}
	}
	struct tuple *tuple =
		(struct tuple *)sophia_tuple_new(obj, key_def, format, NULL);
	sp_destroy(obj);
	return tuple;
}

struct tuple *
SophiaIndex::replace(struct tuple*, struct tuple*, enum dup_replace_mode)
{
	/* This method is unused by sophia index.
	 *
	 * see ::replace_or_insert() */
	assert(0);
	return NULL;
}

struct sophia_mempool {
	void *chunks[128];
	int count;
};

static inline void
sophia_mempool_init(sophia_mempool *p)
{
	memset(p->chunks, 0, sizeof(p->chunks));
	p->count = 0;
}

static inline void
sophia_mempool_free(sophia_mempool *p)
{
	int i = 0;
	while (i < p->count) {
		free(p->chunks[i]);
		i++;
	}
}

static void *
sophia_update_alloc(void *arg, size_t size)
{
	/* simulate region allocator for use with
	 * tuple_upsert_execute() */
	struct sophia_mempool *p = (struct sophia_mempool*)arg;
	assert(p->count < 128);
	void *ptr = malloc(size);
	p->chunks[p->count++] = ptr;
	return ptr;
}

static inline int
sophia_upsert_mp(char **tuple, int *tuple_size_key, struct key_def *key_def,
                 char **key, int *key_size,
                 char *src, int src_size)
{
	/* calculate msgpack size */
	uint32_t mp_keysize = 0;
	uint32_t i = 0;
	while (i < key_def->part_count) {
		if (key_def->parts[i].type == STRING)
			mp_keysize += mp_sizeof_str(key_size[i]);
		else
			mp_keysize += mp_sizeof_uint(load_u64(key[i]));
		i++;
	}
	*tuple_size_key = mp_keysize + mp_sizeof_array(key_def->part_count);

	/* count fields */
	int count = key_def->part_count;
	const char *p = src;
	while (p < (src + src_size)) {
		count++;
		mp_next((const char **)&p);
	}

	/* allocate and encode tuple */
	int mp_size = mp_sizeof_array(count) +
		mp_keysize + src_size;
	char *mp = (char *)malloc(mp_size);
	char *mp_ptr = mp;
	if (mp == NULL)
		return -1;
	mp_ptr = mp_encode_array(mp_ptr, count);
	i = 0;
	while (i < key_def->part_count) {
		if (key_def->parts[i].type == STRING)
			mp_ptr = mp_encode_str(mp_ptr, key[i], key_size[i]);
		else
			mp_ptr = mp_encode_uint(mp_ptr, load_u64(key[i]));
		i++;
	}
	memcpy(mp_ptr, src, src_size);

	*tuple = mp;
	return mp_size;
}

static inline int
sophia_upsert(char **result,
              char *tuple, int tuple_size, int tuple_size_key,
              char *upsert, int upsert_size)
{
	char *p = upsert;
	uint8_t index_base = *(uint8_t *)p;
	p += sizeof(uint8_t);
	uint32_t default_tuple_size = *(uint32_t *)p;
	p += sizeof(uint32_t);
	p += default_tuple_size;
	char *expr = p;
	char *expr_end = upsert + upsert_size;
	const char *up;
	uint32_t up_size;
	/* emit upsert */
	struct sophia_mempool alloc;
	sophia_mempool_init(&alloc);
	try {
		up = tuple_upsert_execute(sophia_update_alloc, &alloc,
		                          expr,
		                          expr_end,
		                          tuple,
		                          tuple + tuple_size,
		                          &up_size, index_base);
	} catch (Exception *e) {
		sophia_mempool_free(&alloc);
		return -1;
	}
	/* get new value */
	int size = up_size - tuple_size_key;
	*result = (char *)malloc(size);
	if (! *result) {
		sophia_mempool_free(&alloc);
		return -1;
	}
	memcpy(*result, up + tuple_size_key, size);
	sophia_mempool_free(&alloc);
	return size;
}

static int
sophia_upsert_callback(char **result,
                       char **key, int *key_size, int /* key_count */,
                       char *src, int src_size,
                       char *upsert, int upsert_size,
                       void *arg)
{
	/* use default tuple value */
	if (src == NULL) {
		char *p = upsert;
		p += sizeof(uint8_t); /* index base */
		uint32_t value_size = *(uint32_t *)p;
		p += sizeof(uint32_t);
		*result = (char *)malloc(value_size);
		if (! *result)
			return -1;
		memcpy(*result, p, value_size);
		return value_size;
	}
	struct key_def *key_def = (struct key_def *)arg;
	/* convert to msgpack */
	char *tuple;
	int tuple_size_key;
	int tuple_size;
	tuple_size = sophia_upsert_mp(&tuple, &tuple_size_key,
	                              key_def, key, key_size,
	                              src, src_size);
	if (tuple_size == -1)
		return -1;
	/* execute upsert */
	int size;
	size = sophia_upsert(result,
	                     tuple, tuple_size, tuple_size_key,
	                     upsert,
	                     upsert_size);
	free(tuple);
	return size;
}

void
SophiaIndex::upsert(const char *expr,
                    const char *expr_end,
                    const char *tuple,
                    const char *tuple_end,
                    uint8_t index_base)
{
	mp_decode_array(&tuple);
	uint32_t expr_size  = expr_end - expr;
	uint32_t tuple_size = tuple_end - tuple;
	uint32_t tuple_value_size;
	const char *tuple_value;
	void *obj = createDocument(tuple, false, &tuple_value);
	tuple_value_size = tuple_size - (tuple_value - tuple);
	uint32_t value_size =
		sizeof(uint8_t) + sizeof(uint32_t) + tuple_value_size + expr_size;
	char *value = (char *)malloc(value_size);
	if (value == NULL) {
	}
	char *p = value;
	memcpy(p, &index_base, sizeof(uint8_t));
	p += sizeof(uint8_t);
	memcpy(p, &tuple_value_size, sizeof(uint32_t));
	p += sizeof(uint32_t);
	memcpy(p, tuple_value, tuple_value_size);
	p += tuple_value_size;
	memcpy(p, expr, expr_size);
	sp_setstring(obj, "value", value, value_size);
	void *transaction = in_txn()->engine_tx;
	int rc = sp_upsert(transaction, obj);
	free(value);
	if (rc == -1)
		sophia_error(env);
}

void
SophiaIndex::replace_or_insert(const char *tuple,
                               const char *tuple_end,
                               enum dup_replace_mode mode)
{
	uint32_t size = tuple_end - tuple;
	const char *key = tuple_field_raw(tuple, size, key_def->parts[0].fieldno);
	/* insert: ensure key does not exists */
	if (mode == DUP_INSERT) {
		struct tuple *found = findByKey(key);
		if (found) {
			tuple_delete(found);
			struct space *sp = space_cache_find(key_def->space_id);
			tnt_raise(ClientError, ER_TUPLE_FOUND,
			          index_name(this), space_name(sp));
		}
	}

	/* replace */
	void *transaction = in_txn()->engine_tx;
	const char *value;
	size_t valuesize;
	void *obj = createDocument(key, false, &value);
	valuesize = size - (value - tuple);
	if (valuesize > 0)
		sp_setstring(obj, "value", value, valuesize);
	int rc;
	rc = sp_set(transaction, obj);
	if (rc == -1)
		sophia_error(env);
}

void
SophiaIndex::remove(const char *key)
{
	void *obj = createDocument(key, false, NULL);
	void *transaction = in_txn()->engine_tx;
	int rc = sp_delete(transaction, obj);
	if (rc == -1)
		sophia_error(env);
}

struct sophia_iterator {
	struct iterator base;
	const char *key;
	const char *keyend;
	struct space *space;
	struct key_def *key_def;
	int open;
	void *env;
	void *db;
	void *cursor;
	void *current;
};

void
sophia_iterator_free(struct iterator *ptr)
{
	assert(ptr->free == sophia_iterator_free);
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	if (it->current) {
		sp_destroy(it->current);
		it->current = NULL;
	}
	if (it->cursor) {
		sp_destroy(it->cursor);
		it->cursor = NULL;
	}
	free(ptr);
}

struct tuple *
sophia_iterator_next(struct iterator *ptr)
{
	assert(ptr->next == sophia_iterator_next);
	struct sophia_iterator *it = (struct sophia_iterator *) ptr;
	assert(it->cursor != NULL);
	if (it->open) {
		it->open = 0;
		if (it->current) {
			return (struct tuple *)
				sophia_tuple_new(it->current, it->key_def,
				                 it->space->format,
				                 NULL);
		} else {
			return NULL;
		}
	}
	/* try to read next key from cache */
	sp_setint(it->current, "async", 0);
	sp_setint(it->current, "cache_only", 1);
	sp_setint(it->current, "immutable", 1);
	void *obj = sp_get(it->cursor, it->current);
	sp_setint(it->current, "async", 1);
	sp_setint(it->current, "cache_only", 0);
	sp_setint(it->current, "immutable", 0);
	/* key found in cache */
	if (obj) {
		sp_destroy(it->current);
		it->current = obj;
		return (struct tuple *)
			sophia_tuple_new(obj, it->key_def, it->space->format, NULL);
	}

	/* retry search, but use disk this time */
	obj = sp_get(it->cursor, it->current);
	it->current = NULL;
	if (obj == NULL)
		return NULL;
	sp_destroy(obj);
	fiber_yield();
	obj = fiber_get_key(fiber(), FIBER_KEY_MSG);
	if (obj == NULL)
		return NULL;
	int rc = sp_getint(obj, "status");
	if (rc <= 0) {
		sp_destroy(obj);
		return NULL;
	}
	it->current = obj;
	return (struct tuple *)
		sophia_tuple_new(obj, it->key_def, it->space->format, NULL);
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
	SophiaIndex *index = (SophiaIndex *)index_find(it->space, 0);
	return index->findByKey(it->key);
}

struct iterator *
SophiaIndex::allocIterator() const
{
	struct sophia_iterator *it =
		(struct sophia_iterator *) calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct sophia_iterator),
			  "Sophia Index", "iterator");
	}
	it->base.next = sophia_iterator_next;
	it->base.free = sophia_iterator_free;
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
	if (part_count > 0) {
		if (part_count != key_def->part_count) {
			tnt_raise(UnsupportedIndexFeature, this,
				  "partial keys");
		}
	} else {
		key = NULL;
	}
	it->key = key;
	it->key_def = key_def;
	it->env = env;
	it->db = db;
	it->space = space_cache_find(key_def->space_id);
	it->current = NULL;
	it->open = 1;
	const char *compare;
	switch (type) {
	case ITER_EQ:
		it->base.next = sophia_iterator_eq;
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
		return initIterator(ptr, type, key, part_count);
	}
	it->base.next = sophia_iterator_next;
	it->cursor = sp_cursor(env);
	if (it->cursor == NULL)
		sophia_error(env);
	void *obj = ((SophiaIndex *)this)->createDocument(key, true, &it->keyend);
	sp_setstring(obj, "order", compare, 0);
	/* Position first key here, since key pointer might be
	 * unavailable from lua.
	 *
	 * Read from disk and fill cursor cache.
	 */
	obj = sp_get(it->cursor, obj);
	if (obj == NULL) {
		sp_destroy(it->cursor);
		it->cursor = NULL;
		return;
	}
	sp_destroy(obj);
	fiber_yield();
	obj = fiber_get_key(fiber(), FIBER_KEY_MSG);
	if (obj == NULL) {
		it->current = NULL;
		return;
	}
	int rc = sp_getint(obj, "status");
	if (rc <= 0) {
		it->current = NULL;
		sp_destroy(obj);
		return;
	}
	it->current = obj;
}
