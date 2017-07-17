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

/**
 * Process events
 */
static void
curl_multi_process(CURLM *multi, curl_socket_t sockfd, int events)
{
	/*
	 * Notify curl about events
	 */

	CURLMcode code;
	int still_running = 0;
	/*
	 * From https://curl.haxx.se/libcurl/c/curl_multi_socket_action.html:
	 * Before version 7.20.0: If you receive CURLM_CALL_MULTI_PERFORM,
	 * this basically means that you should call curl_multi_socket_action
	 * again before you wait for more actions on libcurl's sockets.
	 * You don't have to do it immediately, but the return code means that
	 * libcurl may have more data available to return or that there may be
	 * more data to send off before it is "satisfied".
	 */
	do {
		code = curl_multi_socket_action(multi, sockfd, events,
						&still_running);
	} while (code == CURLM_CALL_MULTI_PERFORM);
	if (code != CURLM_OK) {
		/* Sic: we can't handle errors properly in EV callbacks */
		say_error("curl_multi_socket_action failed for sockfd=%d: %s",
			  sockfd, curl_multi_strerror(code));
	}

	/*
	 * Check for resuls
	 */

	CURLMsg *msg;
	int msgs_left;
	while ((msg = curl_multi_info_read(multi, &msgs_left))) {
		if (msg->msg != CURLMSG_DONE)
			continue;

		CURL *easy = msg->easy_handle;
		CURLcode curl_code = msg->data.result;
		struct httpc_response *resp = NULL;
		curl_easy_getinfo(easy, CURLINFO_PRIVATE, (void *) &resp);

		resp->curl_code = (int) curl_code;
		fiber_cond_signal(&resp->cond);
	}
}

/**
 * libev timer callback used by curl_multi_timer_cb()
 * @see curl_multi_timer_cb()
 */
static void
curl_timer_cb(struct ev_loop *loop, struct ev_timer *watcher, int revents)
{
	(void) loop;
	(void) revents;
	struct httpc_env *env = (struct httpc_env *) watcher->data;

	say_debug("httpc %p: event timer", env);
	curl_multi_process(env->multi, CURL_SOCKET_TIMEOUT, 0);
}

/**
 * libcurl callback for CURLMOPT_TIMERFUNCTION
 * @see https://curl.haxx.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html
 */
static int
curl_multi_timer_cb(CURLM *multi, long timeout_ms, void *envp)
{
	(void) multi;
	struct httpc_env *env = (struct httpc_env *) envp;

	say_debug("httpc %p: wait timeout=%ldms", env, timeout_ms);
	ev_timer_stop(loop(), &env->timer_event);
	if (timeout_ms > 0) {
		/*
		 * From CURLMOPT_TIMERFUNCTION manual:
		 * Your callback function should install a non-repeating timer
		 * with an interval of timeout_ms. Each time that timer fires,
		 * call curl_multi_socket_action().
		 */
		double timeout = (double) timeout_ms / 1000.0;
		ev_timer_init(&env->timer_event, curl_timer_cb, timeout, 0);
		ev_timer_start(loop(), &env->timer_event);
		return 0;
	} else if (timeout_ms == 0) {
		/*
		 * From CURLMOPT_TIMERFUNCTION manual:
		 * A timeout_ms value of 0 means you should call
		 * curl_multi_socket_action or curl_multi_perform (once) as
		 * soon as possible.
		 */
		curl_timer_cb(loop(), &env->timer_event, 0);
		return 0;
	} else {
		assert(timeout_ms == -1);
		/*
		 * From CURLMOPT_TIMERFUNCTION manual:
		 * A timeout_ms value of -1 means you should delete your
		 * timer.
		 */
		return 0;
	}
}

/** Human-readable names for libev events. Used for debug. */
static const char *evstr[] = {
	[EV_READ] = "IN",
	[EV_WRITE] = "OUT",
	[EV_READ | EV_WRITE] = "INOUT",
};

/**
 * libev I/O callback used by curl_multi_sock_cb()
 */
static void
curl_sock_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	(void) loop;
	struct httpc_env *env = (struct httpc_env *) watcher->data;
	int fd = watcher->fd;

	say_debug("httpc %p: event fd=%d %s", env, fd, evstr[revents]);
	const int action = ((revents & EV_READ  ? CURL_POLL_IN  : 0) |
			    (revents & EV_WRITE ? CURL_POLL_OUT : 0));
	curl_multi_process(env->multi, fd, action);
}

/**
 * libcurl callback for CURLMOPT_SOCKETFUNCTION
 * @see https://curl.haxx.se/libcurl/c/CURLMOPT_SOCKETFUNCTION.html
 */
static int
curl_multi_sock_cb(CURL *easy, curl_socket_t fd, int what, void *envp,
		   void *watcherp)
{
	(void) easy;
	struct httpc_env *env = (struct httpc_env *) envp;
	struct ev_io *watcher = (struct ev_io *) watcherp;

	if (what == CURL_POLL_REMOVE) {
		say_debug("httpc %p: remove fd=%d", env, fd);
		assert(watcher != NULL);
		ev_io_stop(loop(), watcher);
		++env->stat.sockets_deleted;
		mempool_free(&env->sock_pool, watcher);
		return 0;
	}

	if (watcher == NULL) {
		watcher = mempool_alloc(&env->sock_pool);
		if (watcher == NULL) {
			diag_set(OutOfMemory, sizeof(*watcher), "mempool",
				 "httpc sock");
			return -1;
		}
		ev_io_init(watcher, curl_sock_cb, fd, 0);
		watcher->data = env;
		++env->stat.sockets_added;
		curl_multi_assign(env->multi, fd, watcher);
		say_debug("httpc %p: add fd=%d", env, fd);
	}

	if (what == CURL_POLL_NONE)
		return 0; /* register, not interested in readiness (yet) */

	const int events = ((what & CURL_POLL_IN  ? EV_READ  : 0) |
			    (what & CURL_POLL_OUT ? EV_WRITE : 0));
	if (watcher->events == events)
		return 0; /* already registered, nothing to do */

	/* Re-register watcher */
	say_debug("httpc %p: poll fd=%d %s", env, fd, evstr[events]);
	ev_io_stop(loop(), watcher);
	ev_io_set(watcher, fd, events);
	ev_io_start(loop(), watcher);

	return 0;
}

/**
 * libcurl callback for CURLOPT_WRITEFUNCTION
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
static size_t
curl_easy_write_cb(char *ptr, size_t size, size_t nmemb, void *ctx)
{
	struct httpc_response *resp = (struct httpc_response *) ctx;
	const size_t bytes = size * nmemb;

	char *p = region_alloc(&resp->body, bytes);
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
	struct httpc_response *resp = (struct httpc_response *) ctx;
	const size_t bytes = size * nitems;
	char *p = region_alloc(&resp->headers, bytes);
	if (p == NULL) {
		diag_set(OutOfMemory, bytes, "ibuf", "httpc header");
		return 0;
	}
	memcpy(p, buffer, bytes);
	return bytes;
}

int
httpc_env_create(struct httpc_env *env, long max_conns)
{
	memset(env, 0, sizeof(*env));
	mempool_create(&env->req_pool, &cord()->slabc,
			sizeof(struct httpc_request));
	mempool_create(&env->resp_pool, &cord()->slabc,
			sizeof(struct httpc_response));
	mempool_create(&env->sock_pool, &cord()->slabc,
			sizeof(struct ev_io));

	env->multi = curl_multi_init();
	if (env->multi == NULL) {
		diag_set(SystemError, "failed to init multi handler");
		goto error_exit;
	}

	ev_init(&env->timer_event, curl_timer_cb);
	env->timer_event.data = (void *) env;
	curl_multi_setopt(env->multi, CURLMOPT_TIMERFUNCTION,
			  curl_multi_timer_cb);
	curl_multi_setopt(env->multi, CURLMOPT_TIMERDATA, (void *) env);

	curl_multi_setopt(env->multi, CURLMOPT_SOCKETFUNCTION,
			  curl_multi_sock_cb);
	curl_multi_setopt(env->multi, CURLMOPT_SOCKETDATA, (void *) env);

	curl_multi_setopt(env->multi, CURLMOPT_MAXCONNECTS, max_conns);

	return 0;

error_exit:
	httpc_env_destroy(env);
	return -1;
}

void
httpc_env_destroy(struct httpc_env *ctx)
{
	assert(ctx);
	if (ctx->multi != NULL)
		curl_multi_cleanup(ctx->multi);

	mempool_destroy(&ctx->req_pool);
	mempool_destroy(&ctx->resp_pool);
	mempool_destroy(&ctx->sock_pool);
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
	req->easy = curl_easy_init();
	if (req->easy == NULL) {
		diag_set(OutOfMemory, 0, "curl", "easy");
		return NULL;
	}

	if (strcmp(method, "GET") == 0) {
		curl_easy_setopt(req->easy, CURLOPT_HTTPGET, 1L);
	} else if (strcmp(method, "HEAD") == 0) {
		curl_easy_setopt(req->easy, CURLOPT_NOBODY, 1L);
	} else if (strcmp(method, "POST") == 0 ||
		   strcmp(method, "PUT") == 0 ||
		   strcmp(method, "PATCH")) {
		/*
		 * Set CURLOPT_POSTFIELDS to "" and CURLOPT_POSTFIELDSSIZE 0
		 * to avoid the read callback in any cases even if user
		 * forgot to call httpc_set_body() for POST request.
		 * @see https://curl.haxx.se/libcurl/c/CURLOPT_POSTFIELDS.html
		 */
		curl_easy_setopt(req->easy, CURLOPT_POST, 1L);
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS, "");
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE, 0);
		curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, method);
		if (httpc_set_header(req, "Accept: */*") < 0)
			goto error;
	} else {
		curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, method);
	}

	curl_easy_setopt(req->easy, CURLOPT_URL, url);

	curl_easy_setopt(req->easy, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(req->easy, CURLOPT_SSL_VERIFYPEER, 1);
	curl_easy_setopt(req->easy, CURLOPT_WRITEFUNCTION,
			curl_easy_write_cb);
	curl_easy_setopt(req->easy, CURLOPT_HEADERFUNCTION,
			curl_easy_header_cb);
	curl_easy_setopt(req->easy, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(req->easy, CURLOPT_HTTP_VERSION,
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

	if (req->easy != NULL)
		curl_easy_cleanup(req->easy);

	ibuf_destroy(&req->body);
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

	curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS, req->body.buf);
	curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE, size);

	if (httpc_set_header(req, "Content-Length: %zu", size) != 0)
		return -1;

	return 0;
}

int
httpc_set_keepalive(struct httpc_request *req, long idle, long interval)
{
#if (LIBCURL_VERSION_MAJOR >= 7 && LIBCURL_VERSION_MINOR >= 25)
	if (idle > 0 && interval > 0) {
		curl_easy_setopt(req->easy, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(req->easy, CURLOPT_TCP_KEEPIDLE, idle);
		curl_easy_setopt(req->easy, CURLOPT_TCP_KEEPINTVL, interval);
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
	curl_easy_setopt(req->easy, CURLOPT_LOW_SPEED_TIME, low_speed_time);
}

void
httpc_set_low_speed_limit(struct httpc_request *req, long low_speed_limit)
{
	curl_easy_setopt(req->easy, CURLOPT_LOW_SPEED_LIMIT, low_speed_limit);
}

void
httpc_set_verbose(struct httpc_request *req, bool curl_verbose)
{
	curl_easy_setopt(req->easy, CURLOPT_VERBOSE, curl_verbose);
}

void
httpc_set_ca_path(struct httpc_request *req, const char *ca_path)
{
	curl_easy_setopt(req->easy, CURLOPT_CAPATH, ca_path);
}

void
httpc_set_ca_file(struct httpc_request *req, const char *ca_file)
{
	curl_easy_setopt(req->easy, CURLOPT_CAINFO, ca_file);
}

static struct httpc_response *
httpc_response_new(struct httpc_env *ctx)
{
	assert(ctx);
	struct httpc_response *resp = mempool_alloc(&ctx->resp_pool);
	if (!resp) {
		diag_set(OutOfMemory, sizeof(struct httpc_response),
			 "mempool_alloc", "curl");
		return NULL;
	}
	memset(resp, 0, sizeof(*resp));
	resp->ctx = ctx;
	resp->curl_code = CURLE_OK;
	region_create(&resp->headers, &cord()->slabc);
	region_create(&resp->body, &cord()->slabc);
	fiber_cond_create(&resp->cond);
	return resp;
}

void
httpc_response_delete(struct httpc_response *resp)
{
	region_destroy(&resp->headers);
	region_destroy(&resp->body);
	fiber_cond_destroy(&resp->cond);
	mempool_free(&resp->ctx->resp_pool, resp);
}

struct httpc_response *
httpc_execute(struct httpc_request *req, double timeout)
{
	struct httpc_env *env = req->env;

	struct httpc_response *resp = httpc_response_new(env);
	if (resp == NULL)
		return NULL;

	CURLMcode mcode;
	curl_easy_setopt(req->easy, CURLOPT_WRITEDATA, (void *) resp);
	curl_easy_setopt(req->easy, CURLOPT_HEADERDATA, (void *) resp);
	curl_easy_setopt(req->easy, CURLOPT_PRIVATE, (void *) resp);
	curl_easy_setopt(req->easy, CURLOPT_HTTPHEADER, req->headers);

	++env->stat.total_requests;

	mcode = curl_multi_add_handle(env->multi, req->easy);
	if (mcode != CURLM_OK)
		goto curl_merror;

	/* Don't wait on a cond if request has already failed */
	if (resp->curl_code == CURLE_OK) {
		++env->stat.active_requests;
		int rc = fiber_cond_wait_timeout(&resp->cond, timeout);
		if (rc < 0 || fiber_is_cancelled())
			resp->curl_code = CURLE_OPERATION_TIMEDOUT;
		--env->stat.active_requests;
	}

	mcode = curl_multi_remove_handle(env->multi, req->easy);
	if (mcode != CURLM_OK)
		goto curl_merror;

	long longval = 0;
	switch (resp->curl_code) {
	case CURLE_OK:
		curl_easy_getinfo(req->easy, CURLINFO_RESPONSE_CODE, &longval);
		resp->status = (int) longval;
		/* TODO: get real response string from resp->headers */
		resp->reason = "Ok";
		if (resp->status == 200) {
			++env->stat.http_200_responses;
		} else {
			++env->stat.http_other_responses;
		}
		break;
	case CURLE_SSL_CACERT:
	case CURLE_PEER_FAILED_VERIFICATION:
		/* 495 SSL Certificate Error (nginx non-standard) */
		resp->status = 495;
		resp->reason = curl_easy_strerror(resp->curl_code);
		++env->stat.failed_requests;
		break;
	case CURLE_OPERATION_TIMEDOUT:
		/* 408 Request Timeout (nginx non-standard) */
		resp->status = 408;
		resp->reason = curl_easy_strerror(resp->curl_code);
		++env->stat.failed_requests;
		break;
	case CURLE_GOT_NOTHING:
		/* 444 No Response */
		resp->status = 444;
		resp->reason = curl_easy_strerror(resp->curl_code);
		++env->stat.failed_requests;
		break;
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT:
		/* 595 Connection Problem (AnyEvent non-standard) */
		resp->status = 595;
		resp->reason = curl_easy_strerror(resp->curl_code);
		++env->stat.failed_requests;
		break;
	case CURLE_WRITE_ERROR:
		/* Diag is already set by curl_write_cb() */
		assert(!diag_is_empty(&fiber()->diag));
		httpc_response_delete(resp);
		++env->stat.failed_requests;
		return NULL;
	case CURLE_OUT_OF_MEMORY:
		diag_set(OutOfMemory, 0, "curl", "internal");
		httpc_response_delete(resp);
		++env->stat.failed_requests;
		return NULL;
	default:
		curl_easy_getinfo(req->easy, CURLINFO_OS_ERRNO, &longval);
		errno = longval ? longval : EINVAL;
		diag_set(SystemError, "curl: %s",
			 curl_easy_strerror(resp->curl_code));
		httpc_response_delete(resp);
		++env->stat.failed_requests;
		return NULL;
	}

	return resp;
curl_merror:
	httpc_response_delete(resp);
	switch (mcode) {
	case CURLM_OUT_OF_MEMORY:
		diag_set(OutOfMemory, 0, "curl", "internal");
		break;
	default:
		errno = EINVAL;
		diag_set(SystemError, "curl_multi_error: %s",
			 curl_multi_strerror(mcode));
	}
	return NULL;
}
