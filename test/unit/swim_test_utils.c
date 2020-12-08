/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "swim_test_utils.h"
#include "swim/swim_ev.h"
#include "uuid/tt_uuid.h"
#include "trivia/util.h"
#include "msgpuck.h"
#include "trigger.h"
#include "memory.h"
#include "random.h"
#include <fcntl.h>

/**
 * Drop rate packet filter to drop packets with a certain
 * probability.
 */
struct swim_drop_rate {
	/** True if should be applied to incoming packets. */
	bool is_for_in;
	/** True if should be applied to outgoing packets. */
	bool is_for_out;
	/** Drop rate percentage. */
	double rate;
};

/** Initialize drop rate packet filter. */
static inline void
swim_drop_rate_create(struct swim_drop_rate *dr, double rate, bool is_for_in,
		      bool is_for_out)
{
	dr->is_for_in = is_for_in;
	dr->is_for_out = is_for_out;
	dr->rate = rate;
}

/** Packet filter to drop packets with specified destinations. */
struct swim_drop_channel {
	/**
	 * An array of file descriptors to drop messages sent to
	 * them.
	 */
	int *drop_fd;
	/** Length of @a drop_fd. */
	int drop_fd_size;
	/** Capacity of @a drop_fd. */
	int drop_fd_cap;
};

/** Initialize drop channel packet filter. */
static inline void
swim_drop_channel_create(struct swim_drop_channel *dc)
{
	dc->drop_fd = NULL;
	dc->drop_fd_size = 0;
	dc->drop_fd_cap = 0;
}

/**
 * Set @a new_fd file descriptor into @a dc drop channel packet
 * filter in place of @a old_fd descriptor. Just like dup2()
 * system call.
 * @retval 0 Success.
 * @retval -1 @a old_fd is not found.
 */
static inline int
swim_drop_channel_dup_fd(const struct swim_drop_channel *dc, int new_fd,
			 int old_fd)
{
	for (int i = 0; i < dc->drop_fd_size; ++i) {
		if (dc->drop_fd[i] == old_fd) {
			dc->drop_fd[i] = new_fd;
			return 0;
		}
	}
	return -1;
}

/** Add @a fd to @a dc drop channel packet filter. */
static inline void
swim_drop_channel_add_fd(struct swim_drop_channel *dc, int fd)
{
	if (swim_drop_channel_dup_fd(dc, fd, -1) == 0)
		return;
	dc->drop_fd_cap += dc->drop_fd_cap + 1;
	int new_bsize = dc->drop_fd_cap * sizeof(int);
	dc->drop_fd = (int *) realloc(dc->drop_fd, new_bsize);
	dc->drop_fd[dc->drop_fd_size++] = fd;
}

/** Destroy drop channel packet filter. */
static inline void
swim_drop_channel_destroy(struct swim_drop_channel *dc)
{
	free(dc->drop_fd);
}

/**
 * SWIM cluster node and its UUID. UUID is stored separately
 * because sometimes a test wants to drop a SWIM instance, but
 * still check how does it look in other membership instances.
 * UUID is necessary since it is a key to lookup a view of that
 * instance in the member tables.
 */
struct swim_node {
	/** SWIM instance. Can be NULL. */
	struct swim *swim;
	/**
	 * UUID. Is used when @a swim is NULL to lookup view of
	 * that instance.
	 */
	struct tt_uuid uuid;
	/** Generation to increment on restart. */
	uint64_t generation;
	/**
	 * Filter to drop packets with a certain probability
	 * from/to a specified direction.
	 */
	struct swim_drop_rate drop_rate;
	/** Filter to drop packets with specified destinations. */
	struct swim_drop_channel drop_channel;
};

/**
 * Cluster is a simple array of SWIM instances assigned to
 * different URIs.
 */
struct swim_cluster {
	int size;
	struct swim_node *node;
	/**
	 * Saved values to restart SWIM nodes with the most actual
	 * configuration.
	 */
	double ack_timeout;
	enum swim_gc_mode gc_mode;
};

/** Build URI of a SWIM instance for a given @a id. */
static inline void
swim_cluster_id_to_uri(char *buffer, int id)
{
	sprintf(buffer, "127.0.0.1:%d", id + 1);
}

/**
 * A trigger to check correctness of event context, and ability
 * to yield.
 */
int
swim_test_event_cb(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct swim_on_member_event_ctx *ctx =
		(struct swim_on_member_event_ctx *) event;
	assert(ctx->events != 0);
	assert((ctx->events & SWIM_EV_NEW) == 0 ||
	       (ctx->events & SWIM_EV_DROP) == 0);
	fiber_sleep(0);
	return 0;
}

/** Create a SWIM cluster node @a n with a 0-based @a id. */
static inline void
swim_node_create(struct swim_node *n, int id)
{
	n->generation = 0;
	n->swim = swim_new(0);
	assert(n->swim != NULL);
	struct trigger *t = (struct trigger *) malloc(sizeof(*t));
	trigger_create(t, swim_test_event_cb, NULL, (trigger_f0) free);
	trigger_add(swim_trigger_list_on_member_event(n->swim), t);

	char uri[128];
	swim_cluster_id_to_uri(uri, id);
	n->uuid = uuid_nil;
	n->uuid.time_low = id + 1;
	int rc = swim_cfg(n->swim, uri, -1, -1, -1, &n->uuid);
	assert(rc == 0);
	(void) rc;

	swim_drop_rate_create(&n->drop_rate, 0, false, false);
	swim_drop_channel_create(&n->drop_channel);
}

struct swim_cluster *
swim_cluster_new(int size)
{
	struct swim_cluster *res = (struct swim_cluster *) malloc(sizeof(*res));
	assert(res != NULL);
	int bsize = sizeof(res->node[0]) * size;
	res->node = (struct swim_node *) malloc(bsize);
	assert(res->node != NULL);
	res->size = size;
	res->ack_timeout = -1;
	res->gc_mode = SWIM_GC_DEFAULT;
	struct swim_node *n = res->node;
	for (int i = 0; i < size; ++i, ++n)
		swim_node_create(n, i);
	return res;
}

#define swim_cluster_set_cfg(cluster, func, ...) ({				\
	for (int i = 0; i < cluster->size; ++i) {			\
		int rc = func(cluster->node[i].swim, __VA_ARGS__);	\
		assert(rc == 0);					\
		(void) rc;						\
	}								\
})

void
swim_cluster_set_ack_timeout(struct swim_cluster *cluster, double ack_timeout)
{
	swim_cluster_set_cfg(cluster, swim_cfg, NULL, -1, ack_timeout, -1, NULL);
	cluster->ack_timeout = ack_timeout;
}

void
swim_cluster_set_codec(struct swim_cluster *cluster, enum crypto_algo algo,
		       enum crypto_mode mode, const char *key, int key_size)
{
	swim_cluster_set_cfg(cluster, swim_set_codec, algo, mode,
			     key, key_size);
}

void
swim_cluster_set_gc(struct swim_cluster *cluster, enum swim_gc_mode gc_mode)
{
	swim_cluster_set_cfg(cluster, swim_cfg, NULL, -1, -1, gc_mode, NULL);
	cluster->gc_mode = gc_mode;
}

void
swim_cluster_delete(struct swim_cluster *cluster)
{
	for (int i = 0; i < cluster->size; ++i) {
		if (cluster->node[i].swim != NULL)
			swim_delete(cluster->node[i].swim);
		swim_drop_channel_destroy(&cluster->node[i].drop_channel);
	}
	free(cluster->node);
	free(cluster);
}

/** Safely get node of @a cluster with id @a i. */
static inline struct swim_node *
swim_cluster_node(struct swim_cluster *cluster, int i)
{
	assert(i >= 0 && i < cluster->size);
	return &cluster->node[i];
}

struct swim *
swim_cluster_member(struct swim_cluster *cluster, int i)
{
	return swim_cluster_node(cluster, i)->swim;
}

int
swim_cluster_update_uuid(struct swim_cluster *cluster, int i,
			 const struct tt_uuid *new_uuid)
{
	struct swim_node *n = swim_cluster_node(cluster, i);
	if (swim_cfg(n->swim, NULL, -1, -1, -1, new_uuid) != 0)
		return -1;
	n->uuid = *new_uuid;
	return 0;
}

int
swim_cluster_add_link(struct swim_cluster *cluster, int to_id, int from_id)
{
	const struct swim_member *from =
		swim_self(swim_cluster_member(cluster, from_id));
	return swim_add_member(swim_cluster_member(cluster, to_id),
			       swim_member_uri(from), swim_member_uuid(from));
}

const struct swim_member *
swim_cluster_member_view(struct swim_cluster *cluster, int node_id,
			 int member_id)
{
	/*
	 * Do not use node[member_id].swim - it can be NULL
	 * already, for example, in case of quit or deletion.
	 */
	struct swim_node *n = swim_cluster_node(cluster, member_id);
	return swim_member_by_uuid(swim_cluster_member(cluster, node_id),
				   &n->uuid);
}

enum swim_member_status
swim_cluster_member_status(struct swim_cluster *cluster, int node_id,
			   int member_id)
{
	const struct swim_member *m =
		swim_cluster_member_view(cluster, node_id, member_id);
	if (m == NULL)
		return swim_member_status_MAX;
	return swim_member_status(m);
}

struct swim_incarnation
swim_cluster_member_incarnation(struct swim_cluster *cluster, int node_id,
				int member_id)
{
	const struct swim_member *m =
		swim_cluster_member_view(cluster, node_id, member_id);
	if (m == NULL) {
		struct swim_incarnation inc;
		swim_incarnation_create(&inc, UINT64_MAX, UINT64_MAX);
		return inc;
	}
	return swim_member_incarnation(m);
}

const char *
swim_cluster_member_payload(struct swim_cluster *cluster, int node_id,
			    int member_id, int *size)
{
	const struct swim_member *m =
		swim_cluster_member_view(cluster, node_id, member_id);
	if (m == NULL) {
		*size = 0;
		return NULL;
	}
	return swim_member_payload(m, size);
}

int
swim_cluster_member_set_payload(struct swim_cluster *cluster, int i,
				const char *payload, int size)
{
	struct swim *s = swim_cluster_member(cluster, i);
	return swim_set_payload(s, payload, size);
}

void
swim_cluster_quit_node(struct swim_cluster *cluster, int i)
{
	struct swim_node *n = swim_cluster_node(cluster, i);
	assert(tt_uuid_is_equal(&n->uuid,
				swim_member_uuid(swim_self(n->swim))));
	swim_quit(n->swim);
	n->swim = NULL;
}

void
swim_cluster_restart_node(struct swim_cluster *cluster, int i)
{
	struct swim_node *n = swim_cluster_node(cluster, i);
	struct swim *s = n->swim;
	char uri[128];
	swim_cluster_id_to_uri(uri, i);
	if (s != NULL) {
		assert(tt_uuid_is_equal(swim_member_uuid(swim_self(s)),
					&n->uuid));
		swim_delete(s);
	}
	s = swim_new(++n->generation);
	assert(s != NULL);
	int rc = swim_cfg(s, uri, -1, cluster->ack_timeout, cluster->gc_mode,
			  &n->uuid);
	assert(rc == 0);
	(void) rc;
	n->swim = s;
}

void
swim_cluster_block_io(struct swim_cluster *cluster, int i)
{
	fakenet_block(swim_fd(cluster->node[i].swim));
}

void
swim_cluster_unblock_io(struct swim_cluster *cluster, int i)
{
	struct swim *s = swim_cluster_member(cluster, i);
	fakenet_unblock(swim_fd(s));
}

/** Create a new drop rate filter helper. */
static inline struct swim_drop_rate *
swim_drop_rate_new(double rate, bool is_for_in, bool is_for_out)
{
	struct swim_drop_rate *dr =
		(struct swim_drop_rate *) malloc(sizeof(*dr));
	assert(dr != NULL);
	dr->rate = rate;
	dr->is_for_in = is_for_in;
	dr->is_for_out = is_for_out;
	return dr;
}

/**
 * A packet filter dropping a packet with a certain probability.
 */
static bool
swim_filter_drop_rate(const char *data, int size, void *udata, int dir,
		      int peer_fd)
{
	(void) data;
	(void) size;
	(void) peer_fd;
	struct swim_drop_rate *dr = (struct swim_drop_rate *) udata;
	if ((dir == 0 && !dr->is_for_in) || (dir == 1 && !dr->is_for_out))
		return false;
	return ((double) rand() / RAND_MAX) * 100 < dr->rate;
}

/**
 * Create a new drop rate filter for the instance with id @a i.
 */
static void
swim_cluster_set_drop_generic(struct swim_cluster *cluster, int i,
			      double value, bool is_for_in, bool is_for_out)
{
	struct swim_node *n = swim_cluster_node(cluster, i);
	int fd = swim_fd(n->swim);
	if (value == 0) {
		fakenet_remove_filter(fd, swim_filter_drop_rate);
		return;
	}
	swim_drop_rate_create(&n->drop_rate, value, is_for_in, is_for_out);
	fakenet_add_filter(fd, swim_filter_drop_rate, &n->drop_rate);
}

void
swim_cluster_set_drop(struct swim_cluster *cluster, int i, double value)
{
	swim_cluster_set_drop_generic(cluster, i, value, true, true);
}

void
swim_cluster_set_drop_out(struct swim_cluster *cluster, int i, double value)
{
	swim_cluster_set_drop_generic(cluster, i, value, false, true);
}

void
swim_cluster_set_drop_in(struct swim_cluster *cluster, int i, double value)
{
	swim_cluster_set_drop_generic(cluster, i, value, true, false);
}

/**
 * Check if the packet sender should drop a packet outgoing to
 * @a peer_fd file descriptor.
 */
static bool
swim_filter_drop_channel(const char *data, int size, void *udata, int dir,
			 int peer_fd)
{
	(void) data;
	(void) size;
	if (dir != 1)
		return false;
	struct swim_drop_channel *dc = (struct swim_drop_channel *) udata;
	/*
	 * Fullscan is totally ok - there are no more than 2-3
	 * blocks simultaneously in the tests.
	 */
	for (int i = 0; i < dc->drop_fd_size; ++i) {
		if (dc->drop_fd[i] == peer_fd)
			return true;
	}
	return false;
}

void
swim_cluster_set_drop_channel(struct swim_cluster *cluster, int from_id,
			      int to_id, bool value)
{
	int to_fd = swim_fd(swim_cluster_member(cluster, to_id));
	struct swim_node *from_node = swim_cluster_node(cluster, from_id);
	struct swim_drop_channel *dc = &from_node->drop_channel;
	if (! value) {
		swim_drop_channel_dup_fd(dc, -1, to_fd);
		return;
	}
	swim_drop_channel_add_fd(dc, to_fd);
	fakenet_add_filter(swim_fd(from_node->swim), swim_filter_drop_channel,
			   &from_node->drop_channel);
}

/** Check if @a s1 knows every member of @a s2's table. */
static inline bool
swim1_contains_swim2(struct swim *s1, struct swim *s2)
{
	struct swim_iterator *it = swim_iterator_open(s1);
	const struct swim_member *m;
	while ((m = swim_iterator_next(it)) != NULL) {
		if (swim_member_by_uuid(s2, swim_member_uuid(m)) == NULL) {
			swim_iterator_close(it);
			return false;
		}
	}
	swim_iterator_close(it);
	return true;
}

bool
swim_cluster_is_fullmesh(struct swim_cluster *cluster)
{
	struct swim_node *end = cluster->node + cluster->size;
	for (struct swim_node *s1 = cluster->node; s1 < end; ++s1) {
		if (s1->swim == NULL)
			continue;
		for (struct swim_node *s2 = s1 + 1; s2 < end; ++s2) {
			if (s2->swim == NULL)
				continue;
			if (! swim1_contains_swim2(s1->swim, s2->swim) ||
			    ! swim1_contains_swim2(s2->swim, s1->swim))
				return false;
		}
	}
	return true;
}

typedef bool (*swim_loop_check_f)(struct swim_cluster *cluster, void *data);

/**
 * Run the event loop until timeout happens or a custom
 * test-defined condition is met.
 * @param timeout Maximal number of bogus seconds to run the loop
 *        for.
 * @param cluster Cluster to test for a condition.
 * @param check Function condition-checker. It should return true,
 *        when the condition is met.
 * @param data Arbitrary test data passed to @a check without
 *        changes.
 *
 * @retval -1 Timeout, condition is not satisfied.
 * @retval 0 Success, condition is met before timeout.
 */
static int
swim_wait_timeout(double timeout, struct swim_cluster *cluster,
		  swim_loop_check_f check, void *data)
{
	fakeev_set_brk(timeout);
	double deadline = fakeev_time() + timeout;
	struct ev_loop *loop = fakeev_loop();
	/*
	 * There can be pending out of bound IO events, affecting
	 * the result. For example, 'quit' messages, which are
	 * send immediately without preliminary timeouts or
	 * whatsoever.
	 */
	fakenet_loop_update(loop);
	if (cluster != NULL)
		swim_cluster_run_triggers(cluster);
	while (! check(cluster, data)) {
		if (fakeev_time() >= deadline)
			return -1;
		fakeev_loop_update(loop);
		/*
		 * After events are processed, it is possible that
		 * some of them generated IO events. Process them
		 * too.
		 */
		fakenet_loop_update(loop);
		if (cluster != NULL)
			swim_cluster_run_triggers(cluster);
	}
	return 0;
}

/** Wrapper to check a cluster for fullmesh for timeout. */
static bool
swim_loop_check_fullmesh(struct swim_cluster *cluster, void *data)
{
	(void) data;
	return swim_cluster_is_fullmesh(cluster);
}

int
swim_cluster_wait_fullmesh(struct swim_cluster *cluster, double timeout)
{
	return swim_wait_timeout(timeout, cluster, swim_loop_check_fullmesh,
				 NULL);
}

/**
 * Wrapper to run the loop until timeout with an unreachable
 * condition.
 */
static bool
swim_loop_check_false(struct swim_cluster *cluster, void *data)
{
	(void) data;
	(void) cluster;
	return false;
}

void
swim_run_for(double duration)
{
	swim_wait_timeout(duration, NULL, swim_loop_check_false, NULL);
}

/**
 * A helper structure to carry some parameters into a callback
 * which checks a condition after each SWIM loop iteration. It
 * describes one member and what to check for that member.
 */
struct swim_member_template {
	/**
	 * In SWIM a member is a relative concept. The same member
	 * can look differently on various SWIM instances. This
	 * attribute specifies from which view the member should
	 * be looked at.
	 */
	int node_id;
	/** Ordinal number of the SWIM member in the cluster. */
	int member_id;
	/**
	 * True, if the status should be checked to be equal to @a
	 * status.
	 */
	bool need_check_status;
	enum swim_member_status status;
	/**
	 * True, if the incarnation should be checked to be equal
	 * to @a incarnation.
	 */
	bool need_check_incarnation;
	struct swim_incarnation incarnation;
	/**
	 * True, if the payload should be checked to be equal to
	 * @a payload of size @a payload_size.
	 */
	bool need_check_payload;
	const char *payload;
	int payload_size;
};

/** Build member template. No checks are set. */
static inline void
swim_member_template_create(struct swim_member_template *t, int node_id,
			    int member_id)
{
	memset(t, 0, sizeof(*t));
	t->node_id = node_id;
	t->member_id = member_id;
}

/**
 * Set that the member template should be used to check member
 * status.
 */
static inline void
swim_member_template_set_status(struct swim_member_template *t,
				enum swim_member_status status)
{
	t->need_check_status = true;
	t->status = status;
}

/**
 * Set that the member template should be used to check member
 * incarnation.
 */
static inline void
swim_member_template_set_incarnation(struct swim_member_template *t,
				     uint64_t generation, uint64_t version)
{
	t->need_check_incarnation = true;
	swim_incarnation_create(&t->incarnation, generation, version);
}

/**
 * Set that the member template should be used to check member
 * status.
 */
static inline void
swim_member_template_set_payload(struct swim_member_template *t,
				 const char *payload, int payload_size)
{
	t->need_check_payload = true;
	t->payload = payload;
	t->payload_size = payload_size;
}

/** Callback to check that a member matches a template. */
static bool
swim_loop_check_member(struct swim_cluster *cluster, void *data)
{
	struct swim_member_template *t = (struct swim_member_template *) data;
	const struct swim_member *m =
		swim_cluster_member_view(cluster, t->node_id, t->member_id);
	enum swim_member_status status;
	struct swim_incarnation incarnation;
	const char *payload;
	int payload_size;
	if (m != NULL) {
		status = swim_member_status(m);
		incarnation = swim_member_incarnation(m);
		payload = swim_member_payload(m, &payload_size);
	} else {
		status = swim_member_status_MAX;
		swim_incarnation_create(&incarnation, 0, 0);
		payload = NULL;
		payload_size = 0;
	}
	if (t->need_check_status && status != t->status)
		return false;
	if (t->need_check_incarnation &&
	    swim_incarnation_cmp(&incarnation, &t->incarnation) != 0)
		return false;
	if (t->need_check_payload &&
	    (payload_size != t->payload_size ||
	     memcmp(payload, t->payload, payload_size) != 0))
		return false;
	return true;
}

/**
 * Callback to check that a member matches a template on any
 * instance in the cluster.
 */
static bool
swim_loop_check_member_anywhere(struct swim_cluster *cluster, void *data)
{
	struct swim_member_template *t = (struct swim_member_template *) data;
	for (t->node_id = 0; t->node_id < cluster->size; ++t->node_id) {
		if (t->node_id != t->member_id &&
		    swim_loop_check_member(cluster, data))
			return true;
	}
	return false;
}

/**
 * Callback to check that a member matches a template on every
 * instance in the cluster.
 */
static bool
swim_loop_check_member_everywhere(struct swim_cluster *cluster, void *data)
{
	struct swim_member_template *t = (struct swim_member_template *) data;
	for (t->node_id = 0; t->node_id < cluster->size; ++t->node_id) {
		if (t->node_id != t->member_id &&
		    !swim_loop_check_member(cluster, data))
			return false;
	}
	return true;
}


int
swim_cluster_wait_status(struct swim_cluster *cluster, int node_id,
			 int member_id, enum swim_member_status status,
			 double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, node_id, member_id);
	swim_member_template_set_status(&t, status);
	return swim_wait_timeout(timeout, cluster, swim_loop_check_member, &t);
}

int
swim_cluster_wait_incarnation(struct swim_cluster *cluster, int node_id,
			      int member_id, uint64_t generation,
			      uint64_t version, double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, node_id, member_id);
	swim_member_template_set_incarnation(&t, generation, version);
	return swim_wait_timeout(timeout, cluster, swim_loop_check_member, &t);
}

int
swim_cluster_wait_status_anywhere(struct swim_cluster *cluster, int member_id,
				  enum swim_member_status status,
				  double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, -1, member_id);
	swim_member_template_set_status(&t, status);
	return swim_wait_timeout(timeout, cluster,
				 swim_loop_check_member_anywhere, &t);
}

int
swim_cluster_wait_status_everywhere(struct swim_cluster *cluster, int member_id,
				    enum swim_member_status status,
				    double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, -1, member_id);
	swim_member_template_set_status(&t, status);
	return swim_wait_timeout(timeout, cluster,
				 swim_loop_check_member_everywhere, &t);
}

int
swim_cluster_wait_payload_everywhere(struct swim_cluster *cluster,
				     int member_id, const char *payload,
				     int payload_size, double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, -1, member_id);
	swim_member_template_set_payload(&t, payload, payload_size);
	return swim_wait_timeout(timeout, cluster,
				 swim_loop_check_member_everywhere, &t);
}

void
swim_cluster_run_triggers(struct swim_cluster *cluster)
{
	bool has_events;
	do {
		has_events = false;
		struct swim_node *n = cluster->node;
		for (int i = 0; i < cluster->size; ++i, ++n) {
			if (n->swim != NULL &&
			    swim_has_pending_events(n->swim)) {
				has_events = true;
				fiber_sleep(0);
			}
		}
	} while (has_events);
}

bool
swim_error_check_match(const char *msg)
{
	return strstr(diag_last_error(diag_get())->errmsg, msg) != NULL;
}

void
swim_run_test(const char *log_file, fiber_func test)
{
	random_init();
	time_t seed = time(NULL);
	srand(seed);
	memory_init();
	fiber_init(fiber_c_invoke);
	int fd = open(log_file, O_TRUNC);
	if (fd != -1)
		close(fd);
	say_logger_init(log_file, 5, 1, "plain", 0);
	/*
	 * Print the seed to be able to reproduce a bug with the
	 * same seed.
	 */
	say_info("Random seed = %llu", (unsigned long long) seed);
	say_info("xoshiro random state = %s", xoshiro_state_str());

	struct fiber *main_fiber = fiber_new("main", test);
	fiber_set_joinable(main_fiber, true);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);
	fiber_join(main_fiber);

	say_logger_free();
	fiber_free();
	memory_free();
	random_free();
}
