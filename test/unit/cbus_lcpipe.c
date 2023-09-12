#define UNIT_TAP_COMPATIBLE 1
#include "memory.h"
#include "fiber.h"
#include "cbus.h"
#include "unit.h"
#include "trigger.h"

/**
 * Test lcpipe message passing. lcpipe works with messages in two modes:
 * 1) lcpipe_push_now - send message immediately
 * 2) lcpipe_push - puts a message in a lcpipe without forwarding it.
 * Forwarding must call explicitly by calling lcpipe_flush_input function.
 */

/** Counter of flush events. */
static int flushed_cnt = 0;

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/** Common callbacks. {{{ ------------------------------------- */

static void
inc_counter_cb(struct cmsg *m)
{
	(void)m;
	++flushed_cnt;
	note("flush event, counter: %d\n", flushed_cnt);
}

static void
inc_counter_and_signal_cb(struct cmsg *m)
{
	inc_counter_cb(m);
	tt_pthread_mutex_lock(&mutex);
	tt_pthread_cond_signal(&cond);
	tt_pthread_mutex_unlock(&mutex);
}

static void
inc_counter_and_signal_then_cancel_cb(struct cmsg *m)
{
	inc_counter_and_signal_cb(m);
	fiber_cancel(fiber());
}

/** }}} Common callbacks. ------------------------------------- */

/**
 * Test push a single message.
 * {{{ -----------------------------------------------------------
 */

static void
test_single_msg(struct lcpipe *pipe)
{
	note("\n*** Test single message ***\n");
	static struct cmsg_hop test_event_route[] = {
		{ inc_counter_and_signal_cb, NULL },
	};
	static struct cmsg test_msg;
	cmsg_init(&test_msg, test_event_route);

	tt_pthread_mutex_lock(&mutex);

	lcpipe_push_now(pipe, &test_msg);

	tt_pthread_cond_wait(&cond, &mutex);
	tt_pthread_mutex_unlock(&mutex);

	is(flushed_cnt, 1, "1 flush after %s", __func__);
}

/** }}} Test single message. ---------------------------------- */

/**
 * Test insert a batch of messages and flush it.
 * {{{ -----------------------------------------------------------
 */

static void
test_batch_msg(struct lcpipe *pipe)
{
	note("\n*** Test batch of messages ***\n");
	static struct cmsg_hop test_event_routes[][1] = {
		{{inc_counter_cb,                        NULL}},
		{{inc_counter_cb,                        NULL}},
		{{inc_counter_cb,                        NULL}},
		{{inc_counter_cb,                        NULL}},
		{{inc_counter_and_signal_cb, NULL}},
	};
	static struct cmsg test_msg[5];
	for (unsigned i = 0; i < 5; i++) {
		cmsg_init(&test_msg[i], test_event_routes[i]);
		lcpipe_push(pipe, &test_msg[i]);
	}

	tt_pthread_mutex_lock(&mutex);

	lcpipe_flush_input(pipe);

	tt_pthread_cond_wait(&cond, &mutex);
	tt_pthread_mutex_unlock(&mutex);

	is(flushed_cnt, 6, "6 flush after %s", __func__);
}

/** }}} Test a batch of messages. ---------------------------------- */

/**
 * Test sequence of lcpipe_push and lcpipe_push_now functions. lcpipe_push_now
 * must release messages previously inserted by lcpipe_push function.
 * {{{ -----------------------------------------------------------
 */

static void
test_push_then_push_now(struct lcpipe *pipe)
{
	note("\n*** Test sequence of lcpipe_push and lcpipe_push_now ***\n");
	static struct cmsg_hop test_event_route_1[] = {
		{ inc_counter_cb, NULL },
	};
	static struct cmsg test_msg_1;
	cmsg_init(&test_msg_1, test_event_route_1);

	static struct cmsg_hop test_event_route_2[] = {
		{ inc_counter_and_signal_then_cancel_cb, NULL },
	};
	static struct cmsg test_msg_2;
	cmsg_init(&test_msg_2, test_event_route_2);

	tt_pthread_mutex_lock(&mutex);

	lcpipe_push(pipe, &test_msg_1);
	lcpipe_push_now(pipe, &test_msg_2);

	tt_pthread_cond_wait(&cond, &mutex);
	tt_pthread_mutex_unlock(&mutex);

	is(flushed_cnt, 8, "8 flush after %s", __func__);
}

/** }}} Test sequence of lcpipe_push and lcpipe_push_now functions. ------ */

/** Worker routines. {{{ -------------------------------------- */

static void *
worker_f(void *data)
{
	char *name = (char *)data;

	plan(3);
	header();

	note("start new worker, thread %s\n", name);

	struct lcpipe *pipe = lcpipe_new("main");
	test_single_msg(pipe);
	test_batch_msg(pipe);
	test_push_then_push_now(pipe);
	lcpipe_delete(pipe);

	int rc = check_plan();
	pthread_exit((void *)(intptr_t)rc);
}

static void
worker_start(pthread_t *handle)
{
	pthread_create(handle, NULL, worker_f, "X");
}

static int
worker_stop(pthread_t handle)
{
	note("finish worker\n");
	void *rc = NULL;
	pthread_join(handle, &rc);
	return (int)(intptr_t)rc;
}

/** }}} Worker routines. -------------------------------------- */

static int
main_f(va_list ap)
{
	(void)ap;
	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "main", fiber_schedule_cb, fiber());
	pthread_t th;
	worker_start(&th);
	cbus_loop(&endpoint);
	int rc = worker_stop(th);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	ev_break(loop(), EVBREAK_ALL);
	return rc;
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	cbus_init();
	struct fiber *main_fiber = fiber_new("main", main_f);
	assert(main_fiber != NULL);
	fiber_set_joinable(main_fiber, true);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);
	note("finish %s loop\n", __func__);
	int rc = fiber_join(main_fiber);
	cbus_free();
	fiber_free();
	memory_free();

	footer();
	return rc;
}
