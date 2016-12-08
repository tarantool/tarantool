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
#include "request.h"
#include "xrow.h"
#include "tuple.h"
#include "schema.h"
#include "iproto_constants.h"

struct rmean *rmean_box;

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
	uint32_t key_len;
	char *key = tuple_extract_key(found_tuple, primary->key_def, &key_len);
	if (key == NULL)
		diag_raise();
	request->key = key;
	request->key_end = key + key_len;
	request->index_id = 0;
	/* Clear the header to ensure it's rebuilt at commit. */
	request->header = NULL;
}

void
request_normalize_ops(struct request *request)
{
	assert(request->type == IPROTO_UPSERT ||
	       request->type == IPROTO_UPDATE);
	assert(request->index_base != 0);
	assert(tuple_update_check_ops(region_aligned_alloc_xc_cb,
	       &fiber()->gc, request->ops, request->ops_end,
	       request->index_base) == 0);
	char *ops;
	ssize_t ops_len = request->ops_end - request->ops;
	ops = (char *)region_alloc_xc(&fiber()->gc, ops_len);
	char *ops_end = ops;
	const char *pos = request->ops;
	int op_cnt = mp_decode_array(&pos);
	ops_end = mp_encode_array(ops_end, op_cnt);
	int op_no = 0;
	for (op_no = 0; op_no < op_cnt; ++op_no) {
		int op_len = mp_decode_array(&pos);
		ops_end = mp_encode_array(ops_end, op_len);

		uint32_t op_name_len;
		const char  *op_name = mp_decode_str(&pos, &op_name_len);
		ops_end = mp_encode_str(ops_end, op_name, op_name_len);

		int field_no;
		if (mp_typeof(*pos) == MP_INT) {
			field_no = mp_decode_int(&pos);
			ops_end = mp_encode_int(ops_end, field_no);
		} else {
			field_no = mp_decode_uint(&pos);
			field_no -= request->index_base;
			ops_end = mp_encode_uint(ops_end, field_no);
		}

		if (*op_name == ':') {
			/**
			 * splice op adjust string pos and copy
			 * 2 additional arguments
			 */
			int str_pos;
			if (mp_typeof(*pos) == MP_INT) {
				str_pos = mp_decode_int(&pos);
				ops_end = mp_encode_int(ops_end, str_pos);
			} else {
				str_pos = mp_decode_uint(&pos);
				str_pos -= request->index_base;
				ops_end = mp_encode_uint(ops_end, str_pos);
			}
			const char *arg = pos;
			mp_next(&pos);
			memcpy(ops_end, arg, pos - arg);
			ops_end += pos - arg;
		}
		const char *arg = pos;
		mp_next(&pos);
		memcpy(ops_end, arg, pos - arg);
		ops_end += pos - arg;
	}
	request->ops = (const char *)ops;
	request->ops_end = (const char *)ops_end;
	request->index_base = 0;

	/* Clear the header to ensure it's rebuilt at commit. */
	request->header = NULL;
}
