#ifndef TARANTOOL_CURL_H_INCLUDED
#define TARANTOOL_CURL_H_INCLUDED 1
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <curl/curl.h>

#include <small/mempool.h>

#include "tarantool_ev.h"

#include "diag.h"
#include "fiber_cond.h"

/**
 * CURL Statistics
 */
struct curl_stat {
	uint64_t sockets_added;
	uint64_t sockets_deleted;
	uint64_t active_requests;
};

/**
 * CURL Environment
 */
struct curl_env {
	/** libcurl multi handler. */
	CURLM *multi;
	/** Memory pool for sockets. */
	struct mempool sock_pool;
	/** libev timer watcher. */
	struct ev_timer timer_event;
	/** Statistics. */
	struct curl_stat stat;
};

/**
 * CURL Request
 */
struct curl_request {
	/** Internal libcurl status code. */
	int code;
	/** States that request is running. */
	bool in_progress;
	/** Information associated with a specific easy handle. */
	CURL *easy;
	/**
	 * When request is given to curl-driver, client waits on this variable
	 * until the handler (callback function) gives a signal within variable.
	 * */
	struct fiber_cond cond;
};

/**
 * @brief Create a new CURL environment
 * @param env pointer to a structure to initialize
 * @param max_conn The maximum number of entries in connection cache
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
int
curl_env_create(struct curl_env *env, long max_conns, long max_total_conns);

/**
 * Destroy HTTP client environment
 * @param env pointer to a structure to destroy
 */
void
curl_env_destroy(struct curl_env *env);

/**
 * Initialize a new CURL request
 * @param curl_request request
 * @retval  0 success
 * @retval -1 error, check diag
 */
int
curl_request_create(struct curl_request *curl_request);

/**
 * Cleanup CURL request
 * @param curl_request request
 */
void
curl_request_destroy(struct curl_request *curl_request);

/**
 * Execute CURL request
 * @param curl_request request
 * @param env environment
 * @param timeout - timeout of waiting for libcurl api
 */
CURLMcode
curl_execute(struct curl_request *curl_request, struct curl_env *env,
	     double timeout);

#endif /* TARANTOOL_CURL_H_INCLUDED */
