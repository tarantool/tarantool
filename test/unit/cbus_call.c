#include "exception.h"
#include "memory.h"
#include "cbus.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static struct fiber *caller_fiber;
static struct cpipe pipe_to_callee;
static struct cpipe pipe_to_caller;

static int
func(struct cbus_call_msg *msg)
{
	usleep(100000);
	return 0;
}

/** Check ordinary cbus_call, nothing special. */
static void
test_cbus_call(void)
{
	struct cbus_call_msg msg;
	int rc = cbus_call(&pipe_to_callee, &pipe_to_caller, &msg, func);
	is(rc, 0, "cbus_call ordinary");
}

static int
empty(struct cbus_call_msg *msg)
{
	return 0;
}

/** Block until previously called func have been completed. */
static void
barrier(void)
{
	struct cbus_call_msg msg;
	int rc = cbus_call(&pipe_to_callee, &pipe_to_caller, &msg, empty);
	fail_if(rc != 0);
}

struct test_msg {
	struct cbus_call_msg base;
	bool was_freed;
};

static int
free_cb(struct cbus_call_msg *msg)
{
	struct test_msg *m = (struct test_msg *)msg;
	m->was_freed = true;
	return 0;
}

/** Set cbus_call timeout to 10 ms, while func runs for 100 ms. */
static void
test_cbus_call_timeout(void)
{
	plan(3);
	struct test_msg msg;
	msg.was_freed = false;
	int rc = cbus_call_timeout(&pipe_to_callee, &pipe_to_caller, &msg.base,
				   func, free_cb, 0.01);
	struct error *err = diag_last_error(diag_get());
	bool pass = (rc == -1) && err && (err->type == &type_TimedOut);
	ok(pass, "cbus_call timeout");
	ok(!msg.was_freed, "free_cb doesn't fire on timeout");
	barrier();
	ok(msg.was_freed, "free_cb executed on message return");
	check_plan();
}

static void
test_cbus_call_async(void)
{
	plan(3);
	struct test_msg msg;
	msg.was_freed = false;
	int csw = fiber()->csw;
	cbus_call_async(&pipe_to_callee, &pipe_to_caller, &msg.base, func,
			free_cb);
	is(fiber()->csw, csw, "no context switch");
	ok(!msg.was_freed, "free_cb doesn't fire on async call");
	barrier();
	ok(msg.was_freed, "free_cb executed on message return");
	check_plan();
}

static int
waker_fn(va_list ap)
{
	fiber_sleep(0.05);
	fiber_wakeup(caller_fiber);
	return 0;
}

/** Check that cbus_call is not interrupted by fiber_wakeup. */
static void
test_cbus_call_wakeup(void)
{
	struct fiber *waker_fiber = fiber_new("waker", waker_fn);
	fail_if(waker_fiber == NULL);
	fiber_wakeup(waker_fiber);

	struct cbus_call_msg msg;
	int rc = cbus_call(&pipe_to_callee, &pipe_to_caller, &msg, func);
	is(rc, 0, "cbus_call wakeup");
	barrier();
}

static int
canceler_fn(va_list ap)
{
	fiber_sleep(0.05);
	fiber_cancel(caller_fiber);
	return 0;
}

/** Check that cbus_call is not interrupted by fiber_cancel. */
static void
test_cbus_call_cancel(void)
{
	struct fiber *canceler_fiber = fiber_new("canceler", canceler_fn);
	fail_if(canceler_fiber == NULL);
	fiber_wakeup(canceler_fiber);

	struct cbus_call_msg msg;
	int rc = cbus_call(&pipe_to_callee, &pipe_to_caller, &msg, func);
	is(rc, 0, "cbus_call cancel");
	barrier();
}

static void
caller_cb(struct ev_loop *loop, ev_watcher *watcher, int events)
{
	struct cbus_endpoint *endpoint = (struct cbus_endpoint *)watcher->data;
	cbus_process(endpoint);
}

static int
callee_fn(va_list ap)
{
	struct cbus_endpoint endpoint;
	cpipe_create(&pipe_to_caller, "caller");
	cbus_endpoint_create(&endpoint, "callee", fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&pipe_to_caller);
	return 0;
}

static void
callee_start(struct cord *c)
{
	fail_if(cord_costart(c, "callee", callee_fn, NULL) != 0);
	cpipe_create(&pipe_to_callee, "callee");
}

static void
callee_stop(struct cord *c)
{
	cbus_stop_loop(&pipe_to_callee);
	cpipe_destroy(&pipe_to_callee);
	fail_if(cord_join(c) != 0);
}

static int
caller_fn(va_list ap)
{
	test_cbus_call();
	test_cbus_call_timeout();
	test_cbus_call_async();
	test_cbus_call_wakeup();
	test_cbus_call_cancel();

	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int
main(void)
{
	struct cord callee_cord;
	struct cbus_endpoint endpoint;

	header();
	plan(5);

	memory_init();
	fiber_init(fiber_c_invoke);
	cbus_init();
	cbus_endpoint_create(&endpoint, "caller", caller_cb, &endpoint);
	callee_start(&callee_cord);

	caller_fiber = fiber_new("caller", caller_fn);
	fail_if(caller_fiber == NULL);
	fiber_wakeup(caller_fiber);

	ev_run(loop(), 0);

	callee_stop(&callee_cord);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	cbus_free();
	fiber_free();
	memory_free();

	int rc = check_plan();
	footer();
	return rc;
}
