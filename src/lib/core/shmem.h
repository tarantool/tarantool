#ifndef TARANTOOL_LIB_CORE_SHMEM_H_INCLUDED
#define TARANTOOL_LIB_CORE_SHMEM_H_INCLUDED
/*
 * Copyright 2010-2016 Tarantool AUTHORS: please see AUTHORS file.
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
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <stdbool.h>
#include <sys/types.h>
#include "small/small.h"
#include "small/rb.h"
#include "src/lib/uuid/tt_uuid.h"

/**
 * When configuring the database,
 * create a shared memory segment,
 * in which store the information about
 * the current instance:
 * initially, cpu id of the main thread
 * later, checkpoint daemon schedule
 * If the segment already exists,
 * read values of other instances in the segment,
 * and adjust own settings accordingly.
 * The segment is identifier by cluster UUID,
 * but the segment name can be reset in box.cfg{}.
 * The entire behaviour can be switched off in box.cfg.
 */

typedef struct node_s node_t; // ready
typedef struct bind_data_s bind_data_t; // ready
struct bind_data_s { // ready
	int cpu_id;
	int bind_res;
};
struct node_s { // ready
	rb_node(node_t) node;
	char *key;
	bind_data_t data;
};
typedef rb_tree(node_t) tree_t; // ready

typedef struct shm_s shm_t;
struct shm_s {
	char *name;
	int fd;
	void *start_addr;
	struct small_alloc *sl_alloc_pointer;
	size_t mmap_size;
	tree_t *shm_rtee_pointer;
};

static int
shmem_open(shm_t *sm);

static int
shmem_close(const shm_t *sm);

static node_t*
shmem_get(shm_t *sm, const char *key);

static int
shmem_put(shm_t *sm, const char *key, int cpu_id);


















#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif  /* TARANTOOL_LIB_CORE_SHMEM_H_INCLUDED */
