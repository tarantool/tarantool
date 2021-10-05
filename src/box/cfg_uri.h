#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "sio.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	MAX_OPT_NAME_LEN = 256,
	/** Maximum count of listening sockets */
	IPROTO_LISTEN_SOCKET_MAX = 20,
	/**
	 * We need `SERVICE_NAME_MAXLEN` bytes for each listen
	 * address and two bytes for delimiter `,` between them.
	 */
	IPROTO_LISTEN_INFO_MAXLEN =
		(SERVICE_NAME_MAXLEN + 2) * IPROTO_LISTEN_SOCKET_MAX,
};

struct cfg_uri_array;

typedef int (*cfg_uri_array_checker)(const char *, const char *);

struct cfg_uri_array *
cfg_uri_array_new(void);

void
cfg_uri_array_delete(struct cfg_uri_array *uri_array);

int
cfg_uri_array_create(const char *option_name, struct cfg_uri_array *uri_array);

void
cfg_uri_array_destroy(struct cfg_uri_array *uri_array);

int
cfg_uri_array_size(const struct cfg_uri_array *uri_array);

const char *
cfg_uri_array_get_uri(const struct cfg_uri_array *uri_array, int idx);

int
cfg_uri_array_check(const struct cfg_uri_array *uri_array,
		    cfg_uri_array_checker,
		    const char *option_name);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
