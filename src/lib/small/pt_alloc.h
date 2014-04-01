#pragma once
/*
 * pt_alloc: pointer translation allocator
 * pt_alloc is as an allocator, that provides blocks of specified size (N),
 * and provides an integer incrementally-growning ID for each provided block.
 * Block size must be power of 2 (checked by assert in debug build).
 * pt_alloc can restore pointer to block by it's ID, so
 * one can store such ID instead of storing pointer to block.
 * Since block IDs are generated incrementally from 0 and pt instance stores
 * the number of provided blocks, one can simply iterate all provided blocks.
 * pt_alloc in it's turn allocates extents of memory with given allocator,
 * and strictly with specified size (M).
 * Extent size must be power of 2.
 * No block freeing provided, but destoying pt_alloc instance frees all blocks.
 * There is an impotant compile-time setting - recurse level (L).
 * Actually pt_alloc defined several times, with different L as suffix of "pt"
 * For example pt3_alloc is a pt_alloc with L = 3
 * Briefly, with a given N, M and L (see above) the pt_alloc instance:
 * 1) can provide not more than POW(M / sizeof(void*), L - 1) * (M / N) blocks
 * 2) costs L memory reading for providing new block and restoring block ptr
 * 3) has approximate memory overhead of size (L * M)
 * Of course, ID integer type limit also limits maximum capability of pt_alloc.
 */

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

typedef uint32_t PT_ID_T;
typedef void *(*prov_alloc_func)();
typedef void (*prov_free_func)(void *);

/*
 * pt_alloc struct definition
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

/*
 * Construction
 */
void
pt3_construct(pt3 *p, PT_ID_T extent_size, PT_ID_T block_size,
		prov_alloc_func alloc_func, prov_free_func free_func);
void
pt2_construct(pt2 *p, PT_ID_T extent_size, PT_ID_T block_size,
		prov_alloc_func alloc_func, prov_free_func free_func);

/*
 * Destruction
 */
void
pt3_destroy(pt3 *p);
void
pt2_destroy(pt2 *p);

/*
 * Allocation
 */
void *
pt3_alloc(pt3 *p, PT_ID_T *id);
void *
pt2_alloc(pt2 *p, PT_ID_T *id);

/*
 * Restoration
 */
void *
pt3_get(pt3 *p, PT_ID_T id);
void *
pt2_get(pt2 *p, PT_ID_T id);

/*
  * Getting number of allocated extents (of size extent_size each)
*/
PT_ID_T
pt3_extents_count(pt3 *p);
PT_ID_T
pt2_extents_count(pt2 *p);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
