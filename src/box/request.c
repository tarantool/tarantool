/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "request.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <msgpuck.h>
#include <small/region.h>

#include "fiber.h"
#include "space.h"
#include "index.h"
#include "sequence.h"
#include "key_def.h"
#include "tuple.h"
#include "xrow.h"
#include "iproto_constants.h"

int
request_create_from_tuple(struct request *request, struct space *space,
			  struct tuple *old_tuple, struct tuple *new_tuple)
{
	memset(request, 0, sizeof(*request));
	request->space_id = space->def->id;

	if (old_tuple == new_tuple) {
		/*
		 * Old and new tuples are the same,
		 * turn this request into no-op.
		 */
		request->type = IPROTO_NOP;
		return 0;
	}

	if (new_tuple == NULL) {
		uint32_t size, key_size;
		const char *data = tuple_data_range(old_tuple, &size);
		request->key = tuple_extract_key_raw(data, data + size,
				space->index[0]->def->key_def, &key_size);
		if (request->key == NULL)
			return -1;
		request->key_end = request->key + key_size;
		request->type = IPROTO_DELETE;
	} else {
		uint32_t size;
		const char *data = tuple_data_range(new_tuple, &size);
		/*
		 * We have to copy the tuple data to region, because
		 * the tuple is allocated on runtime arena and not
		 * referenced and hence may be freed before the
		 * current transaction ends while we need to write
		 * the tuple data to WAL on commit.
		 */
		char *buf = region_alloc(&fiber()->gc, size);
		if (buf == NULL)
			return -1;
		memcpy(buf, data, size);
		request->tuple = buf;
		request->tuple_end = buf + size;
		request->type = IPROTO_REPLACE;
	}
	return 0;
}

void
request_rebind_to_primary_key(struct request *request, struct space *space,
			      struct tuple *found_tuple)
{
	struct index *pk = space_index(space, 0);
	assert(pk != NULL);
	uint32_t key_len;
	char *key = tuple_extract_key(found_tuple, pk->def->key_def, &key_len);
	assert(key != NULL);
	request->key = key;
	request->key_end = key + key_len;
	request->index_id = 0;
	/* Clear the *body* to ensure it's rebuilt at commit. */
	request->header = NULL;
}

int
request_handle_sequence(struct request *request, struct space *space)
{
	struct sequence *seq = space->sequence;

	assert(seq != NULL);
	assert(request->type == IPROTO_INSERT ||
	       request->type == IPROTO_REPLACE);

	/*
	 * An automatically generated sequence inherits
	 * privileges of the space it is used with.
	 */
	if (!seq->is_generated &&
	    access_check_sequence(seq) != 0)
		return -1;

	struct index *pk = space_index(space, 0);
	if (unlikely(pk == NULL))
		return 0;

	/*
	 * Look up the first field of the primary key.
	 */
	const char *data = request->tuple;
	const char *data_end = request->tuple_end;
	int len = mp_decode_array(&data);
	int fieldno = pk->def->key_def->parts[0].fieldno;
	if (unlikely(len < fieldno + 1))
		return 0;

	const char *key = data;
	if (unlikely(fieldno > 0)) {
		do {
			mp_next(&key);
		} while (--fieldno > 0);
	}

	int64_t value;
	if (mp_typeof(*key) == MP_NIL) {
		/*
		 * If the first field of the primary key is nil,
		 * this is an auto increment request and we need
		 * to replace the nil with the next value generated
		 * by the space sequence.
		 */
		if (unlikely(sequence_next(seq, &value) != 0))
			return -1;

		const char *key_end = key;
		mp_decode_nil(&key_end);

		size_t buf_size = (request->tuple_end - request->tuple) +
						mp_sizeof_uint(UINT64_MAX);
		char *tuple = region_alloc(&fiber()->gc, buf_size);
		if (tuple == NULL)
			return -1;
		char *tuple_end = mp_encode_array(tuple, len);

		if (unlikely(key != data)) {
			memcpy(tuple_end, data, key - data);
			tuple_end += key - data;
		}

		if (value >= 0)
			tuple_end = mp_encode_uint(tuple_end, value);
		else
			tuple_end = mp_encode_int(tuple_end, value);

		memcpy(tuple_end, key_end, data_end - key_end);
		tuple_end += data_end - key_end;

		assert(tuple_end <= tuple + buf_size);

		request->tuple = tuple;
		request->tuple_end = tuple_end;
	} else {
		/*
		 * If the first field is not nil, update the space
		 * sequence with its value, to make sure that an
		 * auto increment request never tries to insert a
		 * value that is already in the space. Note, this
		 * code is also invoked on final recovery to restore
		 * the sequence value from WAL.
		 */
		if (likely(mp_read_int64(&key, &value) == 0))
			return sequence_update(seq, value);
	}
	/*
	 * As the request body was changed, we have to update body in header.
	 */
	struct xrow_header *row = request->header;
	if (row != NULL) {
		row->bodycnt = xrow_encode_dml(request, row->body);
		if (row->bodycnt < 0)
			return -1;
	}
	return 0;
}
