/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "xrow.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "txn.h"
#include "index.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "request.h"
#include "iproto_constants.h"
#include "phia.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

PhiaSpace::PhiaSpace(Engine *e)
	:Handler(e)
{ }

void
PhiaSpace::applySnapshotRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	PhiaIndex *index = (PhiaIndex *)index_find(space, 0);

	space_validate_tuple_raw(space, request->tuple);
	int size = request->tuple_end - request->tuple;
	const char *key = tuple_field_raw(request->tuple, size,
					  index->key_def->parts[0].fieldno);
	primary_key_validate(index->key_def, key, index->key_def->part_count);

	const char *value = NULL;
	struct phia_document *obj = index->createDocument(key, &value);
	size_t valuesize = size - (value - request->tuple);
	if (valuesize > 0)
		phia_document_set_field(obj, "value", value, valuesize);

	assert(request->header != NULL);

	struct phia_tx *tx = phia_begin(index->env);
	if (tx == NULL) {
		phia_document_delete(obj);
		phia_raise();
	}

	int64_t signature = request->header->lsn;
	phia_tx_set_lsn(tx, signature);

	if (phia_replace(tx, obj) != 0)
		phia_raise();
	/* obj destroyed by phia_replace() */

	int rc = phia_commit(tx);
	switch (rc) {
	case 0:
		return;
	case 1: /* rollback */
		return;
	case 2: /* lock */
		phia_rollback(tx);
		/* must never happen during JOIN */
		tnt_raise(ClientError, ER_TRANSACTION_CONFLICT);
		return;
	case -1:
		phia_raise();
		return;
	default:
		assert(0);
	}
}

struct tuple *
PhiaSpace::executeReplace(struct txn*,
			  struct space *space,
			  struct request *request)
{
	PhiaIndex *index = (PhiaIndex *)index_find(space, 0);

	space_validate_tuple_raw(space, request->tuple);

	int size = request->tuple_end - request->tuple;
	const char *key =
		tuple_field_raw(request->tuple, size,
		                index->key_def->parts[0].fieldno);
	primary_key_validate(index->key_def, key, index->key_def->part_count);

	/* unique constraint */
	if (request->type == IPROTO_INSERT) {
		enum dup_replace_mode mode = DUP_REPLACE_OR_INSERT;
		PhiaEngine *engine =
			(PhiaEngine *)space->handler->engine;
		if (engine->recovery_complete)
			mode = DUP_INSERT;
		if (mode == DUP_INSERT) {
			struct tuple *found = index->findByKey(key, 0);
			if (found) {
				tuple_delete(found);
				tnt_raise(ClientError, ER_TUPLE_FOUND,
						  index_name(index), space_name(space));
			}
		}
	}

	/* replace */
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	const char *value = NULL;
	struct phia_document *obj = index->createDocument(key, &value);
	size_t valuesize = size - (value - request->tuple);
	if (valuesize > 0)
		phia_document_set_field(obj, "value", value, valuesize);
	int rc;
	rc = phia_replace(tx, obj);
	if (rc == -1)
		phia_raise();

	return NULL;
}

struct tuple *
PhiaSpace::executeDelete(struct txn*, struct space *space,
                           struct request *request)
{
	PhiaIndex *index = (PhiaIndex *)index_find(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);

	/* remove */
	struct phia_document *obj = index->createDocument(key, NULL);
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	int rc = phia_delete(tx, obj);
	if (rc == -1)
		phia_raise();
	return NULL;
}

struct tuple *
PhiaSpace::executeUpdate(struct txn*, struct space *space,
                           struct request *request)
{
	/* Try to find the tuple by unique key */
	PhiaIndex *index = (PhiaIndex *)index_find(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);
	struct tuple *old_tuple = index->findByKey(key, part_count);

	if (old_tuple == NULL)
		return NULL;

	/* Phia always yields a zero-ref tuple, GC it here. */
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

	/* replace */
	key = tuple_field_raw(new_tuple->data, new_tuple->bsize,
	                      index->key_def->parts[0].fieldno);
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	const char *value = NULL;
	struct phia_document *obj = index->createDocument(key, &value);
	size_t valuesize = new_tuple->bsize - (value - new_tuple->data);
	if (valuesize > 0)
		phia_document_set_field(obj, "value", value, valuesize);
	int rc;
	rc = phia_replace(tx, obj);
	if (rc == -1)
		phia_raise();
	return NULL;
}

static inline int
phia_upsert_prepare(char **src, uint32_t *src_size,
                      char **mp, uint32_t *mp_size, uint32_t *mp_size_key,
                      struct key_def *key_def)
{
	/* calculate msgpack size */
	uint32_t i = 0;
	*mp_size_key = 0;
	while (i < key_def->part_count) {
		if (key_def->parts[i].type == STRING)
			*mp_size_key += mp_sizeof_str(src_size[i]);
		else
			*mp_size_key += mp_sizeof_uint(load_u64(src[i]));
		i++;
	}

	/* count msgpack fields */
	uint32_t count = key_def->part_count;
	uint32_t value_field = key_def->part_count;
	uint32_t value_size = src_size[value_field];
	char *p = src[value_field];
	char *end = p + value_size;
	while (p < end) {
		count++;
		mp_next((const char **)&p);
	}

	/* allocate and encode tuple */
	*mp_size = mp_sizeof_array(count) + *mp_size_key + value_size;
	*mp = (char *)malloc(*mp_size);
	if (mp == NULL)
		return -1;
	p = *mp;
	p = mp_encode_array(p, count);
	i = 0;
	while (i < key_def->part_count) {
		if (key_def->parts[i].type == STRING)
			p = mp_encode_str(p, src[i], src_size[i]);
		else
			p = mp_encode_uint(p, load_u64(src[i]));
		i++;
	}
	memcpy(p, src[value_field], src_size[value_field]);
	return 0;
}

struct phia_mempool {
	void *chunks[128];
	int count;
};

static inline void
phia_mempool_init(phia_mempool *p)
{
	p->count = 0;
}

static inline void
phia_mempool_free(phia_mempool *p)
{
	int i = 0;
	while (i < p->count) {
		free(p->chunks[i]);
		i++;
	}
}

static void *
phia_update_alloc(void *arg, size_t size)
{
	/* simulate region allocator for use with
	 * tuple_upsert_execute() */
	struct phia_mempool *p = (struct phia_mempool*)arg;
	assert(p->count < 128);
	void *ptr = malloc(size);
	p->chunks[p->count++] = ptr;
	return ptr;
}

static inline int
phia_upsert_do(char **result, uint32_t *result_size,
              char *tuple, uint32_t tuple_size, uint32_t tuple_size_key,
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
	struct phia_mempool alloc;
	phia_mempool_init(&alloc);
	try {
		up = tuple_upsert_execute(phia_update_alloc, &alloc,
		                          expr,
		                          expr_end,
		                          tuple,
		                          tuple + tuple_size,
		                          &up_size, index_base);
	} catch (Exception *e) {
		phia_mempool_free(&alloc);
		return -1;
	}

	/* skip array size and key */
	const char *ptr = up;
	mp_decode_array(&ptr);
	ptr += tuple_size_key;

	/* get new value */
	*result_size = (uint32_t)((up + up_size) -  ptr);
	*result = (char *)malloc(*result_size);
	if (! *result) {
		phia_mempool_free(&alloc);
		return -1;
	}
	memcpy(*result, ptr, *result_size);
	phia_mempool_free(&alloc);
	return 0;
}

int
phia_upsert_cb(int count,
	       char **src,    uint32_t *src_size,
	       char **upsert, uint32_t *upsert_size,
	       char **result, uint32_t *result_size,
	       struct key_def *key_def)
{
	uint32_t value_field;
	value_field = key_def->part_count;

	/* use default tuple value */
	if (src == NULL)
	{
		/* result key fields are initialized to upsert
		 * fields by default */
		char *p = upsert[value_field];
		p += sizeof(uint8_t); /* index base */
		uint32_t value_size = *(uint32_t *)p;
		p += sizeof(uint32_t);
		void *value = (char *)malloc(value_size);
		if (value == NULL)
			return -1;
		memcpy(value, p, value_size);
		result[value_field] = (char*)value;
		result_size[value_field] = value_size;
		return 0;
	}

	/* convert src to msgpack */
	char *tuple;
	uint32_t tuple_size_key;
	uint32_t tuple_size;
	int rc;
	rc = phia_upsert_prepare(src, src_size,
	                           &tuple, &tuple_size, &tuple_size_key,
	                           key_def);
	if (rc == -1)
		return -1;

	/* execute upsert */
	rc = phia_upsert_do(&result[value_field],
	                   &result_size[value_field],
	                   tuple, tuple_size, tuple_size_key,
	                   upsert[value_field],
	                   upsert_size[value_field]);
	free(tuple);

	(void)count;
	(void)upsert_size;
	return rc;
}

void
PhiaSpace::executeUpsert(struct txn*, struct space *space,
                           struct request *request)
{
	PhiaIndex *index = (PhiaIndex *)index_find(space, request->index_id);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	const char *expr      = request->ops;
	const char *expr_end  = request->ops_end;
	const char *tuple     = request->tuple;
	const char *tuple_end = request->tuple_end;
	uint8_t index_base    = request->index_base;

	/* upsert */
	mp_decode_array(&tuple);
	uint32_t expr_size  = expr_end - expr;
	uint32_t tuple_size = tuple_end - tuple;
	uint32_t tuple_value_size;
	const char *tuple_value;
	struct phia_document *obj = index->createDocument(tuple, &tuple_value);
	tuple_value_size = tuple_size - (tuple_value - tuple);
	uint32_t value_size =
		sizeof(uint8_t) + sizeof(uint32_t) + tuple_value_size + expr_size;
	char *value = (char *)malloc(value_size);
	if (value == NULL) {
		phia_document_delete(obj);
		tnt_raise(OutOfMemory, sizeof(value_size), "Phia Space",
		          "executeUpsert");
	}
	char *p = value;
	memcpy(p, &index_base, sizeof(uint8_t));
	p += sizeof(uint8_t);
	memcpy(p, &tuple_value_size, sizeof(uint32_t));
	p += sizeof(uint32_t);
	memcpy(p, tuple_value, tuple_value_size);
	p += tuple_value_size;
	memcpy(p, expr, expr_size);
	phia_document_set_field(obj, "value", value, value_size);
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	int rc = phia_upsert(tx, obj);
	free(value);
	if (rc == -1)
		phia_raise();
}
