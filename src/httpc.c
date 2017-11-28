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

#include "httpc.h"

#include <assert.h>
#include <curl/curl.h>

#include "fiber.h"
#include "errinj.h"

/**
 * libcurl callback for CURLOPT_WRITEFUNCTION
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
static size_t
curl_easy_write_cb(char *ptr, size_t size, size_t nmemb, void *ctx)
{
	struct httpc_request *req = (struct httpc_request *) ctx;
	const size_t bytes = size * nmemb;

	char *p = region_alloc(&req->resp_body, bytes);
	if (p == NULL) {
		diag_set(OutOfMemory, bytes, "ibuf", "httpc body");
		return 0;
	}
	memcpy(p, ptr, bytes);
	return bytes;
}

/**
 * libcurl callback for CURLOPT_HEADERFUNCTION
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_HEADERFUNCTION.html
 */
static size_t
curl_easy_header_cb(char *buffer, size_t size, size_t nitems, void *ctx)
{
	struct httpc_request *req = (struct httpc_request *) ctx;
	const size_t bytes = size * nitems;
	char *p = region_alloc(&req->resp_headers, bytes);
	if (p == NULL) {
		diag_set(OutOfMemory, bytes, "ibuf", "httpc header");
		return 0;
	}
	memcpy(p, buffer, bytes);
	return bytes;
}

int
httpc_env_create(struct httpc_env *env, int max_conns)
{
	memset(env, 0, sizeof(*env));
	mempool_create(&env->req_pool, &cord()->slabc,
			sizeof(struct httpc_request));

	return curl_env_create(&env->curl_env, max_conns);
}

void
httpc_env_destroy(struct httpc_env *ctx)
{
	assert(ctx);

	curl_env_destroy(&ctx->curl_env);

	mempool_destroy(&ctx->req_pool);
}

struct httpc_request *
httpc_request_new(struct httpc_env *env, const char *method,
		  const char *url)
{
	struct httpc_request *req = mempool_alloc(&env->req_pool);
	if (req == NULL) {
		diag_set(OutOfMemory, sizeof(struct httpc_request),
			 "mempool", "httpc_request");
		return NULL;
	}
	memset(req, 0, sizeof(*req));
	req->env = env;
	region_create(&req->resp_headers, &cord()->slabc);
	region_create(&req->resp_body, &cord()->slabc);

	if (curl_request_create(&req->curl_request) != 0)
		return NULL;

	if (strcmp(method, "GET") == 0) {
		curl_easy_setopt(req->curl_request.easy, CURLOPT_HTTPGET, 1L);
	} else if (strcmp(method, "HEAD") == 0) {
		curl_easy_setopt(req->curl_request.easy, CURLOPT_NOBODY, 1L);
	} else if (strcmp(method, "POST") == 0 ||
		   strcmp(method, "PUT") == 0 ||
		   strcmp(method, "PATCH")) {
		/*
		 * Set CURLOPT_POSTFIELDS to "" and CURLOPT_POSTFIELDSSIZE 0
		 * to avoid the read callback in any cases even if user
		 * forgot to call httpc_set_body() for POST request.
		 * @see https://curl.haxx.se/libcurl/c/CURLOPT_POSTFIELDS.html
		 */
		curl_easy_setopt(req->curl_request.easy, CURLOPT_POST, 1L);
		curl_easy_setopt(req->curl_request.easy, CURLOPT_POSTFIELDS, "");
		curl_easy_setopt(req->curl_request.easy, CURLOPT_POSTFIELDSIZE, 0);
		curl_easy_setopt(req->curl_request.easy, CURLOPT_CUSTOMREQUEST, method);
		if (httpc_set_header(req, "Accept: */*") < 0)
			goto error;
	} else {
		curl_easy_setopt(req->curl_request.easy, CURLOPT_CUSTOMREQUEST, method);
	}

	curl_easy_setopt(req->curl_request.easy, CURLOPT_URL, url);

	curl_easy_setopt(req->curl_request.easy, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_SSL_VERIFYPEER, 1);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_WRITEFUNCTION,
			 curl_easy_write_cb);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_HEADERFUNCTION,
			 curl_easy_header_cb);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_HTTP_VERSION,
			 CURL_HTTP_VERSION_1_1);

	ibuf_create(&req->body, &cord()->slabc, 1);

	return req;
error:
	mempool_free(&env->req_pool, req);
	return NULL;
}

void
httpc_request_delete(struct httpc_request *req)
{
	if (req->headers != NULL)
		curl_slist_free_all(req->headers);

	curl_request_destroy(&req->curl_request);

	ibuf_destroy(&req->body);
	region_destroy(&req->resp_headers);
	region_destroy(&req->resp_body);

	mempool_free(&req->env->req_pool, req);
}

int
httpc_set_header(struct httpc_request *req, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const char *header = tt_vsprintf(fmt, ap);
	va_end(ap);

	struct curl_slist *l = curl_slist_append(req->headers, header);
	if (l == NULL) {
		diag_set(OutOfMemory, strlen(header), "curl", "http header");
		return -1;
	}
	req->headers = l;
	return 0;
}

int
httpc_set_body(struct httpc_request *req, const char *body, size_t size)
{
	ibuf_reset(&req->body);
	char *chunk = ibuf_alloc(&req->body, size);
	if (chunk == NULL) {
		diag_set(OutOfMemory, size, "ibuf", "http request body");
		return -1;
	}
	memcpy(chunk, body, size);

	curl_easy_setopt(req->curl_request.easy, CURLOPT_POSTFIELDS, req->body.buf);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_POSTFIELDSIZE, size);

	if (httpc_set_header(req, "Content-Length: %zu", size) != 0)
		return -1;

	return 0;
}

int
httpc_set_keepalive(struct httpc_request *req, long idle, long interval)
{
#if (LIBCURL_VERSION_MAJOR >= 7 && LIBCURL_VERSION_MINOR >= 25)
	if (idle > 0 && interval > 0) {
		curl_easy_setopt(req->curl_request.easy, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(req->curl_request.easy, CURLOPT_TCP_KEEPIDLE, idle);
		curl_easy_setopt(req->curl_request.easy, CURLOPT_TCP_KEEPINTVL, interval);
		if (httpc_set_header(req, "Connection: Keep-Alive") < 0 ||
		    httpc_set_header(req, "Keep-Alive: timeout=%d",
				     (int) idle) < 0) {
			return -1;
		}
	} else {
		if (httpc_set_header(req, "Connection: close") < 0) {
			return -1;
		}
	}
#else /** < 7.25.0 */
/** Libcurl version < 7.25.0 doesn't support keep-alive feature */
	(void) req;
	(void) idle;
	(void) interval;
#endif
	return 0;
}

void
httpc_set_low_speed_time(struct httpc_request *req, long low_speed_time)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_LOW_SPEED_TIME, low_speed_time);
}

void
httpc_set_low_speed_limit(struct httpc_request *req, long low_speed_limit)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_LOW_SPEED_LIMIT, low_speed_limit);
}

void
httpc_set_verbose(struct httpc_request *req, bool curl_verbose)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_VERBOSE, curl_verbose);
}

void
httpc_set_ca_path(struct httpc_request *req, const char *ca_path)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_CAPATH, ca_path);
}

void
httpc_set_ca_file(struct httpc_request *req, const char *ca_file)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_CAINFO, ca_file);
}

int
httpc_set_unix_socket(struct httpc_request *req, const char *unix_socket)
{
#ifdef CURL_VERSION_UNIX_SOCKETS
	curl_easy_setopt(req->curl_request.easy, CURLOPT_UNIX_SOCKET_PATH,
			 unix_socket);
	return 0;
#else
#pragma message "unix sockets not supported, please upgrade libcurl to 7.40.0"
	(void) req;
	(void) unix_socket;
	diag_set(IllegalParams, "tarantool was built without unix socket support,"
				" please upgrade libcurl to 7.40.0 and rebuild");
	return -1;
#endif
}

void
httpc_set_verify_host(struct httpc_request *req, long verify)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_SSL_VERIFYHOST, verify);
}

void
httpc_set_verify_peer(struct httpc_request *req, long verify)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_SSL_VERIFYPEER, verify);
}

void
httpc_set_ssl_key(struct httpc_request *req, const char *ssl_key)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_SSLKEY, ssl_key);
}

void
httpc_set_ssl_cert(struct httpc_request *req, const char *ssl_cert)
{
	curl_easy_setopt(req->curl_request.easy, CURLOPT_SSLCERT, ssl_cert);
}

int
httpc_execute(struct httpc_request *req, double timeout)
{
	struct httpc_env *env = req->env;

	curl_easy_setopt(req->curl_request.easy, CURLOPT_WRITEDATA, (void *) req);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_HEADERDATA, (void *) req);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_PRIVATE, (void *) &req->curl_request);
	curl_easy_setopt(req->curl_request.easy, CURLOPT_HTTPHEADER, req->headers);

	++env->stat.total_requests;

	if (curl_execute(&req->curl_request, &env->curl_env, timeout) != CURLM_OK)
		return -1;
	ERROR_INJECT_RETURN(ERRINJ_HTTPC_EXECUTE);
	long longval = 0;
	switch (req->curl_request.code) {
	case CURLE_OK:
		curl_easy_getinfo(req->curl_request.easy, CURLINFO_RESPONSE_CODE, &longval);
		req->status = (int) longval;
		/* TODO: get real response string from resp->headers */
		req->reason = "Ok";
		if (req->status == 200) {
			++env->stat.http_200_responses;
		} else {
			++env->stat.http_other_responses;
		}
		break;
	case CURLE_SSL_CACERT:
	case CURLE_PEER_FAILED_VERIFICATION:
		/* 495 SSL Certificate Error (nginx non-standard) */
		req->status = 495;
		req->reason = curl_easy_strerror(req->curl_request.code);
		++env->stat.failed_requests;
		break;
	case CURLE_OPERATION_TIMEDOUT:
		/* 408 Request Timeout (nginx non-standard) */
		req->status = 408;
		req->reason = curl_easy_strerror(req->curl_request.code);
		++env->stat.failed_requests;
		break;
	case CURLE_GOT_NOTHING:
		/* 444 No Response */
		req->status = 444;
		req->reason = curl_easy_strerror(req->curl_request.code);
		++env->stat.failed_requests;
		break;
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT:
		/* 595 Connection Problem (AnyEvent non-standard) */
		req->status = 595;
		req->reason = curl_easy_strerror(req->curl_request.code);
		++env->stat.failed_requests;
		break;
	case CURLE_WRITE_ERROR:
		/* Diag is already set by curl_write_cb() */
		assert(!diag_is_empty(&fiber()->diag));
		++env->stat.failed_requests;
		return -1;
	case CURLE_OUT_OF_MEMORY:
		diag_set(OutOfMemory, 0, "curl", "internal");
		++env->stat.failed_requests;
		return -1;
	default:
		curl_easy_getinfo(req->curl_request.easy, CURLINFO_OS_ERRNO, &longval);
		errno = longval ? longval : EINVAL;
		diag_set(SystemError, "curl: %s",
			 curl_easy_strerror(req->curl_request.code));
		++env->stat.failed_requests;
		return -1;
	}

	return 0;
}
