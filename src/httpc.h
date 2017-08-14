#ifndef TARNATOOL_HTTPC_H_INCLUDED
#define TARANTOOL_HTTPC_H_INCLUDED 1
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
#include <small/ibuf.h>
#include <small/region.h>
#include <small/mempool.h>
#include <tarantool_ev.h>

#include "diag.h"
#include "fiber_cond.h"

/** {{{ Environment */

typedef void CURLM;
typedef void CURL;
struct curl_slist;

/**
 * HTTP Client Statistics
 */
struct httpc_stat {
	uint64_t total_requests;
	uint64_t http_200_responses;
	uint64_t http_other_responses;
	uint64_t failed_requests;
	uint64_t active_requests;
	uint64_t sockets_added;
	uint64_t sockets_deleted;
};

/**
 * HTTP Client Environment
 */
struct httpc_env {
	/** libcurl multi handler*/
	CURLM *multi;

	/** libev timer watcher */
	struct ev_timer timer_event;

	/** Memory pool for requests */
	struct mempool req_pool;
	/** Memory pool for responses */
	struct mempool resp_pool;
	/** Memory pool for sockets */
	struct mempool sock_pool;

	/** Statistics */
	struct httpc_stat stat;
};

/**
 * @brief Creates  new HTTP client environment
 * @param env pointer to a structure to initialize
 * @param max_conn The maximum number of entries in connection cache
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
int
httpc_env_create(struct httpc_env *ctx, long max_conns);

/**
 * Destroy HTTP client environment
 * @param env pointer to a structure to destroy
 */
void
httpc_env_destroy(struct httpc_env *env);

/** Environment }}} */

/** {{{ Request */

/**
 * HTTP request
 */
struct httpc_request {
	/** Environment */
	struct httpc_env *env;
	/** Information associated with a specific easy handle */
	CURL *easy;
	/** HTTP headers */
	struct curl_slist *headers;
	/** Buffer for the request body */
	struct ibuf body;
};

/**
 * @brief Create a new HTTP request
 * @param ctx - reference to context
 * @return a new HTTP request object
 */
struct httpc_request *
httpc_request_new(struct httpc_env *env, const char *method,
		  const char *url);

/**
 * @brief Delete HTTP request
 * @param request - reference to object
 * @details Should be called even if error in execute appeared
 */
void
httpc_request_delete(struct httpc_request *req);

/**
 * Set HTTP header
 * @param req request
 * @param fmt format string
 * @param ... format arguments
 */
int
httpc_set_header(struct httpc_request *req, const char *fmt, ...);

/**
 * Sets body of request
 * @param req request
 * @param body body
 * @param bytes sizeof body
 */
int
httpc_set_body(struct httpc_request *req, const char *body, size_t size);

/**
 * Set TCP keep-alive probing
 * @param req request
 * @param idle delay, in seconds, that the operating system will wait
 *        while the connection is idle before sending keepalive probes
 * @param interval the interval, in seconds, that the operating system
 *        will wait between sending keepalive probes
 * @details Does nothing on libcurl < 7.25.0
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_TCP_KEEPALIVE.html
 */
int
httpc_set_keepalive(struct httpc_request *req, long idle, long interval);

/**
 * Set the "low speed time" - the time that the transfer speed should be
 * below the "low speed limit" for the library to consider it too slow and
 * abort.
 * @param req request
 * @param low_speed_time low speed time
 * @details If the download receives less than "low speed limit" bytes/second
 * during "low speed time" seconds, the operations is aborted.
 * You could i.e if you have a pretty high speed Connection,
 * abort if it is less than 2000 bytes/sec during 20 seconds;
 * @see httpc_set_low_speed_limit()
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_LOW_SPEED_TIME.html
 */
void
httpc_set_low_speed_time(struct httpc_request *req, long low_speed_time);

/**
 * Set the "low speed limit" - the average transfer speed in bytes per second
 * that the transfer should be below during "low speed time" seconds for the
 * library to consider it to be too slow and abort.
 * @param req request
 * @param low_speed_limit low speed limit
 * @details If the download receives less than "low speed limit" bytes/second
 * during "low speed time" seconds, the operations is aborted.
 * You could i.e if you have a pretty high speed Connection,
 * abort if it is less than 2000 bytes/sec during 20 seconds.
 * @see httpc_set_low_speed_time()
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_LOW_SPEED_LIMIT.html
 */
void
httpc_set_low_speed_limit(struct httpc_request *req, long low_speed_limit);

/**
 * Enables/Disables libcurl verbose mode
 * @param req request
 * @param verbose flag
 */
void
httpc_set_verbose(struct httpc_request *req, bool verbose);

/**
 * Specify directory holding CA certificates
 * @param req request
 * @param ca_path path to directory holding one or more certificates
 * to verify the peer with. The application does not have to keep the string
 * around after setting this option.
 */
void
httpc_set_ca_path(struct httpc_request *req, const char *ca_path);

/**
 * Specify path to Certificate Authority (CA) bundle
 * @param req request
 * @param ca_file - File holding one or more certificates
 * to verify the peer with. The application does not have to keep the string
 * around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_CAINFO.html
 */
void
httpc_set_ca_file(struct httpc_request *req, const char *ca_file);

/**
 * Enables/disables verification of the certificate's name (CN) against host
 * @param req request
 * @param verify flag
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html
 */
void
httpc_set_verify_host(struct httpc_request *req, long verify);

/**
 * Enables/disables verification of the peer's SSL certificate
 * @param req request
 * @param verify flag
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
 */
void
httpc_set_verify_peer(struct httpc_request *req, long verify);

/**
 * Specify path to private key for TLS ans SSL client certificate
 * @param req request
 * @param ssl_key - path to the private key. The application does not have to
 * keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSLKEY.html
 */
void
httpc_set_ssl_key(struct httpc_request *req, const char *ssl_key);

/**
 * Specify path to SSL client certificate
 * @param req request
 * @param ssl_cert - path to the client certificate. The application does not
 * have to keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSLCERT.html
 */
void
httpc_set_ssl_cert(struct httpc_request *req, const char *ssl_cert);

/**
 * This function does async HTTP request
 * @param request - reference to request object with filled fields
 * @param method - http method(case sensitive, e.g "GET")
 * @param url
 * @param timeout - timeout of waiting for libcurl api
 * \return pointer structure httpc_response or NULL
 * \details User recieves the reference to object response,
 * which should be destroyed with httpc_response_delete() at the end
 * Don't delete the request object before handling response!
 * That will destroy some fields in response object.
 */
struct httpc_response *
httpc_execute(struct httpc_request *req, double timeout);

/** Request }}} */

/** {{{ Response */

/**
 * HTTP Response
 */
struct httpc_response {
	/** Environment */
	struct httpc_env *ctx;
	/** HTTP status code */
	int status;
	/** Error message */
	const char *reason;
	/** Internal libcurl status code */
	int curl_code;
	/** buffer of headers */
	struct region headers;
	/** buffer of body */
	struct region body;
	/**
	 * Internal condition variable
	 * When request is given to curl-driver, client waits on this variable
	 * until the handler (callback function) gives a signal within variable
	 * */
	struct fiber_cond cond;
};

/**
 * Destroy response object
 * @param resp response object
 */
void
httpc_response_delete(struct httpc_response *resp);

/** Response }}} */

#endif /* TARANTOOL_HTTPC_H_INCLUDED */
