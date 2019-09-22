#include "memory.h"
#include "fiber.h"
#include "cbus.h"
#include "unit.h"
#include "trigger.h"

/**
 * Test triggers on cpipe flush. Cpipe flush send all buffered
 * messages to a consumer. Flush is called either at the end of
 * an event loop, or when a messages queue is full. This event
 * can be used to make some prepare actions before flush.
 */

/** Counter of flush events. */
static int flushed_cnt = 0;
/** Expected value of flushed_cnt at the end of the test. */
static int expected_flushed_cnt = 0;

/**
 * Worker thread. In the test only one worker is started and the
 * main thread sends to it messages to trigger tests one by one.
 */
struct cord worker;
/** Queue of messages from the main to the worker thread. */
struct cpipe pipe_to_worker;
/** Queue of messages from the worker to the main thread. */
struct cpipe pipe_to_main;
/**
 * Trigger which is called on flush to the main thread event. Here
 * we test only this flush direction (from worker to main), becase
 * the direction from the main to the worker works in the same
 * way.
 */
struct trigger on_flush_to_main;

/** Common callbacks. {{{ ------------------------------------- */

/** Dummy callback to fill cmsg rotes with more hops. */
static void
do_nothing(struct cmsg *m)
{
	(void) m;
}

/** Callback called on each flush to the main thread. */
static int
flush_cb(struct trigger *t, void *e)
{
	(void) t;
	(void) e;
	++flushed_cnt;
	printf("flush event, counter = %d\n", flushed_cnt);
	return 0;
}

/** Callback to finish the test. It breaks the main event loop. */
static void
finish_execution(struct cmsg *m)
{
	(void) m;
	fiber_cancel(fiber());
	printf("break main fiber and finish test\n");
	is(flushed_cnt, expected_flushed_cnt,
	   "flushed_cnt at the end of the test");
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
worker_start()
{
	printf("start worker\n");
	fail_if(cord_costart(&worker, "worker", worker_f, NULL) != 0);
	cpipe_create(&pipe_to_worker, "worker");
}

static void
worker_stop()
{
	printf("finish worker\n");
	cbus_stop_loop(&pipe_to_worker);
	cpipe_destroy(&pipe_to_worker);
	fail_if(cord_join(&worker) != 0);
}

/** }}} Worker routines. -------------------------------------- */

/**
 * Test that if messages are not too many, the flush callback
 * is called only once per event loop, even if multiple flush
 * events are created. {{{ ---------------------------------------
 */
static void
do_forced_flush(struct cmsg *m)
{
	(void) m;
	static struct cmsg_hop forced_flush_rote = { do_nothing, NULL };
	static struct cmsg_hop finish_route = { finish_execution, NULL };
	static struct cmsg forced_flush_msg;
	static struct cmsg finish_msg;
	cmsg_init(&forced_flush_msg, &forced_flush_rote);
	cmsg_init(&finish_msg, &finish_route);
	cpipe_push(&pipe_to_main, &forced_flush_msg);
	cpipe_flush_input(&pipe_to_main);
	cpipe_push(&pipe_to_main, &finish_msg);
	expected_flushed_cnt = 1;
}

static void
test_forced_flush(struct cmsg *m)
{
	(void) m;
	is(flushed_cnt, 1, "1 flush after test_several_messages");
	printf("\n*** Test forced flush ***\n");
	flushed_cnt = 0;
	static struct cmsg_hop test_forced_flush_route =
		{ do_forced_flush, NULL };
	static struct cmsg test_forced_flush_msg;
	cmsg_init(&test_forced_flush_msg, &test_forced_flush_route);
	cpipe_push(&pipe_to_worker, &test_forced_flush_msg);
}

/** }}} Test forced flush. ------------------------------------ */

/**
 * Test that flush is called once per event loop event if several
 * messages was pushed. {{{ --------------------------------------
 */

/** Do some event and check flush to was not called. */
static void
do_some_event(struct cmsg *m)
{
	(void) m;
	is(flushed_cnt, 0, "no flush during loop");
}

/**
 * Create the following scenario for the worker:
 * do_some_event() -> do_some_event() -> do_nothing() -> flush().
 * Each do_some_event cheks, that flush was not called.
 */
static void
test_several_messages(struct cmsg *m)
{
	(void) m;
	is(flushed_cnt, 1, "1 flush after test_single_msg");
	printf("\n*** Test several messages ***\n");
	flushed_cnt = 0;
	static struct cmsg_hop test_event_route[] = {
		{ do_some_event, &pipe_to_main },
		{ do_nothing, NULL },
	};
	static struct cmsg_hop test_several_msg_route[] = {
		{ do_some_event, &pipe_to_main },
		{ test_forced_flush, NULL },
	};
	static struct cmsg test_event_msg[2];
	static struct cmsg test_several_msg;
	cmsg_init(&test_event_msg[0], test_event_route);
	cmsg_init(&test_event_msg[1], test_event_route);
	cmsg_init(&test_several_msg, test_several_msg_route);
	cpipe_push(&pipe_to_worker, &test_event_msg[0]);
	cpipe_push(&pipe_to_worker, &test_event_msg[1]);
	cpipe_push(&pipe_to_worker, &test_several_msg);
}

/** }}} Test several messages. -------------------------------- */

/**
 * Test that flush trigger works for a single message.
 * {{{ -----------------------------------------------------------
 */

static void
test_single_msg()
{
	printf("\n*** Test single message ***\n");
	static struct cmsg_hop test_single_flush_route[] = {
		{ do_nothing, &pipe_to_main },
		/* Schedule the next test. */
		{ test_several_messages, NULL },
	};
	static struct cmsg test_msg;
	cmsg_init(&test_msg, test_single_flush_route);
	cpipe_push(&pipe_to_worker, &test_msg);
}

/** }}} Test single message. ---------------------------------- */

static int
main_f(va_list ap)
{
	(void) ap;
	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "main", fiber_schedule_cb, fiber());
	worker_start();
	trigger_create(&on_flush_to_main, flush_cb, NULL, NULL);
	trigger_add(&pipe_to_main.on_flush, &on_flush_to_main);

	test_single_msg();

	cbus_loop(&endpoint);
	worker_stop();
	cbus_endpoint_destroy(&endpoint, cbus_process);
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int
main()
{
	header();
	plan(6);

	memory_init();
	fiber_init(fiber_c_invoke);
	cbus_init();
	printf("start main fiber\n");
	struct fiber *main_fiber = fiber_new("main", main_f);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	printf("start main loop\n");
	ev_run(loop(), 0);
	printf("finish main loop\n");
	cbus_free();
	fiber_free();
	memory_free();

	int rc = check_plan();
	footer();
	return rc;
}
