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
#include "box_lua.h"
#include <errinj.h>
#include <pickle.h>
#include <fiber.h>
#include <scoped_guard.h>

STRS(requests, REQUESTS);

static const char *
read_key(const char **reqpos, const char *reqend, uint32_t *key_part_count)
{
	*key_part_count = pick_u32(reqpos, reqend);
	const char *key = *key_part_count ? *reqpos : NULL;
	/* Advance remaining fields of a key */
	for (uint32_t i = 0; i < *key_part_count; i++)
		pick_field(reqpos, reqend);
	return key;
}

enum dup_replace_mode
dup_replace_mode(uint32_t flags)
{
	return flags & BOX_ADD ? DUP_INSERT :
		flags & BOX_REPLACE ?
		DUP_REPLACE : DUP_REPLACE_OR_INSERT;
}

static void
execute_replace(const struct request *request, struct txn *txn,
		struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);

	struct space *space = space_find(request->r.space_no);
	const char *tuple = request->r.tuple;
	uint32_t field_count = pick_u32(&tuple, request->r.tuple_end);

	struct tuple *new_tuple = tuple_new(space->format, field_count,
					    &tuple, request->r.tuple_end);
	TupleGuard guard(new_tuple);
	space_validate_tuple(space, new_tuple);
	enum dup_replace_mode mode = dup_replace_mode(request->flags);
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

	struct space *space = space_find(request->u.space_no);
	Index *pk = index_find(space, 0);
	/* Try to find the tuple by primary key. */
	primary_key_validate(pk->key_def, request->u.key,
			     request->u.key_part_count);
	struct tuple *old_tuple = pk->findByKey(request->u.key,
						request->u.key_part_count);

	if (old_tuple == NULL)
		return;

	/* Update the tuple. */
	struct tuple *new_tuple = tuple_update(space->format,
					       region_alloc_cb,
					       &fiber->gc,
					       old_tuple, request->u.expr,
					       request->u.expr_end);
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
	struct space *space = space_find(request->s.space_no);
	Index *index = index_find(space, request->s.index_no);

	if (request->s.key_count == 0)
		tnt_raise(IllegalParams, "tuple count must be positive");

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	uint32_t found = 0;
	const char *keys = request->s.keys;
	uint32_t offset = request->s.offset;
	uint32_t limit = request->s.limit;
	for (uint32_t i = 0; i < request->s.key_count; i++) {

		/* End the loop if reached the limit. */
		if (limit == found)
			return;

		/* read key */
		uint32_t key_part_count;
		const char *key = read_key(&keys, request->s.keys_end,
					   &key_part_count);

		struct iterator *it = index->position();
		key_validate(index->key_def, ITER_EQ, key, key_part_count);
		index->initIterator(it, ITER_EQ, key, key_part_count);

		struct tuple *tuple;
		while ((tuple = it->next(it)) != NULL) {
			if (offset > 0) {
				offset--;
				continue;
			}

			port_add_tuple(port, tuple, BOX_RETURN_TUPLE);

			if (limit == ++found)
				break;
		}
	}

	if (keys != request->s.keys_end)
		tnt_raise(IllegalParams, "can't unpack request");
}

static void
execute_delete(const struct request *request, struct txn *txn,
	       struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);
	struct space *space = space_find(request->d.space_no);

	/* Try to find tuple by primary key */
	Index *pk = index_find(space, 0);
	primary_key_validate(pk->key_def, request->d.key,
			     request->d.key_part_count);
	struct tuple *old_tuple = pk->findByKey(request->d.key,
						request->d.key_part_count);

	if (old_tuple == NULL)
		return;

	txn_replace(txn, space, old_tuple, NULL, DUP_REPLACE_OR_INSERT);
}

/** To collects stats, we need a valid request type.
 * We must collect stats before execute.
 * Check request type here for now.
 */
static bool
request_check_type(uint32_t type)
{
	return (type != REPLACE && type != SELECT &&
		type != UPDATE && type != DELETE_1_3 &&
		type != DELETE && type != CALL);
}

const char *
request_name(uint32_t type)
{
	if (request_check_type(type))
		return "unsupported";
	return requests_strs[type];
}

void
request_create(struct request *request, uint32_t type, const char *data,
	       uint32_t len)
{
	if (request_check_type(type)) {
		say_error("Unsupported request = %" PRIi32 "", type);
		tnt_raise(IllegalParams, "unsupported command code, "
			  "check the error log");
	}
	memset(request, 0, sizeof(*request));
	request->type = type;
	request->data = data;
	request->len = len;
	request->flags = 0;

	const char **reqpos = &data;
	const char *reqend = data + len;

	switch (request->type) {
	case REPLACE:
		request->execute = execute_replace;
		request->r.space_no = pick_u32(reqpos, reqend);
		request->flags |= (pick_u32(reqpos, reqend) &
				   BOX_ALLOWED_REQUEST_FLAGS);
		request->r.tuple = *reqpos;
		/* Do not parse the tail, execute_replace will do it */
		request->r.tuple_end = reqend;
		break;
	case SELECT:
		request->execute = execute_select;
		request->s.space_no = pick_u32(reqpos, reqend);
		request->s.index_no = pick_u32(reqpos, reqend);
		request->s.offset = pick_u32(reqpos, reqend);
		request->s.limit = pick_u32(reqpos, reqend);
		request->s.key_count = pick_u32(reqpos, reqend);
		request->s.keys = *reqpos;
		/* Do not parse the tail, execute_select will do it */
		request->s.keys_end = reqend;
		break;
	case UPDATE:
		request->execute = execute_update;
		request->u.space_no = pick_u32(reqpos, reqend);
		request->flags |= (pick_u32(reqpos, reqend) &
				   BOX_ALLOWED_REQUEST_FLAGS);
		request->u.key = read_key(reqpos, reqend,
					       &request->u.key_part_count);
		request->u.key_end = *reqpos;
		request->u.expr = *reqpos;
		/* Do not parse the tail, tuple_update will do it */
		request->u.expr_end = reqend;
		break;
	case DELETE_1_3:
	case DELETE:
		request->execute = execute_delete;
		request->d.space_no = pick_u32(reqpos, reqend);
		if (type == DELETE) {
			request->flags |= pick_u32(reqpos, reqend) &
				BOX_ALLOWED_REQUEST_FLAGS;
		}
		request->d.key = read_key(reqpos, reqend,
					    &request->d.key_part_count);
		request->d.key_end = *reqpos;
		if (*reqpos != reqend)
			tnt_raise(IllegalParams, "can't unpack request");
		break;
	case CALL:
		request->execute = box_lua_execute;
		request->flags |= (pick_u32(reqpos, reqend) &
				   BOX_ALLOWED_REQUEST_FLAGS);
		request->c.procname = pick_field_str(reqpos, reqend,
						     &request->c.procname_len);
		request->c.args = read_key(reqpos, reqend,
					   &request->c.arg_count);;
		request->c.args_end = *reqpos;
		if (*reqpos != reqend)
			tnt_raise(IllegalParams, "can't unpack request");
		break;
	default:
		assert(false);
		break;
	}
}
