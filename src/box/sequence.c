/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
#include "sequence.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <small/mempool.h>
#include <msgpuck/msgpuck.h>

#include "txn.h"
#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "fiber.h"
#include "index.h"
#include "schema.h"
#include "session.h"
#include "trivia/util.h"

#include <PMurHash.h>

enum {
	SEQUENCE_HASH_SEED = 13U,
	SEQUENCE_DATA_EXTENT_SIZE = 512,
};

/** Sequence state. */
struct sequence_data {
	/** Sequence id. */
	uint32_t id;
	/** Sequence value. */
	int64_t value;
};

static inline bool
sequence_data_equal(struct sequence_data data1, struct sequence_data data2)
{
	return data1.id == data2.id;
}

static inline bool
sequence_data_equal_key(struct sequence_data data, uint32_t id)
{
	return data.id == id;
}

#define LIGHT_NAME _sequence
#define LIGHT_DATA_TYPE struct sequence_data
#define LIGHT_KEY_TYPE uint32_t
#define LIGHT_CMP_ARG_TYPE int
#define LIGHT_EQUAL(a, b, c) sequence_data_equal(a, b)
#define LIGHT_EQUAL_KEY(a, b, c) sequence_data_equal_key(a, b)
#include "salad/light.h"

static struct light_sequence_core sequence_data_index;
static struct mempool sequence_data_extent_pool;

static void *
sequence_data_extent_alloc(void *ctx)
{
	(void)ctx;
	void *ret = mempool_alloc(&sequence_data_extent_pool);
	if (ret == NULL)
		diag_set(OutOfMemory, SEQUENCE_DATA_EXTENT_SIZE,
			 "mempool", "sequence_data_extent");
	return ret;
}

static void
sequence_data_extent_free(void *ctx, void *extent)
{
	(void)ctx;
	mempool_free(&sequence_data_extent_pool, extent);
}

static inline uint32_t
sequence_hash(uint32_t id)
{
	return PMurHash32(SEQUENCE_HASH_SEED, &id, sizeof(id));
}

void
sequence_init(void)
{
	mempool_create(&sequence_data_extent_pool, &cord()->slabc,
		       SEQUENCE_DATA_EXTENT_SIZE);
	light_sequence_create(&sequence_data_index, SEQUENCE_DATA_EXTENT_SIZE,
			      sequence_data_extent_alloc,
			      sequence_data_extent_free, NULL, 0);
}

void
sequence_free(void)
{
	light_sequence_destroy(&sequence_data_index);
	mempool_destroy(&sequence_data_extent_pool);
}

struct sequence *
sequence_new(struct sequence_def *def)
{
	struct sequence *seq = calloc(1, sizeof(*seq));
	if (seq == NULL) {
		diag_set(OutOfMemory, sizeof(*seq), "malloc", "sequence");
		return NULL;
	}
	seq->def = def;
	return seq;
}

void
sequence_delete(struct sequence *seq)
{
	/* Delete sequence data. */
	sequence_reset(seq);
	free(seq->def);
	free(seq);
}

void
sequence_reset(struct sequence *seq)
{
	uint32_t key = seq->def->id;
	uint32_t hash = sequence_hash(key);
	uint32_t pos = light_sequence_find_key(&sequence_data_index, hash, key);
	if (pos != light_sequence_end)
		light_sequence_delete(&sequence_data_index, pos);
}

int
sequence_set(struct sequence *seq, int64_t value)
{
	uint32_t key = seq->def->id;
	uint32_t hash = sequence_hash(key);
	struct sequence_data new_data, old_data;
	new_data.id = key;
	new_data.value = value;
	if (light_sequence_replace(&sequence_data_index, hash,
				   new_data, &old_data) != light_sequence_end)
		return 0;
	if (light_sequence_insert(&sequence_data_index, hash,
				  new_data) != light_sequence_end)
		return 0;
	return -1;
}

int
sequence_update(struct sequence *seq, int64_t value)
{
	uint32_t key = seq->def->id;
	uint32_t hash = sequence_hash(key);
	uint32_t pos = light_sequence_find_key(&sequence_data_index, hash, key);
	struct sequence_data new_data, data;
	new_data.id = key;
	new_data.value = value;
	if (pos != light_sequence_end) {
		data = light_sequence_get(&sequence_data_index, pos);
		if ((seq->def->step > 0 && value > data.value) ||
		    (seq->def->step < 0 && value < data.value)) {
			if (light_sequence_replace(&sequence_data_index, hash,
					new_data, &data) == light_sequence_end)
				unreachable();
		}
	} else {
		if (light_sequence_insert(&sequence_data_index, hash,
					  new_data) == light_sequence_end)
			return -1;
	}
	return 0;
}

int
sequence_next(struct sequence *seq, int64_t *result)
{
	int64_t value;
	struct sequence_def *def = seq->def;
	struct sequence_data new_data, old_data;
	uint32_t key = seq->def->id;
	uint32_t hash = sequence_hash(key);
	uint32_t pos = light_sequence_find_key(&sequence_data_index, hash, key);
	if (pos == light_sequence_end) {
		new_data.id = key;
		new_data.value = def->start;
		if (light_sequence_insert(&sequence_data_index, hash,
					  new_data) == light_sequence_end)
			return -1;
		*result = def->start;
		return 0;
	}
	old_data = light_sequence_get(&sequence_data_index, pos);
	value = old_data.value;
	if (def->step > 0) {
		if (value < def->min) {
			value = def->min;
			goto done;
		}
		if (value >= 0 && def->step > INT64_MAX - value)
			goto overflow;
		value += def->step;
		if (value > def->max)
			goto overflow;
	} else {
		assert(def->step < 0);
		if (value > def->max) {
			value = def->max;
			goto done;
		}
		if (value < 0 && def->step < INT64_MIN - value)
			goto overflow;
		value += def->step;
		if (value < def->min)
			goto overflow;
	}
done:
	assert(value >= def->min && value <= def->max);
	new_data.id = key;
	new_data.value = value;
	if (light_sequence_replace(&sequence_data_index, hash,
				   new_data, &old_data) == light_sequence_end)
		unreachable();
	*result = value;
	return 0;
overflow:
	if (!def->cycle) {
		diag_set(ClientError, ER_SEQUENCE_OVERFLOW, def->name);
		return -1;
	}
	value = def->step > 0 ? def->min : def->max;
	goto done;
}

int
access_check_sequence(struct sequence *seq)
{
	struct credentials *cr = effective_user();
	/*
	 * If the user has universal access, don't bother with checks.
	 * No special check for ADMIN user is necessary since ADMIN has
	 * universal access.
	 */

	user_access_t access = PRIV_U | PRIV_W;
	user_access_t sequence_access = access & ~cr->universal_access;
	sequence_access &= ~entity_access_get(SC_SEQUENCE)[cr->auth_token].effective;
	if (sequence_access &&
	    /* Check for missing Usage access, ignore owner rights. */
	    (sequence_access & PRIV_U ||
	     /* Check for missing specific access, respect owner rights. */
	     (seq->def->uid != cr->uid &&
	      sequence_access & ~seq->access[cr->auth_token].effective))) {

		/* Access violation, report error. */
		struct user *user = user_find(cr->uid);
		if (user != NULL) {
			if (!(cr->universal_access & PRIV_U)) {
				diag_set(AccessDeniedError,
					 priv_name(PRIV_U),
					 schema_object_name(SC_UNIVERSE), "",
					 user->def->name);
			} else {
				diag_set(AccessDeniedError,
					 priv_name(access),
					 schema_object_name(SC_SEQUENCE),
					 seq->def->name, user->def->name);
			}
		}
		return -1;
	}
	return 0;
}

/** Read view implementation. */
struct sequence_data_read_view {
	/** Base class. */
	struct index_read_view base;
	/** Frozen view of the data index. */
	struct light_sequence_view view;
};

/** Read view iterator implementation. */
struct sequence_data_iterator {
	/** Base class. */
	struct index_read_view_iterator_base base;
	/** Iterator over the data index. */
	struct light_sequence_iterator iter;
};

static_assert(sizeof(struct sequence_data_iterator) <=
	      INDEX_READ_VIEW_ITERATOR_SIZE,
	      "sizeof(struct sequence_data_iterator) must be less than or "
	      "equal to INDEX_READ_VIEW_ITERATOR_SIZE");

/** Implementation of next_raw index_read_view_iterator callback. */
static int
sequence_data_iterator_next_raw(struct index_read_view_iterator *iterator,
				struct read_view_tuple *result)
{
	struct sequence_data_iterator *iter =
		(struct sequence_data_iterator *)iterator;
	struct sequence_data_read_view *rv =
		(struct sequence_data_read_view *)iter->base.index;

	struct sequence_data *sd = light_sequence_view_iterator_get_and_next(
		&rv->view, &iter->iter);
	if (sd == NULL) {
		*result = read_view_tuple_none();
		return 0;
	}

	const size_t buf_size = mp_sizeof_array(2) +
				2 * mp_sizeof_uint(UINT64_MAX);
	char *buf = region_alloc(&fiber()->gc, buf_size);
	char *buf_end = buf;
	buf_end = mp_encode_array(buf_end, 2);
	buf_end = mp_encode_uint(buf_end, sd->id);
	buf_end = (sd->value >= 0 ?
		   mp_encode_uint(buf_end, sd->value) :
		   mp_encode_int(buf_end, sd->value));
	assert(buf_end <= buf + buf_size);
	result->data = buf;
	result->size = buf_end - buf;
	return 0;
}

static inline int
sequence_data_read_view_get_raw(struct index_read_view *rv,
				const char *key, uint32_t part_count,
				struct read_view_tuple *result)
{
	(void)rv;
	(void)key;
	(void)part_count;
	(void)result;
	diag_set(ClientError, ER_UNSUPPORTED,
		 "_sequence_data read view", "get()");
	return -1;
}

/** Implementation of create_iterator index_read_view callback. */
static int
sequence_data_iterator_create(struct index_read_view *base,
			      enum iterator_type type,
			      const char *key, uint32_t part_count,
			      struct index_read_view_iterator *iterator)
{
	if (type != ITER_ALL) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 "_sequence_data read view", "requested iterator type");
		return -1;
	}
	(void)key;
	(void)part_count;
	struct sequence_data_read_view *rv =
		(struct sequence_data_read_view *)base;
	struct sequence_data_iterator *iter =
		(struct sequence_data_iterator *)iterator;
	iter->base.index = base;
	iter->base.next_raw = sequence_data_iterator_next_raw;
	light_sequence_view_iterator_begin(&rv->view, &iter->iter);
	return 0;
}

static void
sequence_data_read_view_free(struct index_read_view *base)
{
	struct sequence_data_read_view *rv =
		(struct sequence_data_read_view *)base;
	light_sequence_view_destroy(&rv->view);
	TRASH(rv);
	free(rv);
}

struct index_read_view *
sequence_data_read_view_create(struct index *index)
{
	(void)index;
	static const struct index_read_view_vtab vtab = {
		.free = sequence_data_read_view_free,
		.get_raw = sequence_data_read_view_get_raw,
		.create_iterator = sequence_data_iterator_create,
	};
	struct sequence_data_read_view *rv = xmalloc(sizeof(*rv));
	if (index_read_view_create(&rv->base, &vtab, index->def) != 0) {
		free(rv);
		return NULL;
	}
	light_sequence_view_create(&rv->view, &sequence_data_index);
	return (struct index_read_view *)rv;
}

int
sequence_get_value(struct sequence *seq, int64_t *result)
{
	uint32_t key = seq->def->id;
	uint32_t hash = sequence_hash(key);
	uint32_t pos = light_sequence_find_key(&sequence_data_index, hash, key);
	if (pos == light_sequence_end) {
		diag_set(ClientError, ER_SEQUENCE_NOT_STARTED, seq->def->name);
		return -1;
	}
	struct sequence_data data = light_sequence_get(&sequence_data_index,
						       pos);
	*result = data.value;
	return 0;
}
