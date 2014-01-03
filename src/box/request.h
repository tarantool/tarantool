#ifndef TARANTOOL_BOX_REQUEST_H_INCLUDED
#define TARANTOOL_BOX_REQUEST_H_INCLUDED
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
#include "trivia/util.h"
#include <stdbool.h>

struct txn;
struct port;

#define BOX_RETURN_TUPLE		0x01
#define BOX_ADD				0x02
#define BOX_REPLACE			0x04
#define BOX_ALLOWED_REQUEST_FLAGS	(BOX_RETURN_TUPLE | \
					 BOX_ADD | \
					 BOX_REPLACE)

#define REQUESTS(_)				\
	_(SELECT, 1)				\
        _(INSERT, 2)				\
        _(REPLACE, 3)				\
	_(UPDATE, 4)				\
	_(DELETE, 5)				\
	_(CALL, 6)

#define BOX_REQUEST_LAST CALL

ENUM(requests, REQUESTS);
extern const char *requests_strs[];

static inline bool
request_is_select(uint32_t type)
{
	return type == SELECT || type == CALL;
}

const char *request_name(uint32_t type);

struct request
{
	uint32_t type;
	uint32_t flags;
	union {
		struct {
			uint32_t space_no;
			uint32_t index_no;
			uint32_t offset;
			uint32_t limit;
			uint32_t key_count;
			const char *keys;
			const char *keys_end;
		} s; /* select */

		struct {
			uint32_t space_no;
			const char *tuple;
			const char *tuple_end;
		} r; /* replace */

		struct {
			uint32_t space_no;
			const char *key;
			const char *key_end;
			const char *expr;
			const char *expr_end;
		} u; /* update */

		struct {
			uint32_t space_no;
			const char *key;
			const char *key_end;
		} d; /* delete */

		struct {
			const char *procname;
			uint32_t procname_len;
			const char *args;
			const char *args_end;
		} c; /* call */
	};

	const char *data;
	uint32_t len;

	void (*execute)(const struct request *, struct txn *, struct port *);
};

void
request_create(struct request *request, uint32_t type, const char *data,
	       uint32_t len);

#endif /* TARANTOOL_BOX_REQUEST_H_INCLUDED */
