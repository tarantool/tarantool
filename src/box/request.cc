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

static struct space *
read_space(const char **reqpos, const char *reqend)
{
	uint32_t space_no = pick_u32(reqpos, reqend);
	return space_find(space_no);
}

enum dup_replace_mode
dup_replace_mode(uint32_t flags)
{
	return flags & BOX_ADD ? DUP_INSERT :
		flags & BOX_REPLACE ?
		DUP_REPLACE : DUP_REPLACE_OR_INSERT;
}

static void
execute_replace(struct request *request, struct txn *txn, struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);
	const char **reqpos = &request->data;
	const char *reqend = request->data + request->len;
	struct space *space = read_space(reqpos, reqend);
	request->flags |= (pick_u32(reqpos, reqend) &
			   BOX_ALLOWED_REQUEST_FLAGS);
	uint32_t field_count = pick_u32(reqpos, reqend);

	struct tuple *new_tuple = tuple_new(space->format, field_count,
					    reqpos, reqend);
	try {
		space_validate_tuple(space, new_tuple);
		enum dup_replace_mode mode = dup_replace_mode(request->flags);
		txn_replace(txn, space, NULL, new_tuple, mode);

	} catch (const Exception& e) {
		tuple_free(new_tuple);
		throw;
	}
}


static void
execute_update(struct request *request, struct txn *txn, struct port *port)
{
	(void) port;
	txn_add_redo(txn, request->type, request->data, request->len);
	const char **reqpos = &request->data;
	const char *reqend = request->data + request->len;
	struct space *space = read_space(reqpos, reqend);
	request->flags |= (pick_u32(reqpos, reqend) &
			   BOX_ALLOWED_REQUEST_FLAGS);
	/* Parse UPDATE request. */
	/** Search key  and key part count. */
	uint32_t key_part_count;
	const char *key = read_key(reqpos, reqend, &key_part_count);

	Index *pk = space_index(space, 0);
	/* Try to find the tuple by primary key. */
	primary_key_validate(&pk->key_def, key, key_part_count);
	struct tuple *old_tuple = pk->findByKey(key, key_part_count);

	if (old_tuple == NULL)
		return;

	/* Update the tuple. */
	struct tuple *new_tuple = tuple_update(space->format,
					       palloc_region_alloc,
					       fiber->gc_pool,
					       old_tuple, *reqpos, reqend);
	try {
		space_validate_tuple(space, new_tuple);
		txn_replace(txn, space, old_tuple, new_tuple, DUP_INSERT);
	} catch (const Exception& e) {
		tuple_free(new_tuple);
		throw;
	}
}

/** }}} */

static void
execute_select(struct request *request, struct txn *txn, struct port *port)
{
	(void) txn;
	const char **reqpos = &request->data;
	const char *reqend = request->data + request->len;
	struct space *space = read_space(reqpos, reqend);
	uint32_t index_no = pick_u32(reqpos, reqend);
	Index *index = index_find(space, index_no);
	uint32_t offset = pick_u32(reqpos, reqend);
	uint32_t limit = pick_u32(reqpos, reqend);
	uint32_t count = pick_u32(reqpos, reqend);
	if (count == 0)
		tnt_raise(IllegalParams, "tuple count must be positive");

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	uint32_t found = 0;

	for (uint32_t i = 0; i < count; i++) {

		/* End the loop if reached the limit. */
		if (limit == found)
			return;

		/* read key */
		uint32_t key_part_count;
		const char *key = read_key(reqpos, reqend, &key_part_count);

		struct iterator *it = index->position();
		key_validate(&index->key_def, ITER_EQ, key, key_part_count);
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
	if (*reqpos != reqend)
		tnt_raise(IllegalParams, "can't unpack request");
}

static void
execute_delete(struct request *request, struct txn *txn, struct port *port)
{
	(void) port;
	uint32_t type = request->type;
	txn_add_redo(txn, type, request->data, request->len);
	const char **reqpos = &request->data;
	const char *reqend = request->data + request->len;
	struct space *space = read_space(reqpos, reqend);
	if (type == DELETE) {
		request->flags |= pick_u32(reqpos, reqend) &
			BOX_ALLOWED_REQUEST_FLAGS;
	}
	/* read key */
	uint32_t key_part_count;
	const char *key = read_key(reqpos, reqend, &key_part_count);
	/* Try to find tuple by primary key */
	Index *pk = space_index(space, 0);
	primary_key_validate(&pk->key_def, key, key_part_count);
	struct tuple *old_tuple = pk->findByKey(key, key_part_count);

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

	switch (request->type) {
	case REPLACE:
		request->execute = execute_replace;
		break;
	case SELECT:
		request->execute = execute_select;
		break;
	case UPDATE:
		request->execute = execute_update;
		break;
	case DELETE_1_3:
	case DELETE:
		request->execute = execute_delete;
		break;
	case CALL:
		request->execute = box_lua_execute;
		break;
	default:
		assert(false);
		break;
	}
}
