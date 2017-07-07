#ifndef TARANTOOL_SQL_EXECUTE_H_INCLUDED
#define TARANTOOL_SQL_EXECUTE_H_INCLUDED
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct obuf;
struct region;
struct sql_bind;

/** EXECUTE request. */
struct sql_request {
	uint64_t sync;
	/** SQL statement text. */
	const char *sql_text;
	/** Array of parameters. */
	struct sql_bind *bind;
	/** Length of the @bind. */
	uint32_t bind_count;
};

/**
 * Parse the EXECUTE request.
 * @param[out] request Request to decode to.
 * @param data EXECUTE body.
 * @param len Byte length of the @data.
 * @param region Allocator.
 * @param sync Request sync.
 *
 * @retval  0 Sucess.
 * @retval -1 Format or memory error.
 */
int
sql_request_decode(struct sql_request *request, const char *data, uint32_t len,
		   struct region *region, uint64_t sync);

/**
 * Prepare and execute an SQL statement and encode the response in
 * an iproto message.
 * Response structure:
 * +----------------------------------------------+
 * | IPROTO_OK, sync, schema_version   ...        | iproto_header
 * +----------------------------------------------+---------------
 * | Body - a map with two keys.                  |
 * |                                              |
 * | IPROTO_BODY: {                               |
 * |     IPROTO_DESCRIPTION: [                    |
 * |         {IPROTO_FIELD_NAME: column name1},   |
 * |         {IPROTO_FIELD_NAME: column name2},   | iproto_body
 * |         ...                                  |
 * |     ],                                       |
 * |                                              |
 * |     IPROTO_DATA: [                           |
 * |         tuple, tuple, tuple, ...             |
 * |     ]                                        |
 * | }                                            |
 * +----------------------------------------------+
 *
 * @param request IProto request.
 * @param out Out buffer of the iproto message.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
int
sql_prepare_and_execute(struct sql_request *request, struct obuf *out);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif /* TARANTOOL_SQL_EXECUTE_H_INCLUDED */
