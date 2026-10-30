/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tnt_thread.h"

#include "cbus.h"
#include "fiber_pool.h"

namespace {

static const char *const tx_endpoint_name = "tx_user";
/**
 * Fiber pool to handle the callback-messages coming from user-owned non-TX
 * threads.
 */
static struct fiber_pool tx_user_pool;

struct txpipe final : public cpipe {
public:
	txpipe()
	{
		cpipe_create_noev(this, tx_endpoint_name);
	}
	~txpipe()
	{
		cpipe_destroy(this);
	}
};

/** Storage for user callbacks to execute in TX. */
struct tnt_tx_msg {
	/** Parent class. */
	struct cmsg base;
	/** User callback. */
	tnt_tx_func_f func;
	/** Argument for the user callback. */
	void *arg;
};

static void
tnt_tx_msg_execute_f(struct cmsg *m)
{
	assert(cord_is_main());
	struct tnt_tx_msg *msg = (struct tnt_tx_msg *)m;
	msg->func(msg->arg);
	free(msg);
}

static cpipe *
tx_pipe(void)
{
	assert(!cord_is_main());
	/*
	 * The pipe is created on first access, one per thread. And is destroyed
	 * when the thread is terminated. As long as thread-local storage is
	 * handled correctly in the thread's runtime. That allows the user to
	 * just use the push/flush functions without having to manually create
	 * or destroy anything.
	 */
	static thread_local txpipe pipe;
	return &pipe;
}

} /* anon namespace */

API_EXPORT void
tnt_tx_push(tnt_tx_func_f func, void *arg)
{
	static const struct cmsg_hop route = {tnt_tx_msg_execute_f, NULL};
	struct tnt_tx_msg *msg = (struct tnt_tx_msg *)xmalloc(sizeof(*msg));
	cmsg_init(&msg->base, &route);
	msg->func = func;
	msg->arg = arg;
	cpipe_push(tx_pipe(), &msg->base);
}

API_EXPORT void
tnt_tx_flush(void)
{
	cpipe_flush(tx_pipe());
}

void
tnt_thread_init(void)
{
	/*
	 * The default is taken from box.cfg.net_msg_max. The purposes of IProto
	 * and tx_user pool are similar, which means it makes sense to keep
	 * their pool sizes the same.
	 */
	fiber_pool_create(&tx_user_pool, tx_endpoint_name, 768,
			  FIBER_POOL_IDLE_TIMEOUT);
}

void
tnt_thread_set_tx_user_pool_size(int size)
{
	fiber_pool_set_max_size(&tx_user_pool, size);
}

int
tnt_thread_get_tx_user_pool_size(void)
{
	return tx_user_pool.max_size;
}

void
tnt_thread_shutdown(void)
{
	fiber_pool_shutdown(&tx_user_pool);
}

void
tnt_thread_free(void)
{
	fiber_pool_destroy(&tx_user_pool);
}
