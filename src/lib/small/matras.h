#ifndef INCLUDES_TARANTOOL_SMALL_MATRAS_H
#define INCLUDES_TARANTOOL_SMALL_MATRAS_H
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

/* {{{ Description */
/*
 * matras - Memory Address TRanSlation Allocator (Smile)
 * matras is as allocator, that provides aligned blocks of specified
 * size (N), and a 32-bit integer identifiers for
 * each returned block. Block identifiers grow incrementally
 * starting from 0.
 *
 * The block size (N) must be a power of 2 (checked by assert in
 * the debug build). matras can restore a pointer to the block
 * give block ID, so one can store such 32-bit ids instead of
 * storing pointers to blocks.
 *
 * Since block IDs grow incrementally from 0 and matras
 * instance stores the number of provided blocks, there is a
 * simple way to iterate over all provided blocks.
 *
 * Implementation
 * --------------
 * To support block allocation, matras allocates extents of memory
 * by means of the supplied allocator, each extent having the same
 * size (M), M is a power of 2 and a multiple of N.
 * There is no way to free a single block, except the last one,
 * allocated, which happens to be the one with the largest ID.
 * Destroying a matras instance frees all allocated extents.
 *
 * Address translation
 * -------------------
 * To implement 32-bit address space for block identifiers,
 * matras maintains a simple tree of address translation tables.
 *
 * * First N1 bits of the identifier denote a level 0 extend
 *   id, which stores the address of level 1 extent.
 *
 * * Second N2 bits of block identifier stores the address
 *   of a level 2 extent, which stores actual blocks.
 *
 * * The remaining N3 bits denote the block number
 *   within the extent.
 *
 * Actual values of N1 and N2 are a function of block size B,
 * extent size M and sizeof(void *).
 *
 * To sum up, with a given N and M matras instance:
 *
 * 1) can provide not more than
 *    pow(M / sizeof(void*), 2) * (M / N) blocks
 *
 * 2) costs 2 random memory accesses to provide a new block
 *    or restore a block pointer from block id
 *
 * 3) has an approximate memory overhead of size (L * M)
 *
 * Of course, the integer type used for block id (matras_id_t,
 * usually is a typedef to uint32) also limits the maximum number
 * of objects that can be allocated by a single instance of matras.
 *
 * Versioning
 * ----------
 * Starting from Tarantool 1.6, matras implements a way to create
 * a consistent read view of allocated data with
 * matras_create_read_view(). Once a read view is
 * created, the same block identifier can return two different
 * physical addresses in two views: the created view
 * and the current or latest view. Multiple read views can be
 * created.  To work correctly with possibly existing read views,
 * the application must inform matras that data in a block is about to
 * change, using matras_touch() call. Only a block which belong to
 * the current, i.e. latest, view, can be changed: created
 * views are immutable.
 *
 * The implementation of read views is based on copy-on-write
 * technique, which is cheap enough as long as not too many
 * objects have to be touched while a view exists.
 * Another important property of the copy-on-write mechanism is
 * that whenever a write occurs, the writer pays the penalty
 * and copies the block to a new location, and gets a new physical
 * address for the same block id. The reader keeps using the
 * old address. This makes it possible to access the
 * created read view in a concurrent thread, as long as this
 * thread is created after the read view itself is created.
 */
/* }}} */

#include <assert.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Type of a block ID.
 */
#ifdef WIN32
typedef unsigned __int32 matras_id_t;
#else
typedef uint32_t matras_id_t;
#endif

/**
 * Type of the extent allocator (the allocator for regions
 * of size M). Is allowed to return NULL, but is not allowed
 * to throw an exception
 */
typedef void *(*matras_alloc_func)();
typedef void (*matras_free_func)(void *);

/**
 * sruct matras_view represents appropriate mapping between
 * block ID and it's pointer.
 * matras structure has one main read/write view, and a number
 * of user created read-only views.
 */
struct matras_view {
	/* root extent of the view */
	void *root;
	/* block count in the view */
	matras_id_t block_count;
	/* all views are linked into doubly linked list */
	struct matras_view *prev_view, *next_view;
};

/**
 * matras - memory allocator of blocks of equal
 * size with support of address translation.
 */
struct matras {
	/* Main read/write view of the matras */
	struct matras_view head;
	/* Block size (N) */
	matras_id_t block_size;
	/* Extent size (M) */
	matras_id_t extent_size;
	/* Numberof allocated extents */
	matras_id_t extent_count;
	/* binary logarithm  of maximum possible created blocks count */
	matras_id_t log2_capacity;
	/* See "Shifts and masks explanation" below  */
	matras_id_t shift1, shift2;
	/* See "Shifts and masks explanation" below  */
	matras_id_t mask1, mask2;
	/* External extent allocator */
	matras_alloc_func alloc_func;
	/* External extent deallocator */
	matras_free_func free_func;
};

/*
 * "Shifts and masks explanation"
 * For 3-level matras (L = 3), as claimed above, block ID consist of
 * three parts (N1, N2 and N3).
 * In order to optimize splitting ID, several masks and shifts
 * are precalculated during matras initialization.
 * Heres is an example block ID bits, high order bits first:
 * ID :   0  0  0  0  N1 N1 N1 N1 N2 N2 N2 N2 N3 N3 N3 N3 N3
 * mask1: 0  0  0  0  0  0  0  0  1  1  1  1  1  1  1  1  1
 * mask1: 0  0  0  0  0  0  0  0  0  0  0  0  1  1  1  1  1
 *                                <---------shift1-------->
 *                                            <---shift2-->
 * When defined in such way, one can simply split ID to N1, N2 and N3:
 * N1 = ID >> shift1
 * N2 = (ID & mask1) >> shift2
 * N3 = ID & mask2
 */

/*
 * matras API declaration
 */

/**
 * Initialize an empty instance of pointer translating
 * block allocator. Does not allocate memory.
 */
void
matras_create(struct matras *m, matras_id_t extent_size, matras_id_t block_size,
	      matras_alloc_func alloc_func, matras_free_func free_func);

/**
 * Free all memory used by an instance of matras and
 * reinitialize it.
 * Identical to matras_destroy(m); matras_create(m, ...);
 */
void
matras_reset(struct matras *m);

/**
 * Free all memory used by an instance of matras.
 */
void
matras_destroy(struct matras *m);

/**
 * Allocate a new block. Return both, block pointer and block
 * id.
 *
 * @retval NULL failed to allocate memory
 */
void *
matras_alloc(struct matras *m, matras_id_t *id);

/*
 * Deallocate last block (block with maximum ID)
 */
void
matras_dealloc(struct matras *m);

/**
 * Allocate a range_count of blocks. Return both, first block pointer
 * and first block id. This method only works if current number of blocks and
 * number of blocks in one extent are divisible by range_count.
 * range_count must also be less or equal to number of blocks in one extent.
 *
 * @retval NULL failed to allocate memory
 */
void *
matras_alloc_range(struct matras *m, matras_id_t *id, matras_id_t range_count);

/*
 * Deallocate last range_count of blocks (blocks with maximum ID)
 * This method only works if current number of blocks and
 * number of blocks in one extent are divisible by range_count.
 * range_count must also be less or equal to number of blocks in one extent.
 */
void
matras_dealloc_range(struct matras *m, matras_id_t range_count);

/**
 * Convert block id into block address.
 */
static void *
matras_get(const struct matras *m, matras_id_t id);

/**
 * Convert block id of a specified version into block address.
 */
static void *
matras_view_get(const struct matras *m, const struct matras_view *v,
		matras_id_t id);

/*
 * Getting number of allocated extents (of size extent_size each)
*/
matras_id_t
matras_extent_count(const struct matras *m);

/*
 * Connect read view to the matras so that it is always connected with main,
 *  "head" read view. Such a read view does not consume any resources and
 *  should not be destroyed.
 */
static void
matras_head_read_view(struct matras_view *v);

/*
 * Create new read view.
 */
void
matras_create_read_view(struct matras *m, struct matras_view *v);

/*
 * Delete a read view.
 */
void
matras_destroy_read_view(struct matras *m, struct matras_view *v);

/*
 * Determine if the read view is created.
 * @return 1 if the read view was created with matras_create_read_view
 * @return 0 if the read view was initialized with matras_head_read_view
 */
static int
matras_is_read_view_created(struct matras_view *v);

/*
 * Notify matras that memory at given ID will be changed.
 * Returns (perhaps new) address of memory associated with that block.
 * Returns NULL on memory error
 * Only needed (and does any work) if some versions are used.
 */
void *
matras_touch(struct matras *m, matras_id_t id);

/*
 * matras_head_read_view implementation.
 */
static inline void
matras_head_read_view(struct matras_view *v)
{
	v->next_view = 0;
}

/*
 * matras_is_read_view_created implementation.
 */
static inline int
matras_is_read_view_created(struct matras_view *v)
{
	return v->next_view ? 1 : 0;
}

/**
 * Common part of matras_view_get and matras_get
 */
static inline void *
matras_view_get_no_check(const struct matras *m, const struct matras_view *v,
			 matras_id_t id)
{
	assert(id < v->block_count);

	/* see "Shifts and masks explanation" for details */
	matras_id_t n1 = id >> m->shift1;
	matras_id_t n2 = (id & m->mask1) >> m->shift2;
	matras_id_t n3 = (id & m->mask2);

	char ***extent = (char ***)v->root;
	return &extent[n1][n2][n3 * m->block_size];
}

/**
 * matras_view_get definition
 */
static inline void *
matras_view_get(const struct matras *m, const struct matras_view *v,
		matras_id_t id)
{
	return matras_view_get_no_check(m, v->next_view ? v : &m->head, id);
}

/**
 * matras_get definition
 */
static inline void *
matras_get(const struct matras *m, matras_id_t id)
{
	return matras_view_get_no_check(m, &m->head, id);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
#endif /* INCLUDES_TARANTOOL_SMALL_MATRAS_H */
