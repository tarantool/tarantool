/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "app_threads.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "diag.h"
#include "fiber.h"
#include "fiber_pool.h"
#include "msgpuck.h"
#include "port.h"
#include "say.h"
#include "tarantool_ev.h"
#include "trivia/util.h"
#include "xrow.h"

#include "lua/app_threads.h"

int app_thread_count;

/** Array of application threads. */
static struct cord *app_thread_cords;

static void *
app_thread_f(void *unused)
{
	(void)unused;
	app_thread_lua_init();
	struct fiber_pool fiber_pool;
	fiber_pool_create(&fiber_pool, cord_name(cord()), INT_MAX,
			  FIBER_POOL_IDLE_TIMEOUT);
	ev_run(loop(), 0);
	fiber_pool_destroy(&fiber_pool);
	app_thread_lua_free();
	return NULL;
}

void
app_threads_start(int thread_count)
{
	assert(app_thread_count == 0);
	assert(app_thread_cords == NULL);
	assert(thread_count >= 0 && thread_count <= APP_THREADS_MAX);
	if (thread_count == 0)
		return;
	app_thread_count = thread_count;
	app_thread_cords = xcalloc(thread_count, sizeof(*app_thread_cords));
	for (int i = 0; i < app_thread_count; ++i) {
		struct cord *cord = &app_thread_cords[i];
		char name[16];
		/* Sic: ids start with 1 because id 0 is reserved for tx. */
		snprintf(name, sizeof(name), "app%d", i + 1);
		if (cord_start(cord, name, app_thread_f, NULL) != 0)
			panic_syserror("failed to start application thread");
	}
}

void
app_threads_stop(void)
{
	for (int i = 0; i < app_thread_count; i++) {
		struct cord *cord = &app_thread_cords[i];
		cord_cancel(cord);
		if (cord_join(cord) != 0)
			panic_syserror("failed to join application thread");
	}
	free(app_thread_cords);
	app_thread_cords = NULL;
	app_thread_count = 0;
}

int
app_thread_process_call(struct call_request *request, struct port *port)
{
	const char *name = request->name;
	uint32_t name_len = mp_decode_strl(&name);
	struct port args;
	port_msgpack_create(&args, request->args,
			    request->args_end - request->args);
	int rc = app_thread_lua_call(name, name_len, &args, port);
	port_msgpack_destroy(&args);
	return rc;
}

int
app_thread_process_eval(struct call_request *request, struct port *port)
{
	const char *expr = request->expr;
	uint32_t expr_len = mp_decode_strl(&expr);
	struct port args;
	port_msgpack_create(&args, request->args,
			    request->args_end - request->args);
	int rc = app_thread_lua_eval(expr, expr_len, &args, port);
	port_msgpack_destroy(&args);
	return rc;
}
