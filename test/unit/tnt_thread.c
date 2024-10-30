#include "memory.h"
#include "cbus.h"
#include "tnt_thread.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

struct tnt_tx_push_req {
	struct cmsg base;
	tnt_tx_func_f func;
	void *arg;
};

struct fiber_signal {
	bool is_set;
	struct fiber_cond cond;
};

static struct cord worker;
static struct cpipe pipe_to_worker;

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

static int
worker_f(va_list ap)
{
	(void)ap;
	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "worker", fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	return 0;
}

static void
worker_start(void)
{
	note("start worker");
	fail_if(cord_costart(&worker, "worker", worker_f, NULL) != 0);
	cpipe_create(&pipe_to_worker, "worker");
}

static void
worker_stop(void)
{
	note("finish worker");
	cbus_stop_loop(&pipe_to_worker);
	cpipe_destroy(&pipe_to_worker);
	fail_if(cord_join(&worker) != 0);
}

static void
cmsg_tnt_tx_push_f(struct cmsg *m)
{
	struct tnt_tx_push_req *r = (void *)m;
	tnt_tx_push(r->func, r->arg);
	free(r);
}

static void
cmsg_tnt_tx_flush_f(struct cmsg *m)
{
	tnt_tx_flush();
	free(m);
}

static void
tnt_fiber_signal_send_f(void *arg)
{
	fiber_signal_send(arg);
}

static void
push_via_worker(tnt_tx_func_f func, void *arg)
{
	static const struct cmsg_hop route = {cmsg_tnt_tx_push_f, NULL};

	struct tnt_tx_push_req *r = xmalloc(sizeof(*r));
	r->func = func;
	r->arg = arg;
	cmsg_init(&r->base, &route);
	cpipe_push(&pipe_to_worker, &r->base);
}

static void
flush_via_worker(void)
{
	static const struct cmsg_hop route = {cmsg_tnt_tx_flush_f, NULL};
	struct cmsg *m = xmalloc(sizeof(*m));
	cmsg_init(m, &route);
	cpipe_push(&pipe_to_worker, m);
}

static void
execute_via_worker(tnt_tx_func_f func, void *arg)
{
	push_via_worker(func, arg);
	flush_via_worker();
}

/** ------------------------------------------------------------------------- */

static void
test_basic(void)
{
	header();
	plan(1);

	struct fiber_signal signal;
	fiber_signal_create(&signal);
	push_via_worker(tnt_fiber_signal_send_f, &signal);
	fiber_sleep(0.1);
	ok(!signal.is_set, "not delivered yet");

	flush_via_worker();
	fiber_signal_recv(&signal);
	fiber_signal_destroy(&signal);

	check_plan();
	footer();
}

/** ------------------------------------------------------------------------- */

struct test_signal_pair {
	struct fiber_signal src_signal;
	struct fiber_signal dst_signal;
};

static void
test_signal_pair_create(struct test_signal_pair *pair)
{
	fiber_signal_create(&pair->src_signal);
	fiber_signal_create(&pair->dst_signal);
}

static void
test_signal_pair_destroy(struct test_signal_pair *pair)
{
	fiber_signal_destroy(&pair->src_signal);
	fiber_signal_destroy(&pair->dst_signal);
}

static void
test_signal_pair_execute_f(void *arg)
{
	struct test_signal_pair *pair = arg;
	fiber_signal_send(&pair->dst_signal);
	fiber_signal_recv(&pair->src_signal);
}

static void
test_fiber_pool_size(void)
{
	header();
	plan(2);
	int old_size = tnt_thread_get_tx_user_pool_size();
	tnt_thread_set_tx_user_pool_size(1);

	struct test_signal_pair pair;
	test_signal_pair_create(&pair);
	execute_via_worker(test_signal_pair_execute_f, &pair);
	/* The message execution is started. */
	fiber_signal_recv(&pair.dst_signal);

	struct fiber_signal signal;
	fiber_signal_create(&signal);
	execute_via_worker(tnt_fiber_signal_send_f, &signal);
	fiber_sleep(0.1);
	ok(!signal.is_set, "the second msg is waiting in queue");

	/* Unblock the first message. */
	fiber_signal_send(&pair.src_signal);

	/* Finalize the second message. */
	fiber_signal_recv(&signal);

	/* Increase the pool size, should work without blockages now. */
	const int new_pool_size = 10;
	tnt_thread_set_tx_user_pool_size(new_pool_size);
	struct test_signal_pair pairs[new_pool_size];
	for (int i = 0; i < new_pool_size; ++i) {
		test_signal_pair_create(&pairs[i]);
		execute_via_worker(test_signal_pair_execute_f, &pairs[i]);
	}
	execute_via_worker(tnt_fiber_signal_send_f, &signal);
	for (int i = 0; i < new_pool_size; ++i)
		fiber_signal_recv(&pairs[i].dst_signal);
	fiber_sleep(0.1);
	ok(!signal.is_set, "the last msg is waiting in queue");
	for (int i = 0; i < new_pool_size; ++i)
		fiber_signal_send(&pairs[i].src_signal);
	fiber_signal_recv(&signal);

	/* One last time to ensure all the messages are finalized. */
	execute_via_worker(tnt_fiber_signal_send_f, &signal);
	fiber_signal_recv(&signal);

	for (int i = 0; i < new_pool_size; ++i)
		test_signal_pair_destroy(&pairs[i]);
	test_signal_pair_destroy(&pair);
	fiber_signal_destroy(&signal);

	tnt_thread_set_tx_user_pool_size(old_size);
	check_plan();
	footer();
}

/** ------------------------------------------------------------------------- */

struct test_tracked_signal {
	int id;
	double timeout;
	struct fiber_signal signal;
	int *last_started_id;
};

static void
test_tracked_signal_create(struct test_tracked_signal *pair, int id,
			   int *last_started_id, double timeout)
{
	pair->id = id;
	pair->timeout = timeout;
	fiber_signal_create(&pair->signal);
	pair->last_started_id = last_started_id;
}

static void
test_tracked_signal_execute_f(void *arg)
{
	struct test_tracked_signal *pair = arg;
	fail_unless(*pair->last_started_id + 1 == pair->id);
	*pair->last_started_id = pair->id;
	if (pair->timeout >= 0)
		fiber_sleep(pair->timeout);
	fiber_signal_send(&pair->signal);
}

static void
test_tracked_signal_destroy(struct test_tracked_signal *pair)
{
	fiber_signal_destroy(&pair->signal);
}

static void
test_start_order(void)
{
	header();
	plan(1);
	int old_size = tnt_thread_get_tx_user_pool_size();
	const int new_pool_size = 10;
	tnt_thread_set_tx_user_pool_size(new_pool_size);

	const int message_count = 57;
	assert(new_pool_size < message_count);

	int last_started_id = 0;
	struct test_tracked_signal pairs[message_count];
	for (int i = 0; i < message_count; ++i) {
		/*
		 * Make the callbacks complete in not the same order as they
		 * were started in. To ensure that the order of start is still
		 * guaranteed, even if the fiber pool becomes full sometimes.
		 */
		double timeout;
		if (i % 3 == 0)
			timeout = 0;
		else if (i % 5 == 0)
			timeout = -1;
		else
			timeout = i % 10 * 0.001;
		test_tracked_signal_create(&pairs[i], i + 1, &last_started_id,
					   timeout);
		push_via_worker(test_tracked_signal_execute_f, &pairs[i]);
	}
	flush_via_worker();

	for (int i = 0; i < message_count; ++i) {
		fiber_signal_recv(&pairs[i].signal);
		test_tracked_signal_destroy(&pairs[i]);
	}
	is(last_started_id, message_count);

	tnt_thread_set_tx_user_pool_size(old_size);
	check_plan();
	footer();
}

/** ------------------------------------------------------------------------- */

static int
cbus_loop_f(va_list ap)
{
	(void)ap;
	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "main", fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	return 0;
}

static int
tnt_thread_test_suite_f(va_list ap)
{
	(void)ap;
	header();
	plan(3);

	struct fiber *endpoint_worker = fiber_new("main_endpoint", cbus_loop_f);
	fiber_set_joinable(endpoint_worker, true);
	fiber_start(endpoint_worker);

	tnt_thread_init();
	worker_start();

	test_basic();
	test_fiber_pool_size();
	test_start_order();

	worker_stop();
	tnt_thread_shutdown();
	tnt_thread_free();
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
	struct fiber *main_fiber = fiber_new("main", tnt_thread_test_suite_f);
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
