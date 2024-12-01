#include "memory.h"
#include "fiber.h"
#include "cbus.h"
#include "trigger.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/**
 * Test triggers on cpipe flush. Cpipe flush send all buffered
 * messages to a consumer. Flush is called either at the end of
 * an event loop, or when a messages queue is full. This event
 * can be used to make some prepare actions before flush.
 */

/** Counter of flush events. */
static int flushed_cnt = 0;

/**
 * Worker thread. In the test only one worker is started and the
 * main thread sends to it messages to trigger tests one by one.
 */
static struct cord worker;
/** Queue of messages from the main to the worker thread. */
static struct cpipe pipe_to_worker;
/** Queue of messages from the worker to the main thread. */
static struct cpipe pipe_to_main;
/**
 * Trigger which is called on flush to the main thread event. Here
 * we test only this flush direction (from worker to main), becase
 * the direction from the main to the worker works in the same
 * way.
 */
static struct trigger on_flush_to_main;

struct fiber_signal {
	bool is_set;
	struct fiber_cond cond;
};

static void
fiber_signal_create(struct fiber_signal *signal)
{
	signal->is_set = false;
	fiber_cond_create(&signal->cond);
}

static void
fiber_signal_destroy(struct fiber_signal *signal)
{
	fiber_cond_destroy(&signal->cond);
}

static void
fiber_signal_send(struct fiber_signal *signal)
{
	signal->is_set = true;
	fiber_cond_signal(&signal->cond);
}

static void
fiber_signal_recv(struct fiber_signal *signal)
{
	while (!signal->is_set)
		fiber_cond_wait(&signal->cond);
	signal->is_set = false;
}

struct test_msg {
	struct cmsg base;
	struct fiber_signal signal;
};

static void
test_msg_create(struct test_msg *msg, struct cmsg_hop *route)
{
	cmsg_init(&msg->base, route);
	fiber_signal_create(&msg->signal);
}

static void
test_msg_destroy(struct test_msg *msg)
{
	fiber_signal_destroy(&msg->signal);
}

/** Common callbacks. {{{ ------------------------------------- */

/** Dummy callback to fill cmsg rotes with more hops. */
static void
do_nothing(struct cmsg *m)
{
	(void) m;
}

static void
send_signal(struct cmsg *m)
{
	fiber_signal_send(&((struct test_msg *)m)->signal);
}

/** Callback called on each flush to the main thread. */
static int
flush_cb(struct trigger *t, void *e)
{
	(void) t;
	(void) e;
	++flushed_cnt;
	return 0;
}

/** }}} Common callbacks. ------------------------------------- */

/** Worker routines. {{{ -------------------------------------- */

static int
worker_f(va_list ap)
{
	(void) ap;
	cpipe_create(&pipe_to_main, "main");
	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "worker", fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&pipe_to_main);
	return 0;
}

static void
worker_start(void)
{
	fail_if(cord_costart(&worker, "worker", worker_f, NULL) != 0);
	cpipe_create(&pipe_to_worker, "worker");
}

static void
worker_stop(void)
{
	cbus_stop_loop(&pipe_to_worker);
	cpipe_destroy(&pipe_to_worker);
	fail_if(cord_join(&worker) != 0);
}

/** }}} Worker routines. -------------------------------------- */

/**
 * Test that flush trigger works for a single message.
 * {{{ -----------------------------------------------------------
 */

static void
test_single_msg(void)
{
	header();
	plan(1);

	struct cmsg_hop route[] = {
		{ do_nothing, &pipe_to_main },
		{ send_signal, NULL },
	};
	struct test_msg msg;
	test_msg_create(&msg, route);
	cpipe_push(&pipe_to_worker, &msg.base);
	fiber_signal_recv(&msg.signal);
	is(flushed_cnt, 1, "1 flush after");
	flushed_cnt = 0;
	test_msg_destroy(&msg);

	check_plan();
	footer();
}

/** }}} Test single message. ---------------------------------- */

/**
 * Test that flush is called once per event loop event if several
 * messages was pushed. {{{ --------------------------------------
 */

static void
test_auto_flush(void)
{
	header();
	plan(2);

	struct cmsg_hop route[] = {
		{ do_nothing, &pipe_to_main },
		{ send_signal, NULL },
	};
	const int msg_count = 3;
	struct test_msg msgs[msg_count];
	for (int i = 0; i < msg_count; ++i) {
		test_msg_create(&msgs[i], route);
		cpipe_push(&pipe_to_worker, &msgs[i].base);
		/* The manual submissions won't trigger immediate flush. */
		cpipe_submit_flush(&pipe_to_worker);
	}
	is(flushed_cnt, 0, "no flush until end of the loop's iteration");

	for (int i = 0; i < msg_count; ++i) {
		fiber_signal_recv(&msgs[i].signal);
		test_msg_destroy(&msgs[i]);
	}
	is(flushed_cnt, 1, "one flush for all messages");
	flushed_cnt = 0;

	check_plan();
	footer();
}

/** }}} Test several messages. -------------------------------- */

/**
 * Non-libev pipe {{{ --------------------------------------------
 */

static void
test_nonlibev_pipe(void)
{
	const int msg_count = 3;

	header();
	plan(msg_count + 3);

	struct cpipe pipe;
	cpipe_create_noev(&pipe, "worker");

	struct cmsg_hop route[] = {
		{ do_nothing, &pipe_to_main },
		{ send_signal, NULL },
	};
	struct test_msg msgs[msg_count];
	for (int i = 0; i < msg_count; ++i) {
		test_msg_create(&msgs[i], route);
		cpipe_push(&pipe, &msgs[i].base);
	}
	is(flushed_cnt, 0, "no flush until end of the loop's iteration");

	struct test_msg check_msg;
	test_msg_create(&check_msg, route);
	cpipe_push(&pipe_to_worker, &check_msg.base);
	fiber_signal_recv(&check_msg.signal);
	for (int i = 0; i < msg_count; ++i)
		ok(!msgs[i].signal.is_set, "no auto-flush for non-libev");
	is(flushed_cnt, 1, "one flush for the check message");
	flushed_cnt = 0;

	cpipe_flush(&pipe);
	for (int i = 0; i < msg_count; ++i) {
		fiber_signal_recv(&msgs[i].signal);
		test_msg_destroy(&msgs[i]);
	}
	is(flushed_cnt, 1, "one flush for non-libev messages");
	flushed_cnt = 0;

	cpipe_destroy(&pipe);

	check_plan();
	footer();
}

/** }}} Non-libev pipe ---------------------------------------- */

static int
cbus_loop_f(va_list ap)
{
	(void) ap;
	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "main", fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	return 0;
}

static int
cbus_test_suite_f(va_list ap)
{
	(void)ap;
	header();
	plan(3);

	struct fiber *endpoint_worker = fiber_new("main_endpoint", cbus_loop_f);
	fiber_set_joinable(endpoint_worker, true);
	fiber_start(endpoint_worker);

	worker_start();
	trigger_create(&on_flush_to_main, flush_cb, NULL, NULL);
	trigger_add(&pipe_to_main.on_flush, &on_flush_to_main);

	test_single_msg();
	test_auto_flush();
	test_nonlibev_pipe();

	worker_stop();
	fiber_cancel(endpoint_worker);
	fiber_join(endpoint_worker);
	ev_break(loop(), EVBREAK_ALL);

	check_plan();
	footer();
	return 0;
}

int
main(void)
{
	header();
	plan(1);

	memory_init();
	fiber_init(fiber_c_invoke);
	cbus_init();
	struct fiber *main_fiber = fiber_new("main", cbus_test_suite_f);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);

	cbus_free();
	fiber_free();
	memory_free();

	int rc = check_plan();
	footer();
	return rc;
}
