#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

/**
 * The state of the foreign thread started by run_a_thread().
 */
static struct {
	/**
	 * The mutex guarding the fields below.
	 */
	pthread_mutex_t mutex;
	/**
	 * Signalled when thread_id or should_stop is updated.
	 */
	pthread_cond_t cond;
	/**
	 * The TID passed to TX by the new thread, -1 until it is set.
	 */
	pid_t thread_id;
	/**
	 * The handle of the thread started by run_a_thread().
	 */
	pthread_t handle;
	/**
	 * True between pthread_create() and pthread_join().
	 */
	bool started;
	/**
	 * Set by stop_a_thread() to make the thread exit.
	 */
	bool should_stop;
} thread = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.cond = PTHREAD_COND_INITIALIZER,
	.thread_id = -1,
};

/**
 * The foreign thread to receive the SIGURG.
 */
static void *
thread_func(void *arg)
{
	(void)arg;
	/* Make it consume only SIGURG. */
	sigset_t set;
	sigfillset(&set);
	sigdelset(&set, SIGURG);
	pthread_sigmask(SIG_SETMASK, &set, NULL);

	/* Fill the thread ID and pass it to the creator. */
	pthread_mutex_lock(&thread.mutex);
	thread.thread_id = gettid();
	pthread_cond_signal(&thread.cond);
	/* Wait until stop_a_thread() asks the thread to exit. */
	while (!thread.should_stop)
		pthread_cond_wait(&thread.cond, &thread.mutex);
	pthread_mutex_unlock(&thread.mutex);
	return NULL;
}

/**
 * Start the foreign SIGURG receiver and return its TID.
 */
static int
l_run_a_thread(lua_State *L)
{
	pthread_mutex_lock(&thread.mutex);
	assert(!thread.started);
	thread.thread_id = -1;
	thread.should_stop = false;
	int rc = pthread_create(&thread.handle, NULL, thread_func, NULL);
	assert(rc == 0);
	(void)rc;
	thread.started = true;
	/* Wait until the thread has initialized and passed the TID. */
	while (thread.thread_id == -1)
		pthread_cond_wait(&thread.cond, &thread.mutex);
	pid_t tid = thread.thread_id;
	pthread_mutex_unlock(&thread.mutex);
	lua_pushinteger(L, tid);
	return 1;
}

/**
 * Stop the foreign SIGURG receiver and wait until it exits.
 */
static int
l_stop_a_thread(lua_State *L)
{
	(void)L;
	pthread_mutex_lock(&thread.mutex);
	if (thread.started) {
		thread.should_stop = true;
		pthread_cond_signal(&thread.cond);
		pthread_mutex_unlock(&thread.mutex);
		pthread_join(thread.handle, NULL);
		pthread_mutex_lock(&thread.mutex);
		thread.started = false;
	}
	pthread_mutex_unlock(&thread.mutex);
	return 0;
}

LUA_API int
luaopen_gh_12477_signal_logging(lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"run_a_thread", l_run_a_thread},
		{"stop_a_thread", l_stop_a_thread},
		{NULL, NULL},
	};
	luaL_register(L, "gh_12477_signal_logging", lib);
	return 1;
}
