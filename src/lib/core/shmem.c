/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "shmem.h"
#include "diag.h"
#include "memory.h"
#include "say.h"
#include "small/small.h"
#include <small/quota.h>

static struct slab_arena arena;
static struct slab_cache cache;
static struct small_alloc alloc;
static struct quota quota;
static bool shmem_is_in_use = false;

static tree_t tree;

enum {
	OBJSIZE_MIN = 3 * sizeof(int),
	OBJECTS_MAX = 1000
};

static inline int
key_cmp(const char *a, const char *b) //ready
{
	int res =  strcmp(a, b);
	return (res > 0) ? 1 : (res == 0) ? 0 : -1;
}
static inline int
key_node_cmp(const char *a, const node_t *b) //ready
{
	return key_cmp(a, b->key);
}
static inline int
node_cmp(const node_t *a, const node_t *b) //ready
{
	return key_cmp(a->key, b->key);
}

rb_gen_ext_key(MAYBE_UNUSED static inline, shmem_, tree_t, // ready
			node_t, node, node_cmp,
			const char *, key_node_cmp);

/**
 * Cache used for allocating memory for shared memory
 * in the tx thread.
 */

static void
shmem_sl_cache_init()
{
	quota_init(&quota, UINT_MAX);
	slab_arena_create(&arena, &quota, 0, 4000000, MAP_SHARED); // TODO: shared?
	slab_cache_create(&cache, &arena);

	small_alloc_create(&alloc, &cache, OBJSIZE_MIN, 1.3);
	small_alloc_setopt(&alloc, SMALL_DELAYED_FREE_MODE, 1); // TODO: need to ?
}

static int
shmem_open(shm_t *sm)
{
    // flock();
	int flags = O_RDWR | O_CREAT | O_RDONLY;
	mode_t perms = S_IRUSR | S_IWUSR;

	if ((sm->fd = shm_open(sm->name, flags, perms)) == -1) {
		say_error("shm_open error");
		diag_log();
		return -1;
    }

	if (!shmem_is_in_use) {
		shmem_is_in_use = true;
		shmem_sl_cache_init(); // TODO: refactoring

		if (ftruncate(sm->fd, sm->mmap_size) == -1) { // truncate new place for shmem
			say_error("ftruncate");
			diag_log();
			return -1;
		}
		shmem_new(&tree);
	}

	sm->sl_alloc_pointer = &alloc;
	if ((sm->start_addr = mmap(sm->sl_alloc_pointer, sm->mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, sm->fd, 0)) == MAP_FAILED) {
		say_error("mmap");
		diag_log();
		return -1;
	}
	sm->shm_rtee_pointer = &tree;
	return 0;
}

static int
shmem_close(const shm_t *sm) // ready?
{
	if(shm_unlink(sm->name) == -1) {
		say_error("shm_unlink");
		diag_log();
	}
	if(close(sm->fd) == -1) {
		say_error("close");
		diag_log();
	}
	if (munmap(sm->start_addr, sm->mmap_size) == -1){
		say_error("munmap");
		diag_log();
	}
	shmem_is_in_use = false;
	small_alloc_destroy(&alloc);
	slab_cache_destroy(&cache);
	slab_arena_destroy(&arena);
	return 0;
}

static node_t*
shmem_get(shm_t *sm, const char *key)
{
	node_t *ex_node = shmem_search(sm->shm_rtee_pointer, key);
	return (ex_node != NULL)? ex_node : NULL;
}

static int
shmem_put(shm_t *sm, const char *key, int cpu_id)
{
	node_t *ex_node = shmem_search(&tree, key);
	if(ex_node == NULL) {
		node_t *node = (node_t *)smalloc(sm->sl_alloc_pointer, sizeof(*node));

		node->key = (char*)smalloc(sm->sl_alloc_pointer, sizeof (key));
		memcpy(node->key, (char*)key, sizeof (key));

		node->data.cpu_id = cpu_id;
		node->data.bind_res = true;
		shmem_insert(&tree, node);
	}
	return 0;
}
