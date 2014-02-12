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
#include <lib/small/region.h>

#include <connector/c/include/tarantool/tnt.h>

#include "key.h"
#include "hash.h"
#include "options.h"
#include "space.h"
#include "sha1.h"
#include "ref.h"
#include "ts.h"
#include "indexate.h"

extern struct ts tss;

static inline void*
_alloc(void *arg, unsigned int size) {
	(void)arg;
	return region_alloc(&tss.ra, size);
}

struct tnt_tuple*
ts_update(struct tnt_request *r, struct tnt_tuple *old)
{
	(void) r;
	(void) old;
	/* TODO: MessagePack */
#if 0
	void *buf = NULL;
	uint32_t new_size = 0;
	uint32_t new_count = 0;
	try {
		struct tuple_update *u =
			tuple_update_prepare((region_alloc_func)_alloc, NULL,
                                 (const char*)(r->r.update.ops),
                                 (const char*)(r->r.update.ops + r->r.update.ops_size),
                                 (const char*)(old->data + sizeof(uint32_t)),
                                 (const char*)(old->data + sizeof(uint32_t) + old->size - sizeof(uint32_t)),
                                 old->cardinality,
                                 &new_size,
                                 &new_count);
		if (u == NULL)
			return NULL;

		buf = tnt_mem_alloc(new_size);
		if (buf == NULL) {
			region_reset(&tss.ra);
			return NULL;
		}
		memset(buf, 0, new_size);

		tuple_update_execute(u, (char*)buf);
	} catch (Exception *e) {
		if (buf)
			free(buf);
		region_reset(&tss.ra);
		fflush(NULL);
		printf("update failed\n");
		return NULL;
	}

	region_reset(&tss.ra);
	struct tnt_tuple *n = tnt_tuple_set_as(NULL, buf, new_size, new_count);
	if (n == NULL) {
		free(buf);
		return NULL;
	}

	free(buf);
	return n;
#endif
	return NULL;
}

} // extern "C"
