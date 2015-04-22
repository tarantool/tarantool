#ifndef INCLUDES_TARANTOOL_SMALL_MATRAS_H
#define INCLUDES_TARANTOOL_SMALL_MATRAS_H
/*
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
 * matras - Memory Address TRanslation Allocator (Smile)
 * matras is as an allocator, that provides blocks of specified
 * size (N), and provides an integer incrementally-growing ID for
 * each returned block.
 *
 * The block size (N) must be a power of 2 (checked by assert in
 * a debug build). matras can restore the pointer to block
 * by block ID, so one can store such ID instead of storing
 * pointer to block.
 *
 * Since block IDs grow incrementally from 0 and matras
 * instance stores the number of provided blocks, there is a
 * simple way to iterate over all provided blocks.
 * matras in its turn allocates extents of memory by means of
 * the supplied allocator, each extent having the same size (M).
 * M must be a power of 2.
 * There is no way to free a single block, except block with the
 * maximum ID.
 * Destroying a matras instance frees all allocated extents.
 *
 * There is an important compile-time setting - recursion level
 * (L).
 * Imagine block id is uint32 and recursion level is 3. This
 * means that block id value consists of 3 parts:
 *  - first N1 bits - level 0 extent id - stores the address of
 *  level 1 extent
 *  - second N2 bits - level 1 extent id - stores the address of
 *  the extent which contains actual blocks
 *  - remaining N3 bits - block number in level 2 extent
 * (Actual values of N1 and N2 are a function of block size B).
 * Calculation of N1, N2 and N3 depends on sizes of blocks,
 * extents and sizeof(void *).
 *
 * By this moment, matras is implemented only with L = 3
 *
 * To sum up, with a given N, M and L (see above) the matras
 * instance:
 *
 * 1) can provide not more than
 *    pow(M / sizeof(void*) / 2, L - 1) * (M / N)
 *    blocks
 * 2) costs (L - 1) random memory accesses to provide a new block
 *    or restore a block pointer from block id
 * 3) has an approximate memory overhead of size (L * M)
 *
 * Of course, the integer type used for block id (matras_id_t,
 * usually is a typedef to uint32) also limits the maximum number
 * of objects that can be block_count by an instance of matras.
 *
 * Additionally matras provides freezing all allocated data. At
 * any moment user can call matras_new_version method for achieving
 * unique ID of current data state. Then the user could change,
 * allocate and dealocate main matras data, meanwhile getting data
 * from previusly freezed version. This works through copy-on-write
 * mechanism, thus is cheap enough, and of course this works only
 * if user correctly notifies mastras about chainging data.
 * An impotant property of implemented copy-on-write mechnism - is
 * that main data does not moved, i.e. before data modification the
 * extent is copied to another location to become the extent
 * for freezed version. Thus main block address with some particular
 * block ID is unchanged.
 */
/* }}} */

#ifdef WIN32
typedef unsigned __int32 matras_id_t;
#else
#include <stdint.h>
typedef uint32_t matras_id_t;
#endif

#include <assert.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Type of a block ID.
 */

/**
 * Type of the extent allocator (the allocator for regions
 * of size M). Is allowed to return NULL, but is not allowed
 * to throw an exception
 */
typedef void *(*matras_alloc_func)();
typedef void (*matras_free_func)(void *);

typedef uint32_t matras_version_tag_t;

struct matras_record {
	/* pointer to next level of a tree */
	union {
		struct matras_record *ptr;
		matras_version_tag_t ptr_padded;
	};
	/* version tag - bitmask of all version referencing ptr above */
	union {
		matras_version_tag_t tag;
		void *tag_padded;
	};
};

enum {
	MATRAS_VERSION_COUNT = 8
};

/**
 * matras - memory allocator of blocks of equal
 * size with support of address translation.
 */
struct matras {
	/* Pointer to the root extent of matras */
	struct matras_record roots[MATRAS_VERSION_COUNT];
	/* A number of already allocated blocks */
	matras_id_t block_counts[MATRAS_VERSION_COUNT];
	/* Bit mask of used versions */
	matras_version_tag_t ver_occ_mask;
	/* Block size (N) */
	matras_id_t block_size;
	/* Extent size (M) */
	matras_id_t extent_size;
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
matras_getv(const struct matras *m, matras_id_t id, matras_id_t version);

/*
 * Getting number of allocated extents (of size extent_size each)
*/
matras_id_t
matras_extents_count(const struct matras *m);

/*
 * Create new version of matras memory.
 * Return 0 if all version IDs are occupied.
 */
matras_id_t
matras_new_version(struct matras *m);

/*
 * Delete memory version by specified ID.
 */
void
matras_delete_version(struct matras *m, matras_id_t ver_id);

/*
 * Notify matras that memory at given ID will be changed.
 * Returns true if ok, and false if failed to allocate memory.
 * Only needed (and does any work) if some versions are used.
 */
void *
matras_before_change(struct matras *m, matras_id_t id);

/*
 * Debug check that ensures internal consistency.
 * Must return 0. If i returns not 0, smth is terribly wrong.
 */
matras_version_tag_t
matras_debug_selfcheck(const struct matras *m);

/**
 * matras_get implementation
 */
static inline void *
matras_get(const struct matras *m, matras_id_t id)
{
	assert(id < m->block_counts[0]);

	/* see "Shifts and masks explanation" for details */
	matras_id_t n1 = id >> m->shift1;
	matras_id_t n2 = (id & m->mask1) >> m->shift2;
	matras_id_t n3 = (id & m->mask2);

	char *extent = (char *)m->roots[0].ptr[n1].ptr[n2].ptr;
	return &extent[n3 * m->block_size];
}

/**
 * matras_getv implementation
 */
static inline void *
matras_getv(const struct matras *m, matras_id_t id, matras_id_t version)
{
	assert(id < m->block_counts[version]);

	/* see "Shifts and masks explanation" for details */
	matras_id_t n1 = id >> m->shift1;
	matras_id_t n2 = (id & m->mask1) >> m->shift2;
	matras_id_t n3 = (id & m->mask2);

	char *extent = (char *)m->roots[version].ptr[n1].ptr[n2].ptr;
	return &extent[n3 * m->block_size];
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
#endif /* INCLUDES_TARANTOOL_SMALL_MATRAS_H */
