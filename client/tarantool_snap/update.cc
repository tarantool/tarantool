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

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <exception.h>
#include <pickle.h>
#include <src/box/tuple_update.h>

extern "C" {

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include <connector/c/include/tarantool/tnt.h>

#include "key.h"
#include "hash.h"
#include "options.h"
#include "space.h"
#include "sha1.h"
#include "ref.h"
#include "ts.h"
#include "indexate.h"

struct tnt_tuple*
ts_update(struct tnt_request *r, struct tnt_tuple *old)
{
	(void)r;
	(void)old;

	uint32_t new_size = 0;
	uint32_t new_count = 0;

	char *data = old->data + sizeof(uint32_t);
	struct tuple_update *u =
		tuple_update_prepare(NULL, NULL,
		                     (const char*)(r->r.update.ops),
		                     (const char*)(r->r.update.ops + r->r.update.ops_size),
		                     (const char*)(data),
		                     (const char*)(data + old->size - sizeof(uint32_t)),
		                     old->cardinality,
		                     &new_size,
		                     &new_count);
	(void)u;

#if 0
	const void *p = r->r.update.ops;
	const void **reqpos = &p;
	const void *reqend = r->r.update.ops + r->r.update.ops_size;

	u32 op_cnt = r->r.update.opc;

	struct update_op *ops = update_read_ops(reqpos, reqend, op_cnt);
	struct rope *rope =
		update_create_rope_from(ops, ops + op_cnt,
		                        (u8*)old->data + sizeof(uint32_t),
		                        old->cardinality,
		                        old->size - sizeof(uint32_t));

	size_t new_tuple_len = update_calc_new_tuple_length(rope);
	struct tuple *new_tuple = tuple_alloc(new_tuple_len);

	struct tnt_tuple *n = NULL;

	@try {
		update_do_ops(rope, new_tuple);

		n = tnt_tuple_set(NULL, &new_tuple->field_count,
                          new_tuple->bsize + sizeof(uint32_t));
	} @catch (tnt_Exception *e) {
		printf("update error\n");
	} @finally {
		tuple_free(new_tuple);
	}
	return n;
#endif
	return NULL;
}

} // extern "C"
