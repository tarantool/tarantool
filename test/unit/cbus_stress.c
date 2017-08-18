#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "memory.h"
#include "fiber.h"
#include "cbus.h"
#include "unit.h"

/*
 * Number of test threads.
 *
 * Each test thread will connect to, disconnect from, and  send
 * messages to random neighbors in a loop.
 */
static const int thread_count = 32;

/* Number of loop iterations performed by each test thread. */
static const int loop_count = 300;

/* Chance of connecting to a random neighbor in a loop iteration. */
static const int connect_prob = 30;

/* Chance of disconnecting from a random neighbor in a loop iteration. */
static const int disconnect_prob = 20;

/* This structure represents a connection to a test thread. */
struct conn {
	bool active;
	struct cpipe to;
	struct cpipe from;
};

/* Test thread. */
struct thread {
	/* Thread id (between 0 and thread_count - 1, inclusive) */
	int id;
	/* Name of endpoint hosted by this thread. */
	char name[32];
	/* Cord corresponding to this thread. */
	struct cord cord;
	/* Pipe from this to the main thread. */
	struct cpipe main_pipe;
	/* Pipe from the main to this thread. */
	struct cpipe thread_pipe;
	/* Test thread id => connection. */
	struct conn *connections;
	/*
	 * Array of connected thread ids.
	 * Used for picking a random thread to connect to.
	 */
	int *connected;
	int connected_count;
	/*
	 * Array of disconnected thread ids.
	 * Used for picking a random thread to disconnect from.
	 */
	int *disconnected;
	int disconnected_count;
	/*
	 * This message is sent:
	 * - from the main thread to this thread to signal test start
	 * - from this thread to the main thread when the test is complete
	 */
	struct cmsg cmsg;
	/*
	 * Number of messages sent/received by this thread.
	 * Sum 'send' must be equal to sum 'received' over all test threads.
	 */
	int sent, received;
};

/* Array of test threads. */
static struct thread *threads;
/*
 * Number of threads that are still performing the test.
 * When it reaches 0, the main thread is signalled to stop.
 */
static int active_thread_count;

static const char *
thread_name(int id)
{
	return threads[id].name;
}

static int
thread_func(va_list ap);

/* Spawn a test thread. */
static void
thread_create(struct thread *t, int id)
{
	assert(thread_count > 1);
	assert(id >= 0 && id < thread_count);

	const int neighbor_count = thread_count - 1;

	t->id = id;
	snprintf(t->name, sizeof(t->name), "thread_%d", id);
	assert(t->name != NULL);

	t->connections = calloc(thread_count, sizeof(*t->connections));
	assert(t->connections != NULL);

	t->connected_count = 0;
	t->connected = calloc(neighbor_count, sizeof(*t->connected));
	assert(t->connected != NULL);

	t->disconnected_count = 0;
	t->disconnected = calloc(neighbor_count, sizeof(*t->disconnected));
	assert(t->disconnected != NULL);

	/* Initially, we are not connected to anyone. */
	for (int i = 0; i < thread_count; i++) {
		if (i == id)
			continue; /* can't connect to self */
		t->disconnected[t->disconnected_count++] = i;
	}
	assert(t->disconnected_count == neighbor_count);

	t->sent = t->received = 0;
	active_thread_count++;

	if (cord_costart(&t->cord, t->name, thread_func, t) != 0)
		unreachable();

	cpipe_create(&t->thread_pipe, t->name);
}

static int
test_func(va_list ap);

static void
thread_start_test_cb(struct cmsg *cmsg)
{
	struct thread *t = container_of(cmsg, struct thread, cmsg);
	struct fiber *test_fiber = fiber_new("test", test_func);
	assert(test_fiber != NULL);
	fiber_start(test_fiber, t);
}

/* Signal a test thread to start the test. */
static void
thread_start_test(struct thread *t)
{
	static struct cmsg_hop start_route[] = {
		{ thread_start_test_cb, NULL }
	};
	cmsg_init(&t->cmsg, start_route);
	cpipe_push(&t->thread_pipe, &t->cmsg);
}

/* Join a test thread. */
static void
thread_destroy(struct thread *t)
{
	cbus_stop_loop(&t->thread_pipe);
	cpipe_destroy(&t->thread_pipe);

	if (cord_join(&t->cord) != 0)
		unreachable();

	free(t->connected);
	free(t->disconnected);
	free(t->connections);
}

/* Connect to the test thread with the given id. */
static void
thread_connect(struct thread *t, int dest_id)
{
	assert(dest_id != t->id);
	assert(dest_id < thread_count);
	struct conn *conn = &t->connections[dest_id];
	assert(!conn->active);
	cbus_pair(thread_name(dest_id), t->name,
		  &conn->to, &conn->from, NULL, NULL, NULL);
	conn->active = true;
}

/* Disconnect from the test thread with the given id. */
static void
thread_disconnect(struct thread *t, int dest_id)
{
	assert(dest_id != t->id);
	assert(dest_id < thread_count);
	struct conn *conn = &t->connections[dest_id];
	assert(conn->active);
	cbus_unpair(&conn->to, &conn->from, NULL, NULL, NULL);
	conn->active = false;
}

/* Connect to a random test thread. */
static void
thread_connect_random(struct thread *t)
{
	assert(t->disconnected_count > 0);
	assert(t->connected_count + t->disconnected_count == thread_count - 1);
	int idx = rand() % t->disconnected_count;
	int dest_id = t->disconnected[idx];
	t->disconnected[idx] = t->disconnected[--t->disconnected_count];
	t->connected[t->connected_count++] = dest_id;
	thread_connect(t, dest_id);
}

/* Disconnect from a random test thread. */
static void
thread_disconnect_random(struct thread *t)
{
	assert(t->connected_count > 0);
	assert(t->connected_count + t->disconnected_count == thread_count - 1);
	int idx = rand() % t->connected_count;
	int dest_id = t->connected[idx];
	t->connected[idx] = t->connected[--t->connected_count];
	t->disconnected[t->disconnected_count++] = dest_id;
	thread_disconnect(t, dest_id);
}

struct thread_msg {
	struct cmsg cmsg;
	int dest_id;
};

static void
thread_msg_received_cb(struct cmsg *cmsg)
{
	struct thread_msg *msg = container_of(cmsg, struct thread_msg, cmsg);
	struct thread *t = &threads[msg->dest_id];
	t->received++;
	free(msg);
}

/* Send a message to the test thread with the given id. */
static void
thread_send(struct thread *t, int dest_id)
{
	static struct cmsg_hop route[] = {
		{ thread_msg_received_cb, NULL }
	};
	struct conn *c = &t->connections[dest_id];
	assert(c->active);
	struct thread_msg *msg = malloc(sizeof(*msg));
	assert(msg != NULL);
	cmsg_init(&msg->cmsg, route);
	msg->dest_id = dest_id;
	cpipe_push(&c->to, &msg->cmsg);
	t->sent++;
}

/* Send a message to a random connected test thread. */
static void
thread_send_random(struct thread *t)
{
	assert(t->connected_count > 0);
	int idx = rand() % t->connected_count;
	int dest_id = t->connected[idx];
	thread_send(t, dest_id);
}

static void
test_iter(struct thread *t)
{
	if (t->disconnected_count > 0 &&
	    (t->connected_count == 0 || rand() % 100 < connect_prob))
		thread_connect_random(t);
	if (t->connected_count > 1 && rand() % 100 < disconnect_prob)
		thread_disconnect_random(t);
	thread_send_random(t);
}

static void
test_complete_cb(struct cmsg *cmsg)
{
	(void)cmsg;
	assert(active_thread_count > 0);
	if (--active_thread_count == 0) {
		/* Stop the main fiber when all workers are done. */
		fiber_cancel(fiber());
	}
}

static int
test_func(va_list ap)
{
	struct thread *t = va_arg(ap, struct thread *);
	/* Perform the test. */
	for (int i = 0; i < loop_count; i++) {
		test_iter(t);
		fiber_yield_timeout(0);
	}
	/* Disconnect from all neighbors. */
	for (int i = 0; i < thread_count; i++) {
		struct conn *c = &t->connections[i];
		if (c->active)
			thread_disconnect(t, i);
	}
	/* Notify the main thread that we are done. */
	static struct cmsg_hop complete_route[] = {
		{ test_complete_cb, NULL }
	};
	cmsg_init(&t->cmsg, complete_route);
	cpipe_push(&t->main_pipe, &t->cmsg);
	return 0;
}

static int
thread_func(va_list ap)
{
	struct thread *t = va_arg(ap, struct thread *);

	cpipe_create(&t->main_pipe, "main");

	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, t->name,
			     fiber_schedule_cb, fiber());

	cbus_loop(&endpoint);

	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&t->main_pipe);
	return 0;
}

static int
main_func(va_list ap)
{
	(void)ap;

	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, "main", fiber_schedule_cb, fiber());

	threads = calloc(thread_count, sizeof(*threads));
	assert(threads != NULL);

	for (int i = 0; i < thread_count; i++)
		thread_create(&threads[i], i);

	for (int i = 0; i < thread_count; i++)
		thread_start_test(&threads[i]);

	cbus_loop(&endpoint);

	int sent = 0, received = 0;
	for (int i = 0; i < thread_count; i++) {
		struct thread *t = &threads[i];
		sent += t->sent;
		received += t->received;
		thread_destroy(t);
	}
	assert(sent == received);

	cbus_endpoint_destroy(&endpoint, cbus_process);

	free(threads);
	threads = NULL;

	ev_break(loop(), EVBREAK_ALL);

	return 0;
}

int
main()
{
	srand(time(NULL));

	memory_init();
	fiber_init(fiber_c_invoke);
	cbus_init();

	header();

	struct fiber *main_fiber = fiber_new("main", main_func);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);

	footer();

	cbus_free();
	fiber_free();
	memory_free();
}
