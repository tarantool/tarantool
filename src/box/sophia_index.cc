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
	int i = 0;
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
			size += mp_sizeof_uint(*(uint64_t *)parts[i].part);
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
			tnt_raise(ClientError, ER_MEMORY_ISSUE, size, "tuple");
		p = raw;
	}
	p = mp_encode_array(p, count);
	for (i = 0; i < key_def->part_count; i++) {
		if (key_def->parts[i].type == STRING)
			p = mp_encode_str(p, parts[i].part, parts[i].size);
		else
			p = mp_encode_uint(p, *(uint64_t *)parts[i].part);
	}
	memcpy(p, value, valuesize);
	if (format) {
		try {
			tuple_init_field_map(format, tuple, (uint32_t *)tuple);
		} catch (...) {
			tuple_delete(tuple);
			throw;
		}
		return tuple;
	}
	return raw;
}

static uint64_t num_parts[8];

void*
SophiaIndex::createObject(const char *key, bool async, const char **keyend)
{
	assert(key_def->part_count <= 8);
	void *host = db;
	if (async) {
		host = sp_asynchronous(db);
	}
	void *obj = sp_object(host);
	if (obj == NULL)
		sophia_error(env);
	sp_setstring(obj, "arg", fiber(), 0);
	if (key == NULL)
		return obj;
	int i = 0;
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
sophia_update(int, char*, int, int, char*, int, void*, void**, int*);

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
	int i = 0;
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
	/* db.update */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".index.update", key_def->space_id);
	sp_setstring(env, path, (const void *)(uintptr_t)sophia_update, 0);
	/* db.update_arg */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".index.update_arg", key_def->space_id);
	sp_setstring(env, path, (const void *)key_def, 0);
	/* db.compression */
	snprintf(path, sizeof(path), "db.%" PRIu32 ".compression", key_def->space_id);
	sp_setint(env, path, cfg_geti("sophia.compression"));
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
	void *obj = ((SophiaIndex *)this)->createObject(key, true, NULL);
	void *transaction = db;
	/* engine_tx might be empty, even if we are in txn context.
	 *
	 * This can happen on a first-read statement. */
	if (in_txn() && in_txn()->engine_tx)
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

static void *
sophia_update_alloc(void *, size_t size)
{
	return malloc(size);
}

struct sophiaref {
	uint32_t offset;
	uint16_t size;
} __attribute__((packed));

static inline char*
sophia_upsert_to_tarantool(struct key_def *key_def, char *origin, int origin_size,
                           uint32_t *origin_keysize,
                           uint32_t *size)
{
	struct sophiaref *ref = (struct sophiaref *)origin;
	uint32_t src_keysize_mp = 0;
	*origin_keysize = 0;

	/* calculate src msgpack size */
	int i = 0;
	while (i < key_def->part_count) {
		char *ptr;
		if (key_def->parts[i].type == STRING) {
			src_keysize_mp += mp_sizeof_str(ref[i].size);
		} else {
			ptr = origin + ref[i].offset;
			src_keysize_mp += mp_sizeof_uint(*(uint64_t *)ptr);
		}
		*origin_keysize += sizeof(struct sophiaref) + ref[i].size;
		i++;
	}

	/* convert src to msgpack */
	int valueoffset =
		ref[key_def->part_count-1].offset +
		ref[key_def->part_count-1].size;
	int valuesize = origin_size - valueoffset;

	int count = key_def->part_count;
	const char *p = origin + valueoffset;
	while (p < (origin + origin_size)) {
		count++;
		mp_next((const char **)&p);
	}

	int src_size = mp_sizeof_array(count) +
		src_keysize_mp + valuesize;
	char *src = (char *)malloc(src_size);
	char *src_ptr = src;
	if (src == NULL)
		return NULL;

	src_ptr = mp_encode_array(src_ptr, count);
	i = 0;
	while (i < key_def->part_count) {
		char *ptr = origin + ref[i].offset;
		if (key_def->parts[i].type == STRING) {
			src_ptr = mp_encode_str(src_ptr, ptr, ref[i].size);
		} else {
			src_ptr = mp_encode_uint(src_ptr, *(uint64_t *)ptr);
		}
		i++;
	}
	memcpy(src_ptr, origin + valueoffset, valuesize);
	src_ptr += valuesize;
	assert((src_ptr - src) == src_size);

	*size = src_size;
	return src;
}

static inline char*
sophia_upsert_to_sophia(struct key_def *key_def, char *dest, int dest_size,
                        char *key, int key_size,
                        int *size)
{
	const char *p = dest;
	int i = 0;
	mp_decode_array(&p);
	while (i < key_def->part_count) {
		mp_next(&p);
		i++;
	}
	const char *dest_value = p;
	uint32_t dest_value_size = dest_size - (p - dest);
	*size = key_size + dest_value_size;
	char *cnv = (char *)malloc(*size);
	if (cnv == NULL)
		return NULL;
	p = cnv;
	memcpy((void *)p, (void *)key, key_size);
	p += key_size;
	memcpy((void *)p, (void *)dest_value, dest_value_size);
	p += dest_value_size;
	assert((p - cnv) == *size);
	return cnv;
}

static inline char*
sophia_upsert_default(struct key_def *key_def, char *update, int update_size,
                      uint32_t *origin_keysize,
                      uint32_t *size)
{
	/* calculate keysize */
	struct sophiaref *ref = (struct sophiaref *)update;
	*origin_keysize = 0;
	int i = 0;
	while (i < key_def->part_count) {
		*origin_keysize += sizeof(struct sophiaref) + ref[i].size;
		i++;
	}
	/* upsert using default tuple */
	char *p = update + *origin_keysize;
	uint8_t index_base = *(uint32_t *)p;
	p += sizeof(uint8_t);
	uint32_t default_tuple_size = *(uint32_t *)p;
	p += sizeof(uint32_t);
	char *default_tuple = p;
	char *default_tuple_end = p + default_tuple_size;
	p += default_tuple_size;
	char *expr = p;
	char *expr_end = update + update_size;
	const char *up;
	try {
		up = tuple_upsert_execute(sophia_update_alloc, NULL,
		                          expr,
		                          expr_end,
		                          default_tuple,
		                          default_tuple_end,
		                          size, index_base);
	} catch (...) {
		return NULL;
	}
	return (char *)up;
}

static inline char*
sophia_upsert(char *src, int src_size, char *update, int update_size,
              uint32_t origin_keysize,
              uint32_t *size)
{
	char *p = update + origin_keysize;
	uint8_t index_base = *(uint32_t *)p;
	p += sizeof(uint8_t);
	uint32_t default_tuple_size = *(uint32_t *)p;
	p += sizeof(uint32_t);
	char *default_tuple = p;
	char *default_tuple_end = p + default_tuple_size;
	(void)default_tuple;
	(void)default_tuple_end;
	p += default_tuple_size;
	char *expr = p;
	char *expr_end = update + update_size;
	const char *up;
	try {
		up = tuple_upsert_execute(sophia_update_alloc, NULL,
		                          expr,
		                          expr_end,
		                          src,
		                          src + src_size,
		                          size, index_base);
	} catch (...) {
		return NULL;
	}
	return (char *)up;
}

static int
sophia_update(int origin_flags, char *origin, int origin_size,
              int update_flags, char *update, int update_size,
              void *arg,
              void **result, int *size)
{
	(void)origin_flags;
	(void)update_flags;
	struct key_def *key_def = (struct key_def *)arg;
	uint32_t origin_keysize;
	uint32_t src_size;
	char *src;
	uint32_t dest_size;
	char *dest;
	if (origin) {
		/* convert origin object to msgpack */
		src = sophia_upsert_to_tarantool(key_def, origin, origin_size,
		                                 &origin_keysize,
		                                 &src_size);
		if (src == NULL)
			return -1;
		/* execute upsert */
		dest = sophia_upsert(src, src_size, update, update_size,
		                     origin_keysize, &dest_size);
		free(src);
	} else {
		/* use default tuple from update */
		dest = sophia_upsert_default(key_def, update, update_size,
		                             &origin_keysize, &dest_size);
		origin = update;
	}
	if (dest == NULL)
		return -1;

	/* convert msgpack to sophia format */
	*result = sophia_upsert_to_sophia(key_def, dest, dest_size, origin,
	                                  origin_keysize, size);
	free(dest);
	return (*result == NULL) ? -1 : 0;
}

void
SophiaIndex::upsert(const char *key,
                    const char *expr,
                    const char *expr_end,
                    const char *tuple,
                    const char *tuple_end,
                    uint8_t index_base)
{
	uint32_t tuple_size = tuple_end - tuple;
	uint32_t expr_size = expr_end - expr;
	uint32_t valuesize =
		sizeof(uint8_t) + sizeof(uint32_t) + tuple_size + expr_size;
	char *value = (char *)malloc(valuesize);
	if (value == NULL) {
	}
	char *p = value;
	memcpy(p, &index_base, sizeof(uint8_t));
	p += sizeof(uint8_t);
	memcpy(p, &tuple_size, sizeof(uint32_t));
	p += sizeof(uint32_t);
	memcpy(p, tuple, tuple_size);
	p += tuple_size;
	memcpy(p, expr, expr_size);
	void *transaction = in_txn()->engine_tx;
	void *obj = createObject(key, false, NULL);
	sp_setstring(obj, "value", value, valuesize);
	int rc = sp_update(transaction, obj);
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
	void *obj = createObject(key, false, &value);
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
	void *obj = createObject(key, false, NULL);
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
	void *obj = sp_get(it->cursor, it->current);
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
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
		          sizeof(struct sophia_iterator), "Sophia Index",
		          "iterator");
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
			tnt_raise(ClientError, ER_UNSUPPORTED,
			          "Sophia Index iterator", "uncomplete keys");
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
		tnt_raise(ClientError, ER_UNSUPPORTED,
		          "Sophia Index", "requested iterator type");
	}
	it->base.next = sophia_iterator_next;
	it->cursor = sp_cursor(env);
	if (it->cursor == NULL)
		sophia_error(env);
	void *obj = ((SophiaIndex *)this)->createObject(key, true, &it->keyend);
	sp_setstring(obj, "order", compare, 0);
	/* position first key here, since key pointer might be
	 * unavailable from lua */
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
