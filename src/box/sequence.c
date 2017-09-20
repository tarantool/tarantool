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

#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "fiber.h"

#include "third_party/PMurHash.h"

enum {
	SEQUENCE_HASH_SEED = 13U,
	SEQUENCE_DATA_EXTENT_SIZE = 512,
};

struct light_sequence_core sequence_data_index;

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
