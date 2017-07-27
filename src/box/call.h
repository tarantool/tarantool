#ifndef INCLUDES_TARANTOOL_MOD_BOX_CALL_H
#define INCLUDES_TARANTOOL_MOD_BOX_CALL_H
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

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * CALL/EVAL request.
 */
struct call_request {
	/** Request header */
	const struct xrow_header *header;
	/** Function name for CALL request. MessagePack String. */
	const char *name;
	/** Expression for EVAL request. MessagePack String. */
	const char *expr;
	/** CALL/EVAL parameters. MessagePack Array. */
	const char *args;
	const char *args_end;
};

/**
 * Decode CALL/EVAL request from a given MessagePack map.
 * @param[out] call_request Request to decode to.
 * @param type Request type - either CALL or CALL_16 or EVAL.
 * @param sync Request sync.
 * @param data Request MessagePack encoded body.
 * @param len @data length.
 */
int
xrow_decode_call(const struct xrow_header *row, struct call_request *request);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

struct obuf;

struct box_function_ctx {
	struct call_request *request;
	struct port *port;
};

void
box_process_call(struct call_request *request, struct obuf *out);

void
box_process_eval(struct call_request *request, struct obuf *out);

#endif /* INCLUDES_TARANTOOL_MOD_BOX_CALL_H */
