#ifndef TARANTOOL_BOX_REQUEST_H_INCLUDED
#define TARANTOOL_BOX_REQUEST_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct request;
struct space;
struct tuple;
struct region;

/**
 * Given old and new tuples, initialize the corresponding
 * request to be written to WAL.
 *
 * @param request - request to fix
 * @param space - space corresponding to request
 * @param old_data - the old tuple data
 * @param old_size - size of the old data
 * @param new_data - the new tuple data
 * @param new_size - size of the new data
 * @param region - region where request parts will be allocated
 * @param preserve_insert_type - do not turn INSERT requests into REPLACE
 *
 * If old_data and new_data are the same, the request is turned into NOP.
 * If new_data is NULL, the request is turned into DELETE(old_data).
 * If new_data is not NULL, the request is turned into REPLACE(new_data),
 * or into INSERT(new_data)/UPSERT(new_data) when preserve_request_type is true
 * and the original request was INSERT or UPSERT.
 */
int
request_create_from_tuple(struct request *request, struct space *space,
			  const char *old_data, uint32_t old_size,
			  const char *new_data, uint32_t new_size,
			  struct region *region, bool preserve_request_type);

/**
 * Convert a request accessing a secondary key to a primary
 * key undo record, given it found a tuple.
 * Flush iproto header of the request to be reconstructed in
 * txn_add_redo().
 *
 * @param request - request to fix
 * @param space - space corresponding to request
 * @param found_tuple - tuple found by secondary key
 * @param region - region where request key will be allocated
 */
void
request_rebind_to_primary_key(struct request *request, struct space *space,
			      struct tuple *found_tuple,
			      struct region *region);

/**
 * Handle INSERT/REPLACE in a space with a sequence attached.
 *
 * @param request - request to fix
 * @param space - space corresponding to request
 * @param region -region where sequence value will be allocated
 */
int
request_handle_sequence(struct request *request, struct space *space,
			struct region *region);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_REQUEST_H_INCLUDED */
