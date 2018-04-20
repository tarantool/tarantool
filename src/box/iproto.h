#ifndef TARANTOOL_IPROTO_H_INCLUDED
#define TARANTOOL_IPROTO_H_INCLUDED
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

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/** The minimal value for net_msg_max. */
	IPROTO_MSG_MAX_MIN = 2,
	/**
	 * The size of tx fiber pool for iproto requests is
	 * limited by the number of concurrent iproto messages,
	 * with the ratio defined in this constant.
	 * The ratio is not 1:1 because of long-polling requests.
	 * Ideally we should not account long polling requests in
	 * the ratio, but currently we can not separate them from
	 * short-living requests, all requests are handled by the
	 * same pool. When the pool size is exhausted, message
	 * processing stops until some new fibers are freed up.
	 */
	IPROTO_FIBER_POOL_SIZE_FACTOR = 5,
};

extern unsigned iproto_readahead;

/**
 * Return size of memory used for storing network buffers.
 */
size_t
iproto_mem_used(void);

/**
 * Reset network statistics.
 */
void
iproto_reset_stat(void);

#if defined(__cplusplus)
} /* extern "C" */

void
iproto_init();

void
iproto_bind(const char *uri);

void
iproto_listen();

void
iproto_set_msg_max(int iproto_msg_max);

#endif /* defined(__cplusplus) */

#endif
