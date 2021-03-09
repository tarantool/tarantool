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
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luaconf.h>

#include <pthread.h>
#include <module.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <float.h>
#include "../unit/unit.h"

#define XTM_MODULE_SIZE 16
#define STOP_MAGIC 0xAABBCCDD
#define MAGIC_MSG_COUNTER 100

struct module {
	/*
	 * Module thread id
	 */
	pthread_t thread;
	/*
	 * Tx thread id, need only for test purpose
	 */
	pthread_t tx_thread;
	/*
	 * Message queue from tx thread to module thread,
	 * in other words, tx thread puts messages in this queue,
	 * and module thread reads and executes them
	 */
	struct xtm_queue *in;
	/*
	 * Message queue from module thread to tx thread,
	 * in other words, module thread puts messages in this queue,
	 * and tx thread reads and executes them
	 */
	struct xtm_queue *out;
	/*
	 * Flag of the module state, May be -1, 0, 1
	 * -1 means that module thread failed to start
	 * 0 means that module currently stop
	 * 1 menas that module currently running
	 */
	int is_running;
	/*
	 * Fiber in tx thread, which read and execute module msg
	 */
	struct fiber *tx_fiber;
	/*
	 * Pipe to stop module thread
	 */
	int stop_fds[2];
};

/*
 * Simple module msg
 */
struct sample_module_msg {
	/*
	 * Thread id of sender thread
	 */
	pthread_t self;
	/*
	 * Msg counter
	 */
	unsigned long long counter;
	/*
	 * Stop msg flag
	 */
	bool stop;
};

static struct module module;

static inline int
xtm_fun_invoke_all(struct xtm_queue *queue)
{
	int rc = xtm_fun_invoke(queue, 1);
	while (rc >= 0 && xtm_msg_count(queue) > 0)
		rc = xtm_fun_invoke(queue, 0);
	return (rc >= 0 ? 0 : -1);
}

/*
 * Function pass from tx thread to xtm_fun_dispatch
 * Called in module thread
 */
static void
tx_msg_func(void *arg)
{
	struct sample_module_msg *msg = (struct sample_module_msg *)arg;
	/*
	 * Msg from tx thread and function called in module thread context
	 * Also here you can print msg and make sure of this
	 */
	fail_unless(msg->self == module.tx_thread &&
		    pthread_self() == module.thread);
	if(msg->counter == MAGIC_MSG_COUNTER)
		fprintf(stderr, "tx_msg_func called\n");
	free(msg);
}

/*
 * Function pass from module thread to xtm_fun_dispatch
 * Called in tx thread
 */
static void
module_msg_func(void *arg)
{
	struct sample_module_msg *msg = (struct sample_module_msg *)arg;
	/*
	 * Msg from module thread and function called in tx thread context
	 * Also here you can print msg and make sure of this
	 */
	fail_unless(msg->self == module.thread &&
		    pthread_self() == module.tx_thread);
	msg->self = pthread_self();
	if (msg->counter == MAGIC_MSG_COUNTER)
		fprintf(stderr, "module_msg_func called\n");
	if (!msg->stop && xtm_msg_probe(module.in) == 0) {
		fail_unless(xtm_fun_dispatch(module.in, tx_msg_func,
					     msg, 0) == 0);
	} else {
		free(msg);
	}
}

/*
 * Function to stop module thread
 */
static void
module_thread_stop(void)
{
	uint64_t tmp = STOP_MAGIC;
	ssize_t write_bytes = write(module.stop_fds[1], &tmp, sizeof(tmp));
	fail_unless(write_bytes == sizeof(tmp));
	__atomic_store_n(&module.is_running, 0, __ATOMIC_SEQ_CST);
	pthread_join(module.thread, NULL);
	fiber_join(module.tx_fiber);
	fail_unless(module.in != NULL);
	fail_unless(xtm_delete(module.in) == 0);
	module.in = NULL;
	fail_unless(module.out != NULL);
	fail_unless(xtm_delete(module.out) == 0);
	module.out = NULL;
}

/*
 * Timer function, called in module thread
 * Allocate msg and send it to tx thread
 */
static void
enqueue_message(bool stop)
{
	static unsigned long long counter;
	if (module.out == NULL)
		return;

	struct sample_module_msg *msg = (struct sample_module_msg *)
		malloc(sizeof(struct sample_module_msg));
	if (msg == NULL)
		return;
	msg->self = pthread_self();
	msg->counter = counter++;
	msg->stop = stop;
	fail_unless(msg->self == module.thread);
	if (xtm_msg_probe(module.out) == 0) {
		fail_unless(xtm_fun_dispatch(module.out, module_msg_func,
					     msg, 0) == 0);
	} else {
		free(msg);
	}
}

/*
 * Tx fiber function, received msg from module
 * Wait pipe of queue from module to tx
 * Read and execute msg from module to tx thread
 */
static int
tx_fiber_func(va_list arg)
{
	fail_unless((module.out = xtm_create(XTM_MODULE_SIZE)) != NULL);
	int pipe_fd = xtm_fd(module.out);
	while(__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1) {
		fail_unless (coio_wait(pipe_fd, COIO_READ,
				       DBL_MAX) & COIO_READ);
		fail_unless(xtm_fun_invoke_all(module.out) == 0);
	}
	/*
	 * Flush queue
	 */
	fail_unless(xtm_fun_invoke_all(module.out) == 0);
	return 0;
}

/*
 * Main module thread function.
 */
static void *
main_module_func(void *arg)
{
	fail_unless(pipe(module.stop_fds) ==0);
	fail_unless(fcntl(module.stop_fds[0], F_SETFL, O_NONBLOCK) == 0 &&
		    fcntl(module.stop_fds[1], F_SETFL, O_NONBLOCK) == 0);
	fail_unless((module.in = xtm_create(XTM_MODULE_SIZE)) != NULL);
	int pipe_fd = xtm_fd(module.in);
	__atomic_store_n(&module.is_running, 1, __ATOMIC_SEQ_CST);

	while (1) {
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(module.stop_fds[0], &readset);
		FD_SET(pipe_fd, &readset);
		int max = (pipe_fd > module.stop_fds[0] ?
			   pipe_fd : module.stop_fds[0]);

		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 5000;
		int rc = select(max + 1, &readset, NULL, NULL, &timeout);
		if (rc < 0 && errno == EINTR)
			continue;
		fail_unless(rc >= 0);
		/*
		 * Timeout to send msg to tx thread fiber
		 */
		if (rc == 0) {
			enqueue_message(false);
			continue;
		}
		if (FD_ISSET(module.stop_fds[0], &readset)) {
			uint64_t tmp;
			ssize_t read_bytes = read(module.stop_fds[0],
						  &tmp, sizeof(tmp));
			fail_unless(read_bytes == sizeof(tmp) &&
				    tmp == STOP_MAGIC);
			/*
			 * Push msg for wake up tx fiber
			 */
			enqueue_message(true);
			break;
		}
		if (FD_ISSET(pipe_fd, &readset))
			fail_unless(xtm_fun_invoke_all(module.in) == 0);
	}
	/*
	 * Flush queue
	 */
	fail_unless(xtm_fun_invoke_all(module.in) == 0);
	return (void *)NULL;
}

static int
stop(lua_State *L)
{
	if (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1)
		module_thread_stop();
	return 0;
}

static int
cfg(lua_State *L)
{
	/*
	 * Save tx thread id, for test purpose
	 */
	module.tx_thread = pthread_self();
	/*
	 * In case module already running, stop it
	 */
	if (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1)
		module_thread_stop();
	/*
	 * Create main module thread
	 */
	fail_unless(pthread_create(&module.thread, NULL,
				   main_module_func, NULL) == 0);
	/*
	 * Wait until module thread main function start event loop or failed
	 */
	while (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 0)
		;
	/*
	 * Create fiber in tx thread, which processed module messages
	 */
	module.tx_fiber = fiber_new("tx_fiber", tx_fiber_func);
	fail_unless(module.tx_fiber != NULL);
	fiber_set_joinable(module.tx_fiber, true);
	fiber_start(module.tx_fiber);
	return 0;
}

static const struct luaL_Reg xtm_lib[] = {
	{"cfg", cfg},
	{"stop", stop},
	{NULL, NULL}
};

LUALIB_API int
luaopen_xtmlib(lua_State *L)
{
	luaL_openlib(L, "xtmlib", xtm_lib, 0);
	return 0;
}
