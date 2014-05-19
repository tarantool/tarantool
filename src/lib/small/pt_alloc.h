#ifndef INCLUDES_TARANTOOL_SMALL_PTALLOC_H
#define INCLUDES_TARANTOOL_SMALL_PTALLOC_H
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
/*
 * pt_alloc: pointer translation allocator
 * pt_alloc is as an allocator, that provides blocks of specified
 * size (N), and provides an integer incrementally-growing ID for
 * each returned block.
 *
 * The block size must be a power of 2 (checked by assert in
 * a debug build). pt_alloc can restore the pointer to block
 * by block ID, so one can store such ID instead of storing
 * pointer to block.
 *
 * Since block IDs grow incrementally from 0 and pt_alloc
 * instance stores the number of provided blocks, there is a
 * simple way to iterate over all provided blocks.
 * pt_alloc in its turn allocates extents of memory by means of
 * the supplied allocator, each extent having the same size (M).
 * M must be a power of 2.
 * There is no way to free a single block, but destroying a
 * pt_alloc instance frees all allocated extents.
 *
 * There is an important compile-time setting - recursion level
 * (L).
 * Imagine block id is uint32 and recursion level is 3. This
 * means that block id value consists of 3 parts:
 *  - first N1 bits - level 0 extent id - stores the address of
 *  level 1 extent
 *  - second N2 bits - level 1 extent id - stores the address of
 *  the extent which contains actual blocks
 *  - remaining bits - block number in level 1 extent
 * (Actual values of N1 and N2 are a function of block size B).
 *
 * Actually, pt_alloc is re-defined twice, with different
 * recursion levels as suffixes of "pt" function names:
 * pt3_alloc is a pt_alloc with L = 3
 * pt2_alloc is a pt_alloc with L = 2
 *
 * To sum up, with a given N, M and L (see above) the pt_alloc
 * instance:
 *
 * 1) can provide not more than
 *    pow(M / sizeof(void*), L - 1) * (M / N)
 *    blocks
 * 2) costs L random memory accesses to provide a new block or
 *    restore a block pointer from block id
 * 3) has an approximate memory overhead of size (L * M)
 *
 * Of course, the integer type used for block id (PT_ID_T, usually
 * is a typedef to uint32) also limits the maximum number of
 * objects that can be created by an instance of pt_alloc.
 */

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

typedef uint32_t PT_ID_T;
/*
 * Type of the extent allocator (the allocator
 * for blocks of size M).
 */
typedef void *(*prov_alloc_func)();
typedef void (*prov_free_func)(void *);

/*
 * pt_alloc - memory allocator of blocks of equal
 * size with support of address translation.
 */
typedef struct tag_pt3 {
	void *extent;
	PT_ID_T created;
	PT_ID_T extent_size;
	PT_ID_T block_size;
	PT_ID_T log2_capacity;
	PT_ID_T shift1, shift2;
	PT_ID_T mask1, mask2;
	prov_alloc_func alloc_func;
	prov_free_func free_func;
} pt3;

typedef struct tag_pt2 {
	void *extent;
	PT_ID_T created;
	PT_ID_T extent_size;
	PT_ID_T block_size;
	PT_ID_T log2_capacity;
	PT_ID_T shift;
	PT_ID_T mask;
	prov_alloc_func alloc_func;
	prov_free_func free_func;
} pt2;

/*
 * pt_alloc API declaration
 */

/**
 * Initialize an empty instantce of pointer translating
 * block allocator. Does not allocate memory.
 */
void
pt3_construct(pt3 *p, PT_ID_T extent_size, PT_ID_T block_size,
	      prov_alloc_func alloc_func, prov_free_func free_func);
void
pt2_construct(pt2 *p, PT_ID_T extent_size, PT_ID_T block_size,
	      prov_alloc_func alloc_func, prov_free_func free_func);

/**
 * Free all memory used by an instance of pt_alloc.
 */
void
pt3_destroy(pt3 *p);
void
pt2_destroy(pt2 *p);

/**
 * Allocate a new block. Return both, block pointer and block
 * id.
 *
 * @retval NULL failed to allocate memory
 */
void *
pt3_alloc(pt3 *p, PT_ID_T *id);
void *
pt2_alloc(pt2 *p, PT_ID_T *id);

/**
 * Convert block id into block address.
 */
void *
pt3_get(const pt3 *p, PT_ID_T id);
void *
pt2_get(const pt2 *p, PT_ID_T id);

/*
  * Getting number of allocated extents (of size extent_size each)
*/
PT_ID_T
pt3_extents_count(const pt3 *p);
PT_ID_T
pt2_extents_count(const pt2 *p);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
#endif /* INCLUDES_TARANTOOL_SMALL_PTALLOC_H */
