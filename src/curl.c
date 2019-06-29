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

#include "curl.h"

#include <assert.h>
#include <curl/curl.h>

#include "fiber.h"
#include "errinj.h"

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
	 * From curl://curl.haxx.se/libcurl/c/curl_multi_socket_action.html:
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
		CURLcode code = msg->data.result;
		struct curl_request *request = NULL;
		curl_easy_getinfo(easy, CURLINFO_PRIVATE, (void *) &request);
		request->code = (int) code;
		request->in_progress = false;
#ifndef NDEBUG
		struct errinj *errinj = errinj(ERRINJ_HTTP_RESPONSE_ADD_WAIT,
					       ERRINJ_BOOL);
		if (errinj != NULL)
			errinj->bparam = false;
#endif
		fiber_cond_signal(&request->cond);
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
	struct curl_env *env = (struct curl_env *) watcher->data;

	say_debug("curl %p: event timer", env);
	curl_multi_process(env->multi, CURL_SOCKET_TIMEOUT, 0);
}

/**
 * libcurl callback for CURLMOPT_TIMERFUNCTION
 * @see curl://curl.haxx.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html
 */
static int
curl_multi_timer_cb(CURLM *multi, long timeout_ms, void *envp)
{
	(void) multi;
	struct curl_env *env = (struct curl_env *) envp;

	say_debug("curl %p: wait timeout=%ldms", env, timeout_ms);
	ev_timer_stop(loop(), &env->timer_event);
	if (timeout_ms >= 0) {
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
	struct curl_env *env = (struct curl_env *) watcher->data;
	int fd = watcher->fd;

	say_debug("curl %p: event fd=%d %s", env, fd, evstr[revents]);
	const int action = ((revents & EV_READ  ? CURL_POLL_IN  : 0) |
			    (revents & EV_WRITE ? CURL_POLL_OUT : 0));
	curl_multi_process(env->multi, fd, action);
}

/**
 * libcurl callback for CURLMOPT_SOCKETFUNCTION
 * @see curl://curl.haxx.se/libcurl/c/CURLMOPT_SOCKETFUNCTION.html
 */
static int
curl_multi_sock_cb(CURL *easy, curl_socket_t fd, int what, void *envp,
		   void *watcherp)
{
	(void) easy;
	struct curl_env *env = (struct curl_env *) envp;
	struct ev_io *watcher = (struct ev_io *) watcherp;

	if (what == CURL_POLL_REMOVE) {
		say_debug("curl %p: remove fd=%d", env, fd);
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
				 "curl sock");
			return -1;
		}
		ev_io_init(watcher, curl_sock_cb, fd, 0);
		watcher->data = env;
		++env->stat.sockets_added;
		curl_multi_assign(env->multi, fd, watcher);
		say_debug("curl %p: add fd=%d", env, fd);
	}

	if (what == CURL_POLL_NONE)
		return 0; /* register, not interested in readiness (yet) */

	const int events = ((what & CURL_POLL_IN  ? EV_READ  : 0) |
			    (what & CURL_POLL_OUT ? EV_WRITE : 0));
	if (watcher->events == events)
		return 0; /* already registered, nothing to do */

	/* Re-register watcher */
	say_debug("curl %p: poll fd=%d %s", env, fd, evstr[events]);
	ev_io_stop(loop(), watcher);
	ev_io_set(watcher, fd, events);
	ev_io_start(loop(), watcher);

	return 0;
}


int
curl_env_create(struct curl_env *env, long max_conns, long max_total_conns)
{
	memset(env, 0, sizeof(*env));
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
#if LIBCURL_VERSION_NUM >= 0x071e00
	curl_multi_setopt(env->multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, max_total_conns);
#else
	(void) max_total_conns;
#endif

	return 0;

error_exit:
	curl_env_destroy(env);
	return -1;
}

void
curl_env_destroy(struct curl_env *env)
{
	assert(env);
	if (env->multi != NULL)
		curl_multi_cleanup(env->multi);

	mempool_destroy(&env->sock_pool);
}

int
curl_request_create(struct curl_request *curl_request)
{
	curl_request->easy = curl_easy_init();
	if (curl_request->easy == NULL) {
		diag_set(OutOfMemory, 0, "curl", "easy");
		return -1;
	}
	curl_request->in_progress = false;
	curl_request->code = CURLE_OK;
	fiber_cond_create(&curl_request->cond);
	return 0;
}

void
curl_request_destroy(struct curl_request *curl_request)
{
	if (curl_request->easy != NULL)
		curl_easy_cleanup(curl_request->easy);
	fiber_cond_destroy(&curl_request->cond);
}

CURLMcode
curl_execute(struct curl_request *curl_request, struct curl_env *env,
	     double timeout)
{
	CURLMcode mcode;
	curl_request->in_progress = true;
	mcode = curl_multi_add_handle(env->multi, curl_request->easy);
	if (mcode != CURLM_OK)
		goto curl_merror;
	ERROR_INJECT_YIELD(ERRINJ_HTTP_RESPONSE_ADD_WAIT);
	/* Don't wait on a cond if request has already failed or finished. */
	if (curl_request->code == CURLE_OK && curl_request->in_progress) {
		++env->stat.active_requests;
		int rc = fiber_cond_wait_timeout(&curl_request->cond, timeout);
		if (rc < 0 || fiber_is_cancelled())
			curl_request->code = CURLE_OPERATION_TIMEDOUT;
		--env->stat.active_requests;
	}
	mcode = curl_multi_remove_handle(env->multi, curl_request->easy);
	if (mcode != CURLM_OK)
		goto curl_merror;

	return CURLM_OK;

curl_merror:
	switch (mcode) {
	case CURLM_OUT_OF_MEMORY:
		diag_set(OutOfMemory, 0, "curl", "internal");
		break;
	default:
		errno = EINVAL;
		diag_set(SystemError, "curl_multi_error: %s",
			 curl_multi_strerror(mcode));
	}
	return mcode;
}
