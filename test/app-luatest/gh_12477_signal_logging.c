#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "module.h"
#include "msgpuck.h"

/* The TID passed to TX by the new thread. */
pid_t thread_id = -1;
pthread_mutex_t thread_id_mutex;
pthread_cond_t thread_id_cond;

/* The external thread to receive the SIGURG. */
static void *
thread_func(void *arg)
{
	/* Make it able to only consume SIGURG. */
	sigset_t set;
	sigfillset(&set);
	sigdelset(&set, SIGURG);
	pthread_sigmask(SIG_SETMASK, &set, NULL);

	/* Fill the process and thread IDs and pass them to the creator. */
	pthread_mutex_lock(&thread_id_mutex);
	thread_id = gettid();
	pthread_cond_signal(&thread_id_cond);
	pthread_mutex_unlock(&thread_id_mutex);

	/* Just a loop to have it persistent and receiving signals. */
	while (true)
		sleep(1);
	return NULL;
}

int
run_a_thread(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	/* Init the thread_id guards. */
	pthread_mutex_init(&thread_id_mutex, NULL);
	pthread_cond_init(&thread_id_cond, NULL);

	/* Create the foreign SIGURG receiver. */
	pthread_t thread_handle;
	pthread_create(&thread_handle, NULL, thread_func, NULL);

	/* Wait till it's initialized and passed TID. */
	pthread_mutex_lock(&thread_id_mutex);
	pthread_cond_wait(&thread_id_cond, &thread_id_mutex);
	long tid = thread_id;
	pthread_mutex_unlock(&thread_id_mutex);

	/* Clean-up thread_id guards. */
	pthread_mutex_destroy(&thread_id_mutex);
	pthread_cond_destroy(&thread_id_cond);

	/* Return the foreign thread ID. */
	assert(tid > 0);
	const size_t data_size = mp_sizeof_uint(tid);
	char data[data_size];
	char *data_end = mp_encode_uint(data, tid);
	return box_return_mp(ctx, data, data_end);
}
