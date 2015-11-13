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
#include "request.h"
#include "engine.h"
#include "txn.h"
#include "tuple.h"
#include "index.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "lua/call.h"
#include <errinj.h>
#include <fiber.h>
#include <scoped_guard.h>
#include "user_def.h"
#include "iproto_constants.h"
#include "rmean.h"

struct rmean *rmean_box;

void
request_create(struct request *request, uint32_t type)
{
	memset(request, 0, sizeof(*request));
	request->type = type;
}

void
request_decode(struct request *request, const char *data, uint32_t len)
{
	const char *end = data + len;
	uint64_t key_map = iproto_body_key_map[request->type];

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet body");
	}
	uint32_t size = mp_decode_map(&data);
	for (uint32_t i = 0; i < size; i++) {
		if (! iproto_body_has_key(data, end)) {
			mp_check(&data, end);
			mp_check(&data, end);
			continue;
		}
		unsigned char key = mp_decode_uint(&data);
		key_map &= ~iproto_key_bit(key);
		const char *value = data;
		if (mp_check(&data, end))
			goto error;
		if (iproto_key_type[key] != mp_typeof(*value))
			goto error;
		switch (key) {
		case IPROTO_SPACE_ID:
			request->space_id = mp_decode_uint(&value);
			break;
		case IPROTO_INDEX_ID:
			request->index_id = mp_decode_uint(&value);
			break;
		case IPROTO_OFFSET:
			request->offset = mp_decode_uint(&value);
			break;
		case IPROTO_INDEX_BASE:
			request->index_base = mp_decode_uint(&value);
			break;
		case IPROTO_LIMIT:
			request->limit = mp_decode_uint(&value);
			break;
		case IPROTO_ITERATOR:
			request->iterator = mp_decode_uint(&value);
			break;
		case IPROTO_TUPLE:
			request->tuple = value;
			request->tuple_end = data;
			break;
		case IPROTO_KEY:
		case IPROTO_FUNCTION_NAME:
		case IPROTO_USER_NAME:
		case IPROTO_EXPR:
			request->key = value;
			request->key_end = data;
			break;
		case IPROTO_OPS:
			request->ops = value;
			request->ops_end = data;
			break;
		default:
			break;
		}
	}
#ifndef NDEBUG
	if (data != end)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet end");
#endif
	if (key_map) {
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD,
			  iproto_key_strs[__builtin_ffsll((long long) key_map) - 1]);
	  }
}

int
request_encode(struct request *request, struct iovec *iov)
{
	int iovcnt = 1;
	const int MAP_LEN_MAX = 40;
	uint32_t key_len = request->key_end - request->key;
	uint32_t ops_len = request->ops_end - request->ops;
	uint32_t len = MAP_LEN_MAX + key_len + ops_len;
	char *begin = (char *) region_alloc_xc(&fiber()->gc, len);
	char *pos = begin + 1;     /* skip 1 byte for MP_MAP */
	int map_size = 0;
	if (true) {
		pos = mp_encode_uint(pos, IPROTO_SPACE_ID);
		pos = mp_encode_uint(pos, request->space_id);
		map_size++;
	}
	if (request->index_id) {
		pos = mp_encode_uint(pos, IPROTO_INDEX_ID);
		pos = mp_encode_uint(pos, request->index_id);
		map_size++;
	}
	if (request->index_base) { /* UPDATE/UPSERT */
		pos = mp_encode_uint(pos, IPROTO_INDEX_BASE);
		pos = mp_encode_uint(pos, request->index_base);
		map_size++;
	}
	if (request->key) {
		pos = mp_encode_uint(pos, IPROTO_KEY);
		memcpy(pos, request->key, key_len);
		pos += key_len;
		map_size++;
	}
	if (request->ops) {
		pos = mp_encode_uint(pos, IPROTO_OPS);
		memcpy(pos, request->ops, ops_len);
		pos += ops_len;
		map_size++;
	}
	if (request->tuple) {
		pos = mp_encode_uint(pos, IPROTO_TUPLE);
		iov[iovcnt].iov_base = (void *) request->tuple;
		iov[iovcnt].iov_len = (request->tuple_end - request->tuple);
		iovcnt++;
		map_size++;
	}

	assert(pos <= begin + len);
	mp_encode_map(begin, map_size);
	iov[0].iov_base = begin;
	iov[0].iov_len = pos - begin;

	return iovcnt;
}

/**
 * Convert a request accessing a secondary key to a primary key undo
 * record, given it found a tuple.
 * Flush iproto header of the request to be reconstructed in txn_add_redo().
 *
 * @param request - request to fix
 * @param space - space corresponding to request
 * @param found_tuple - tuple found by secondary key
 */
void
request_rebind_to_primary_key(struct request *request, struct space *space,
			      struct tuple *found_tuple)
{
	Index *primary = index_find(space, 0);
	uint32_t key_len = found_tuple->bsize;
	char *key = (char *) region_alloc_xc(&fiber()->gc, key_len);
	const char *data = found_tuple->data;
	key_len = key_create_from_tuple(primary->key_def, data,
					key, key_len);
	assert(key_len <= found_tuple->bsize);
	request->key = key;
	request->key_end = key + key_len;
	request->index_id = 0;
	/* clear header to guarantee it's rebuild during commit */
	request->header = NULL;
}
