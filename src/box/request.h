#ifndef TARANTOOL_BOX_REQUEST_H_INCLUDED
#define TARANTOOL_BOX_REQUEST_H_INCLUDED
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
#include <stdbool.h>
#include "xrow.h"

struct txn;
struct port;
extern int stat_base;

struct request
{
	/*
	 * Either log row, or network header, or NULL, depending
	 * on where this packet originated from: the write ahead
	 * log/snapshot, client request, or a Lua request.
	 */
	struct xrow_header *header;
	/**
	 * Request type - IPROTO type code
	 */
	uint32_t type;
	uint32_t space_id;
	uint32_t index_id;
	uint32_t offset;
	uint32_t limit;
	uint32_t iterator;
	/** Search key or proc name. */
	const char *key;
	const char *key_end;
	/** Insert/replace/upsert tuple or proc argument or update operations. */
	const char *tuple;
	const char *tuple_end;
	/** Upsert operations. */
	const char *ops;
	const char *ops_end;
	/** Base field offset for UPDATE/UPSERT, e.g. 0 for C and 1 for Lua. */
	int index_base;
};

/** The snapshot row metadata repeats the structure of REPLACE request. */
struct request_replace_body {
	uint8_t m_body;
	uint8_t k_space_id;
	uint8_t m_space_id;
	uint32_t v_space_id;
	uint8_t k_tuple;
} __attribute__((packed));

void
request_create(struct request *request, uint32_t code);

void
process_rw(struct request *request, struct port *port);

void
request_decode(struct request *request, const char *data, uint32_t len);

int
request_encode(struct request *request, struct iovec *iov);

/**
 * API of C stored function.
 */
typedef struct box_function_ctx box_function_ctx_t;
typedef int (*box_function_f)(box_function_ctx_t *ctx,
	     const char *args, const char *args_end);

#endif /* TARANTOOL_BOX_REQUEST_H_INCLUDED */
