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
#include "swim/swim.h"
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
		int rc = swim_cfg(res->node[i], uri, -1, &uuid);
		assert(rc == 0);
		(void) rc;
	}
	return res;
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

struct swim *
swim_cluster_node(struct swim_cluster *cluster, int i)
{
	assert(i >= 0 && i < cluster->size);
	return cluster->node[i];
}

void
swim_cluster_block_io(struct swim_cluster *cluster, int i, double delay)
{
	swim_test_ev_block_fd(swim_fd(cluster->node[i]), delay);
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

int
swim_cluster_wait_fullmesh(struct swim_cluster *cluster, double timeout)
{
	double deadline = swim_time() + timeout;
	while (! swim_cluster_is_fullmesh(cluster)) {
		if (swim_time() >= deadline)
			return -1;
		swim_do_loop_step(loop());
	}
	return 0;
}

bool
swim_error_check_match(const char *msg)
{
	return strstr(diag_last_error(diag_get())->errmsg, msg) != NULL;
}
