#include "cbus.h"
#include "fiber.h"
#include "memory.h"
#include "unit.h"


struct cord hang_worker;
struct cord canceled_worker;

struct cbus_endpoint hang_endpoint;
struct cpipe pipe_from_cl_to_hang;
struct cpipe pipe_from_main_to_hang;

/*
 * We want to cancel canceled thread in the moment of cpipe_flush_cb
 * will be processing.
 * A Linux specific dirty hack will be used for reproduce the bug.
 * We need to synchronize the main thread and the canceled worker thread.
 * So, do it using the endpoint's mutex internal field(__data.__lock).
 * __lock == 0 - unlock
 * __lock == 1 - lock
 * __lock == 2 - possible waiters exists
 * After pthred create - __lock change state from 1 to 2
*/

pthread_mutex_t endpoint_hack_mutex_1;
pthread_cond_t endpoint_hack_cond_1;

pthread_mutex_t endpoint_hack_mutex_2;
pthread_cond_t endpoint_hack_cond_2;


static
void join_fail(int signum) {
	(void)signum;
	printf("Can't join the hang worker\n");
	exit(EXIT_FAILURE);
}

static void
do_nothing(struct cmsg *m)
{
	(void) m;
}

static int
hang_worker_f(va_list ap)
{
	(void) ap;
	cbus_endpoint_create(&hang_endpoint, "hang_worker",
	                     fiber_schedule_cb, fiber());

	tt_pthread_mutex_lock(&endpoint_hack_mutex_1);
	tt_pthread_cond_signal(&endpoint_hack_cond_1);
	tt_pthread_mutex_unlock(&endpoint_hack_mutex_1);

	cbus_loop(&hang_endpoint);
	cbus_endpoint_destroy(&hang_endpoint, cbus_process);
	return 0;
}

static void
hang_worker_start()
{
	cord_costart(&hang_worker, "hang_worker", hang_worker_f, NULL);
}

static int
canceled_worker_f(va_list ap)
{
	(void) ap;

	tt_pthread_mutex_lock(&endpoint_hack_mutex_1);
	tt_pthread_cond_signal(&endpoint_hack_cond_1);

	/* Wait a start command from the main thread */
	tt_pthread_mutex_lock(&endpoint_hack_mutex_2);
	tt_pthread_mutex_unlock(&endpoint_hack_mutex_1);

	tt_pthread_cond_wait(&endpoint_hack_cond_2, &endpoint_hack_mutex_2);
	tt_pthread_mutex_unlock(&endpoint_hack_mutex_2);

	cpipe_create(&pipe_from_cl_to_hang, "hang_worker");
	cpipe_set_max_input(&pipe_from_cl_to_hang, 1);
	static struct cmsg_hop nothing_route = { do_nothing, NULL };
	static struct cmsg nothing_msg;
	cmsg_init(&nothing_msg, &nothing_route);
	/*
	 * We need to use the cpipe_push_input cause
	 * an ev_invoke must be called for a hang reproducing
	 */
	cpipe_push_input(&pipe_from_cl_to_hang, &nothing_msg);
	cpipe_destroy(&pipe_from_cl_to_hang);
	return 0;
}

static void
canceled_worker_start()
{
	cord_costart(&canceled_worker, "canceled_worker",
	             canceled_worker_f, NULL);
}

static int
main_f(va_list ap)
{
	(void) ap;

	/* Start the endpoint's mutex hack */

	/* Initialize the endpoint mutex */
	tt_pthread_mutex_lock(&endpoint_hack_mutex_1);
	hang_worker_start();
	tt_pthread_cond_wait(&endpoint_hack_cond_1, &endpoint_hack_mutex_1);
	tt_pthread_mutex_unlock(&endpoint_hack_mutex_1);

	/*
	 * Create (only create) the canceled worker before the endpoint mutex will be locked
	 * for the hack work correctly
	*/
	tt_pthread_mutex_lock(&endpoint_hack_mutex_1);
	canceled_worker_start();
	tt_pthread_cond_wait(&endpoint_hack_cond_1, &endpoint_hack_mutex_1);
	tt_pthread_mutex_unlock(&endpoint_hack_mutex_1);

	tt_pthread_mutex_lock(&(hang_endpoint.mutex));

	/* Start canceled worker */
	tt_pthread_mutex_lock(&endpoint_hack_mutex_2);
	tt_pthread_cond_signal(&endpoint_hack_cond_2);
	tt_pthread_mutex_unlock(&endpoint_hack_mutex_2);

	while(hang_endpoint.mutex.__data.__lock < 2) {
		usleep(200);
	}

	tt_pthread_cancel(canceled_worker.id);
	tt_pthread_mutex_unlock(&(hang_endpoint.mutex));
	/* Hack end */

	tt_pthread_join(canceled_worker.id, NULL);

	unsigned join_timeout = 5;
	signal(SIGALRM, join_fail); // For exit in a hang case
	alarm(join_timeout);

	cpipe_create(&pipe_from_main_to_hang, "hang_worker");
	cbus_stop_loop(&pipe_from_main_to_hang);
	cpipe_destroy(&pipe_from_main_to_hang);

	cord_join(&hang_worker);
	ok(true, "The hang worker has been joined");
	alarm(0);

	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int
main()
{
	header();
	plan(1);

	memory_init();
	fiber_init(fiber_c_invoke);
	cbus_init();
	tt_pthread_cond_init(&endpoint_hack_cond_1, NULL);
	tt_pthread_mutex_init(&endpoint_hack_mutex_1, NULL);
	tt_pthread_cond_init(&endpoint_hack_cond_2, NULL);
	tt_pthread_mutex_init(&endpoint_hack_mutex_2, NULL);

	struct fiber *main_fiber = fiber_new("main", main_f);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);

	tt_pthread_cond_destroy(&endpoint_hack_cond_1);
	tt_pthread_cond_destroy(&endpoint_hack_cond_2);
	cbus_free();
	fiber_free();
	memory_free();

	int rc = check_plan();
	footer();
	return rc;
}
