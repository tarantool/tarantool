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
#include "swim_test_ev.h"
#include "swim_test_transport.h"
#include "swim/swim_ev.h"
#include "uuid/tt_uuid.h"
#include "trivia/util.h"
#include "fiber.h"
#include "msgpuck.h"

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
	struct tt_uuid uuid;
	memset(&uuid, 0, sizeof(uuid));
	char *uri = tt_static_buf();
	struct swim_node *n = res->node;
	for (int i = 0; i < size; ++i, ++n) {
		n->swim = swim_new();
		assert(n->swim != NULL);
		swim_cluster_id_to_uri(uri, i);
		uuid.time_low = i + 1;
		n->uuid = uuid;
		int rc = swim_cfg(n->swim, uri, -1, -1, -1, &uuid);
		assert(rc == 0);
		(void) rc;
	}
	return res;
}

#define swim_cluster_set_cfg(cluster, ...) ({				\
	for (int i = 0; i < cluster->size; ++i) {			\
		int rc = swim_cfg(cluster->node[i].swim, __VA_ARGS__);	\
		assert(rc == 0);					\
		(void) rc;						\
	}								\
})

void
swim_cluster_set_ack_timeout(struct swim_cluster *cluster, double ack_timeout)
{
	swim_cluster_set_cfg(cluster, NULL, -1, ack_timeout, -1, NULL);
	cluster->ack_timeout = ack_timeout;
}

void
swim_cluster_set_gc(struct swim_cluster *cluster, enum swim_gc_mode gc_mode)
{
	swim_cluster_set_cfg(cluster, NULL, -1, -1, gc_mode, NULL);
	cluster->gc_mode = gc_mode;
}

void
swim_cluster_delete(struct swim_cluster *cluster)
{
	for (int i = 0; i < cluster->size; ++i) {
		if (cluster->node[i].swim != NULL)
			swim_delete(cluster->node[i].swim);
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

static const struct swim_member *
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

uint64_t
swim_cluster_member_incarnation(struct swim_cluster *cluster, int node_id,
				int member_id)
{
	const struct swim_member *m =
		swim_cluster_member_view(cluster, node_id, member_id);
	if (m == NULL)
		return UINT64_MAX;
	return swim_member_incarnation(m);
}

const char *
swim_cluster_member_payload(struct swim_cluster *cluster, int node_id,
			    int member_id, uint16_t *size)
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
				const char *payload, uint16_t size)
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
	s = swim_new();
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
	swim_test_transport_block_fd(swim_fd(cluster->node[i].swim));
}

void
swim_cluster_unblock_io(struct swim_cluster *cluster, int i)
{
	struct swim *s = swim_cluster_member(cluster, i);
	swim_test_transport_unblock_fd(swim_fd(s));
}

/** A structure used by drop rate packet filter. */
struct swim_drop_rate {
	/** True if should be applied to incoming packets. */
	bool is_for_in;
	/** True if should be applied to outgoing packets. */
	bool is_for_out;
	/** Drop rate percentage. */
	double rate;
};

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
swim_filter_drop_rate(const char *data, int size, void *udata, int dir)
{
	(void) data;
	(void) size;
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
	int fd = swim_fd(swim_cluster_member(cluster, i));
	if (value == 0) {
		swim_test_transport_remove_filter(fd, swim_filter_drop_rate);
		return;
	}
	struct swim_drop_rate *dr = swim_drop_rate_new(value, is_for_in,
						       is_for_out);
	swim_test_transport_add_filter(fd, swim_filter_drop_rate, free, dr);
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
 * A list of components to drop used by component packet filter.
 */
struct swim_drop_components {
	/** List of component body keys. */
	const int *keys;
	/** Length of @a keys. */
	int key_count;
};

/**
 * Check if a packet contains any of the components to filter out.
 */
static bool
swim_filter_drop_component(const char *data, int size, void *udata, int dir)
{
	(void) size;
	(void) dir;
	struct swim_drop_components *dc = (struct swim_drop_components *) udata;
	/* Skip meta. */
	mp_next(&data);
	int map_size = mp_decode_map(&data);
	for (int i = 0; i < map_size; ++i) {
		int key = mp_decode_uint(&data);
		for (int j = 0; j < dc->key_count; ++j) {
			if (dc->keys[j] == key)
				return true;
		}
		/* Skip value. */
		mp_next(&data);
	}
	return false;
}

void
swim_cluster_drop_components(struct swim_cluster *cluster, int i,
			     const int *keys, int key_count)
{
	int fd = swim_fd(swim_cluster_member(cluster, i));
	if (key_count == 0) {
		swim_test_transport_remove_filter(fd,
						  swim_filter_drop_component);
		return;
	}
	struct swim_drop_components *dc =
		(struct swim_drop_components *) malloc(sizeof(*dc));
	assert(dc != NULL);
	dc->key_count = key_count;
	dc->keys = keys;
	swim_test_transport_add_filter(fd, swim_filter_drop_component, free,
				       dc);
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
	swim_ev_set_brk(timeout);
	double deadline = swim_time() + timeout;
	struct ev_loop *loop = loop();
	/*
	 * There can be pending out of bound IO events, affecting
	 * the result. For example, 'quit' messages, which are
	 * send immediately without preliminary timeouts or
	 * whatsoever.
	 */
	swim_test_transport_do_loop_step(loop);
	while (! check(cluster, data)) {
		if (swim_time() >= deadline)
			return -1;
		swim_test_ev_do_loop_step(loop);
		/*
		 * After events are processed, it is possible that
		 * some of them generated IO events. Process them
		 * too.
		 */
		swim_test_transport_do_loop_step(loop);
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
	uint64_t incarnation;
	/**
	 * True, if the payload should be checked to be equal to
	 * @a payload of size @a payload_size.
	 */
	bool need_check_payload;
	const char *payload;
	uint16_t payload_size;
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
				     uint64_t incarnation)
{
	t->need_check_incarnation = true;
	t->incarnation = incarnation;
}

/**
 * Set that the member template should be used to check member
 * status.
 */
static inline void
swim_member_template_set_payload(struct swim_member_template *t,
				 const char *payload, uint16_t payload_size)
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
	uint64_t incarnation;
	const char *payload;
	uint16_t payload_size;
	if (m != NULL) {
		status = swim_member_status(m);
		incarnation = swim_member_incarnation(m);
		payload = swim_member_payload(m, &payload_size);
	} else {
		status = swim_member_status_MAX;
		incarnation = 0;
		payload = NULL;
		payload_size = 0;
	}
	if (t->need_check_status && status != t->status)
		return false;
	if (t->need_check_incarnation && incarnation != t->incarnation)
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
			      int member_id, uint64_t incarnation,
			      double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, node_id, member_id);
	swim_member_template_set_incarnation(&t, incarnation);
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
				     uint16_t payload_size, double timeout)
{
	struct swim_member_template t;
	swim_member_template_create(&t, -1, member_id);
	swim_member_template_set_payload(&t, payload, payload_size);
	return swim_wait_timeout(timeout, cluster,
				 swim_loop_check_member_everywhere, &t);
}

bool
swim_error_check_match(const char *msg)
{
	return strstr(diag_last_error(diag_get())->errmsg, msg) != NULL;
}
