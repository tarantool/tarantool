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

/**
 * Cluster is a simple array of SWIM instances assigned to
 * different URIs.
 */
struct swim_cluster {
	int size;
	struct swim **node;
};

struct swim_cluster *
swim_cluster_new(int size)
{
	struct swim_cluster *res = (struct swim_cluster *) malloc(sizeof(*res));
	assert(res != NULL);
	int bsize = sizeof(res->node[0]) * size;
	res->node = (struct swim **) malloc(bsize);
	assert(res->node != NULL);
	res->size = size;
	struct tt_uuid uuid;
	memset(&uuid, 0, sizeof(uuid));
	char *uri = tt_static_buf();
	for (int i = 0; i < size; ++i) {
		res->node[i] = swim_new();
		assert(res->node[i] != NULL);
		sprintf(uri, "127.0.0.1:%d", i + 1);
		uuid.time_low = i + 1;
		int rc = swim_cfg(res->node[i], uri, -1, -1, -1, &uuid);
		assert(rc == 0);
		(void) rc;
	}
	return res;
}

#define swim_cluster_set_cfg(cluster, ...) ({				\
	for (int i = 0; i < cluster->size; ++i) {			\
		int rc = swim_cfg(cluster->node[i], __VA_ARGS__);	\
		assert(rc == 0);					\
		(void) rc;						\
	}								\
})

void
swim_cluster_set_ack_timeout(struct swim_cluster *cluster, double ack_timeout)
{
	swim_cluster_set_cfg(cluster, NULL, -1, ack_timeout, -1, NULL);
}

void
swim_cluster_set_gc(struct swim_cluster *cluster, enum swim_gc_mode gc_mode)
{
	swim_cluster_set_cfg(cluster, NULL, -1, -1, gc_mode, NULL);
}

void
swim_cluster_delete(struct swim_cluster *cluster)
{
	for (int i = 0; i < cluster->size; ++i)
		swim_delete(cluster->node[i]);
	free(cluster->node);
	free(cluster);
}

int
swim_cluster_add_link(struct swim_cluster *cluster, int to_id, int from_id)
{
	const struct swim_member *from = swim_self(cluster->node[from_id]);
	return swim_add_member(cluster->node[to_id], swim_member_uri(from),
			       swim_member_uuid(from));
}

static const struct swim_member *
swim_cluster_member_view(struct swim_cluster *cluster, int node_id,
			 int member_id)
{
	struct swim *node = cluster->node[node_id];
	const struct swim_member *member = swim_self(cluster->node[member_id]);
	const struct tt_uuid *member_uuid = swim_member_uuid(member);
	return swim_member_by_uuid(node, member_uuid);
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

struct swim *
swim_cluster_node(struct swim_cluster *cluster, int i)
{
	assert(i >= 0 && i < cluster->size);
	return cluster->node[i];
}

void
swim_cluster_restart_node(struct swim_cluster *cluster, int i)
{
	assert(i >= 0 && i < cluster->size);
	struct swim *s = cluster->node[i];
	const struct swim_member *self = swim_self(s);
	struct tt_uuid uuid = *swim_member_uuid(self);
	char uri[128];
	strcpy(uri, swim_member_uri(self));
	double ack_timeout = swim_ack_timeout(s);
	swim_delete(s);
	s = swim_new();
	assert(s != NULL);
	int rc = swim_cfg(s, uri, -1, ack_timeout, -1, &uuid);
	assert(rc == 0);
	(void) rc;
	cluster->node[i] = s;
}

void
swim_cluster_block_io(struct swim_cluster *cluster, int i)
{
	swim_test_transport_block_fd(swim_fd(cluster->node[i]));
}

void
swim_cluster_unblock_io(struct swim_cluster *cluster, int i)
{
	swim_test_transport_unblock_fd(swim_fd(cluster->node[i]));
}

void
swim_cluster_set_drop(struct swim_cluster *cluster, int i, bool value)
{
	swim_test_transport_set_drop(swim_fd(cluster->node[i]), value);
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
	struct swim **end = cluster->node + cluster->size;
	for (struct swim **s1 = cluster->node; s1 < end; ++s1) {
		for (struct swim **s2 = s1 + 1; s2 < end; ++s2) {
			if (! swim1_contains_swim2(*s1, *s2) ||
			    ! swim1_contains_swim2(*s2, *s1))
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
	while (! check(cluster, data)) {
		if (swim_time() >= deadline)
			return -1;
		swim_do_loop_step(loop());
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

/** Callback to check that a member matches a template. */
static bool
swim_loop_check_member(struct swim_cluster *cluster, void *data)
{
	struct swim_member_template *t = (struct swim_member_template *) data;
	const struct swim_member *m =
		swim_cluster_member_view(cluster, t->node_id, t->member_id);
	enum swim_member_status status;
	uint64_t incarnation;
	if (m != NULL) {
		status = swim_member_status(m);
		incarnation = swim_member_incarnation(m);
	} else {
		status = swim_member_status_MAX;
		incarnation = 0;
	}
	if (t->need_check_status && status != t->status)
		return false;
	if (t->need_check_incarnation && incarnation != t->incarnation)
		return false;
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

bool
swim_error_check_match(const char *msg)
{
	return strstr(diag_last_error(diag_get())->errmsg, msg) != NULL;
}
