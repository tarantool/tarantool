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
#include "region.h"
#include "ts.h"
#include "indexate.h"

extern struct ts tss;

struct tnt_tuple*
ts_update(struct tnt_request *r, struct tnt_tuple *old)
{
	uint32_t new_size = 0;
	uint32_t new_count = 0;

	char *data = old->data + sizeof(uint32_t);
	struct tuple_update *u =
		tuple_update_prepare((region_alloc_func)ts_region_alloc, &tss.rup,
		                     (const char*)(r->r.update.ops),
		                     (const char*)(r->r.update.ops + r->r.update.ops_size),
		                     (const char*)(data),
		                     (const char*)(data + old->size - sizeof(uint32_t)),
		                     old->cardinality,
		                     &new_size,
		                     &new_count);
	if (u == NULL)
		return NULL;

	new_size += sizeof(uint32_t);

	void *buf = tnt_mem_alloc(new_size);
	if (buf == NULL) {
		ts_region_reset(&tss.rup);
		return NULL;
	}

	try {
		tuple_update_execute(u, (char*)buf + sizeof(uint32_t));
	} catch (const Exception&) {
		free(buf);
		ts_region_reset(&tss.rup);
		return NULL;
	}

	ts_region_reset(&tss.rup);
	struct tnt_tuple *n = tnt_tuple_set_as(NULL, buf, new_size, new_count);
	if (n == NULL) {
		free(buf);
		return NULL;
	}

	free(buf);
	return n;
}

} // extern "C"
