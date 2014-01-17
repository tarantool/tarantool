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
#include "request.h"
#include "txn.h"
#include "tuple.h"
#include "index.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "lua/call.h"
#include <errinj.h>
#include <pickle.h>
#include <fiber.h>
#include <scoped_guard.h>
#include <third_party/base64.h>

static const char *
read_tuple(const char **reqpos, const char *reqend)
{
	const char *tuple = *reqpos;
	if (unlikely(!mp_check(reqpos, reqend))) {
		say_error("\n"
			  "********************************************\n"
			  "* Found a corrupted tuple in a request!    *\n"
			  "* This can be either due to a memory       *\n"
			  "* corruption or a bug in the server.       *\n"
			  "* The tuple can not be loaded.             *\n"
			  "********************************************\n"
			  "Request tail, BASE64 encoded:               \n");

		uint32_t tuple_len = reqend - tuple;
		int base64_buflen = base64_bufsize(tuple_len);
		char *base64_buf = (char *) malloc(base64_buflen);
		int len = base64_encode(tuple, tuple_len,
					base64_buf, base64_buflen);
		write(STDERR_FILENO, base64_buf, len);
		free(base64_buf);
		tnt_raise(ClientError, ER_INVALID_MSGPACK);
	}

	if (unlikely(mp_typeof(*tuple) != MP_ARRAY))
		tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);

	return tuple;
}

enum dup_replace_mode
dup_replace_mode(uint32_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

static void
execute_replace(const struct request *request, struct txn *txn,
		struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);

	struct space *space = space_find(request->space_no);
	struct tuple *new_tuple = tuple_new(space->format, request->tuple,
					    request->tuple_end);
	TupleGuard guard(new_tuple);
	space_validate_tuple(space, new_tuple);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	txn_replace(txn, space, NULL, new_tuple, mode);
}

static void
execute_update(const struct request *request, struct txn *txn,
	       struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);
	/* Parse UPDATE request. */
	/** Search key  and key part count. */

	struct space *space = space_find(request->space_no);
	Index *pk = index_find(space, 0);
	/* Try to find the tuple by primary key. */
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL)
		return;

	/* Update the tuple. */
	struct tuple *new_tuple = tuple_update(space->format,
					       region_alloc_cb,
					       &fiber()->gc,
					       old_tuple, request->tuple,
					       request->tuple_end);
	TupleGuard guard(new_tuple);
	space_validate_tuple(space, new_tuple);
	txn_replace(txn, space, old_tuple, new_tuple, DUP_INSERT);
}

/** }}} */

static void
execute_select(const struct request *request, struct txn *txn,
	       struct port *port)
{
	(void) txn;
	struct space *space = space_find(request->space_no);
	Index *index = index_find(space, request->index_no);

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	uint32_t found = 0;
	uint32_t offset = request->offset;
	uint32_t limit = request->limit;

	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);

	struct iterator *it = index->position();
	key_validate(index->key_def, ITER_EQ, key, part_count);
	index->initIterator(it, ITER_EQ, key, part_count);

	struct tuple *tuple;
	while ((tuple = it->next(it)) != NULL) {
		if (offset > 0) {
			offset--;
			continue;
		}
		if (limit == found++)
			break;
		port_add_tuple(port, tuple);
	}
}

static void
execute_delete(const struct request *request, struct txn *txn,
	       struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);
	struct space *space = space_find(request->space_no);

	/* Try to find tuple by primary key */
	Index *pk = index_find(space, 0);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL)
		return;

	txn_replace(txn, space, old_tuple, NULL, DUP_REPLACE_OR_INSERT);
}

void
request_check_type(uint32_t type)
{
	if (type < IPROTO_SELECT || type >= IPROTO_REQUEST_MAX) {
		say_error("Unsupported request = %u", (unsigned) type);
		tnt_raise(IllegalParams, "unsupported command code, "
			  "check the error log");
	}
}

void
request_create(struct request *request, uint32_t type,
	       const char *data, uint32_t len)
{
	request_check_type(type);
	memset(request, 0, sizeof(*request));
	request->type = type;
	request->data = data;
	request->len = len;

	const char **reqpos = &data;
	const char *reqend = data + len;
	const char *s;

	switch (request->type) {
	case IPROTO_SELECT:
		request->execute = execute_select;
		request->space_no = pick_u32(reqpos, reqend);
		request->index_no = pick_u32(reqpos, reqend);
		request->offset = pick_u32(reqpos, reqend);
		request->limit = pick_u32(reqpos, reqend);
		request->key = *reqpos;
		/* Do not parse the tail, execute_select will do it */
		*reqpos = request->key_end = reqend;
		break;
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		request->execute = execute_replace;
		request->space_no = pick_u32(reqpos, reqend);
		request->tuple = read_tuple(reqpos, reqend);
		request->tuple_end = *reqpos;
		break;
	case IPROTO_UPDATE:
		request->execute = execute_update;
		request->space_no = pick_u32(reqpos, reqend);
		request->key = read_tuple(reqpos, reqend);
		request->key_end = *reqpos;
		request->tuple = *reqpos;
		request->tuple_end = *reqpos = reqend;
		/* Do not parse the tail, tuple_update will do it */
		break;
	case IPROTO_DELETE:
		request->execute = execute_delete;
		request->space_no = pick_u32(reqpos, reqend);
		request->key = read_tuple(reqpos, reqend);
		request->key_end = *reqpos;
		break;
	case IPROTO_CALL:
		request->execute = box_lua_call;
		s = *reqpos;
		if (unlikely(!mp_check(reqpos, reqend)))
			tnt_raise(ClientError, ER_INVALID_MSGPACK);
		if (unlikely(mp_typeof(*s) != MP_STR))
			tnt_raise(ClientError, ER_ARG_TYPE, 0, "STR");
		uint32_t namelen;
		request->key = mp_decode_str(&s, &namelen);
		request->key_end = request->key + namelen;
		request->tuple = read_tuple(reqpos, reqend);
		request->tuple_end = reqend;
		break;
	default:
		assert(false);
		break;
	}
	if (unlikely(*reqpos != reqend))
		tnt_raise(IllegalParams, "can't unpack request");
}
