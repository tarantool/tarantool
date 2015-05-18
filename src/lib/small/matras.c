/*
 * matras implementation
 */

#include "matras.h"
#include <limits.h>
#include <string.h>
#include <stdbool.h>

#ifdef WIN32
#include <intrin.h>
#pragma intrinsic (_BitScanReverse)
#endif

/*
 * Binary logarithm of value (exact if the value is a power of 2,
 * approximate (floored) otherwise)
 */
static matras_id_t
matras_log2(matras_id_t val)
{
	assert(val > 0);
#ifdef WIN32
	unsigned long res = 0;
	unsigned char nonzero = _BitScanReverse(&res, val);
	assert(nonzero); (void)nonzero;
	return (matras_id_t)res;
#else
	return sizeof(unsigned int) * CHAR_BIT -
		__builtin_clz((unsigned int) val) - 1;
#endif
}

/**
 * Initialize an empty instance of pointer translating
 * block allocator. Does not allocate memory.
 */
void
matras_create(struct matras *m, matras_id_t extent_size, matras_id_t block_size,
	      matras_alloc_func alloc_func, matras_free_func free_func)
{
	/*extent_size must be power of 2 */
	assert((extent_size & (extent_size - 1)) == 0);
	/*block_size must be power of 2 */
	assert((block_size & (block_size - 1)) == 0);
	/*block must be not greater than the extent*/
	assert(block_size <= extent_size);
	/*extent must be able to store at least two records*/
	assert(extent_size > sizeof(void *));

	m->head.block_count = 0;
	m->head.prev_view = 0;
	m->head.next_view = 0;
	m->block_size = block_size;
	m->extent_size = extent_size;
	m->extent_count = 0;
	m->alloc_func = alloc_func;
	m->free_func = free_func;

	matras_id_t log1 = matras_log2(extent_size);
	matras_id_t log2 = matras_log2(block_size);
	matras_id_t log3 = matras_log2(sizeof(void *));
	m->log2_capacity = log1 * 3 - log2 - log3 * 2;
	m->shift1 = log1 * 2 - log2 - log3;
	m->shift2 = log1 - log2;
	m->mask1 = (((matras_id_t)1) << m->shift1) - ((matras_id_t)1);
	m->mask2 = (((matras_id_t)1) << m->shift2) - ((matras_id_t)1);
}


/**
 * Free all memory used by an instance of matras and
 * reinitialize it.
 * Identical to matras_destroy(m); matras_create(m, ...);
 */
void
matras_reset(struct matras *m)
{
	matras_destroy(m);
	m->head.block_count = 0;
}

/**
 * Helper functions for allocating new extent and incrementing extent counter
 */
static inline void *
matras_alloc_extent(struct matras *m)
{
	void *ext = m->alloc_func();
	if (ext)
		m->extent_count++;
	return ext;
}

/**
 * Helper functions for allocating new extent and incrementing extent counter
 */
static inline void
matras_free_extent(struct matras *m, void *ext)
{
	m->free_func(ext);
	m->extent_count--;
}

/**
 * Free all memory used by an instance of matras.
 */
void
matras_destroy(struct matras *m)
{
	while (m->head.prev_view)
		matras_destroy_read_view(m, m->head.prev_view);
	if (m->head.block_count == 0)
		return;

	matras_id_t step1 = m->mask1 + 1;
	matras_id_t step2 = m->mask2 + 1;
	matras_id_t i1 = 0, j1 = 0, i2, j2;
	matras_id_t ptrs_in_ext = m->extent_size / (matras_id_t)sizeof(void *);
	struct matras_view *v = &m->head;
	void **extent1 = (void **)v->root;
	for (; j1 < v->block_count; i1++, j1 += step1) {
		void **extent2 = (void **)extent1[i1];
		for (i2 = j2 = 0;
		     i2 < ptrs_in_ext && j1 + j2 < v->block_count;
		     i2++, j2 += step2) {
			void **extent3 = (void **)extent2[i2];
			matras_free_extent(m, extent3);
		}
		matras_free_extent(m, extent2);
	}
	matras_free_extent(m, extent1);

	assert(m->extent_count == 0);
}



/**
 * Allocate a new block. Return both, block pointer and block
 * id.
 *
 * @retval NULL failed to allocate memory
 */
void *
matras_alloc(struct matras *m, matras_id_t *result_id)
{
	assert(m->head.block_count == 0 ||
		matras_log2(m->head.block_count) < m->log2_capacity);

	/* Current block_count is the ID of new block */
	matras_id_t id = m->head.block_count;

	/* See "Shifts and masks explanation" for details */
	/* Additionally we determine if we must allocate extents.
	 * Basically,
	 * if n1 == 0 && n2 == 0 && n3 == 0, we must allocate root extent,
	 * if n2 == 0 && n3 == 0, we must allocate second level extent,
	 * if n3 == 0, we must allocate third level extent.
	 * Optimization:
	 * (n1 == 0 && n2 == 0 && n3 == 0) is identical to (id == 0)
	 * (n2 == 0 && n3 == 0) is identical to (id & mask1 == 0)
	 */
	matras_id_t extent1_available = id;
	matras_id_t n1 = id >> m->shift1;
	id &= m->mask1;
	matras_id_t extent2_available = id;
	matras_id_t n2 = id >> m->shift2;
	id &= m->mask2;
	matras_id_t extent3_available = id;
	matras_id_t n3 = id;

	void **extent1, **extent2;
	char *extent3;

	if (extent1_available) {
		extent1 = (void **)m->head.root;
	} else {
		extent1 = (void **)matras_alloc_extent(m);
		if (!extent1)
			return 0;
		m->head.root = (void *)extent1;
	}

	if (extent2_available) {
		extent2 = (void **)extent1[n1];
	} else {
		extent2 = (void **)matras_alloc_extent(m);
		if (!extent2) {
			if (!extent1_available) /* was created */
				matras_free_extent(m, extent1);
			return 0;
		}
		extent1[n1] = (void *)extent2;
	}

	if (extent3_available) {
		extent3 = (char *)extent2[n2];
	} else {
		extent3 = (char *)matras_alloc_extent(m);
		if (!extent3) {
			if (!extent1_available) /* was created */
				matras_free_extent(m, extent1);
			if (!extent2_available) /* was created */
				matras_free_extent(m, extent2);
			return 0;
		}
		extent2[n2] = (void *)extent3;
	}

	*result_id = m->head.block_count++;
	return (void *)(extent3 + n3 * m->block_size);
}

/*
 * Deallocate last block (block with maximum ID)
 */
void
matras_dealloc(struct matras *m)
{
	assert(m->head.block_count);
	matras_id_t id = m->head.block_count - 1;
	matras_touch(m, id);
	m->head.block_count = id;
	/* Current block_count is the ID of deleting block */

	/* See "Shifts and masks explanation" for details */
	/* Deleting extents in same way (but reverse order) like in matras_alloc
	 * See matras_alloc for details. */
	bool extent1_free = !id;
	matras_id_t n1 = id >> m->shift1;
	id &= m->mask1;
	bool extent2_free = !id;
	matras_id_t n2 = id >> m->shift2;
	id &= m->mask2;
	bool extent3_free = !id;

	if (extent1_free || extent2_free || extent3_free) {
		void **extent1, **extent2, *extent3;
		extent1 = (void **)m->head.root;
		extent2 = (void **)extent1[n1];
		extent3 = extent2[n2];
		if (extent3_free)
			matras_free_extent(m, extent3);
		if (extent2_free)
			matras_free_extent(m, extent2);
		if (extent1_free)
			matras_free_extent(m, extent1);
	}
}

/**
 * Allocate a range_count of blocks. Return both, first block pointer
 * and first block id. This method only works if current number of blocks and
 * number of blocks in one extent are divisible by range_count.
 * range_count must also be less or equal to number of blocks in one extent.
 *
 * @retval NULL failed to allocate memory
 */
void *
matras_alloc_range(struct matras *m, matras_id_t *id, matras_id_t range_count)
{
	assert(m->head.block_count % range_count == 0);
	assert(m->extent_size / m->block_size % range_count == 0);
	void *res = matras_alloc(m, id);
	if (res)
		m->head.block_count += (range_count - 1);
	return res;
}

/*
 * Deallocate last range_count of blocks (blocks with maximum ID)
 * This method only works if current number of blocks and
 * number of blocks in one extent are divisible by range_count.
 * range_count must also be less or equal to number of blocks in one extent.
 */
void
matras_dealloc_range(struct matras *m, matras_id_t range_count)
{
	assert(m->head.block_count % range_count == 0);
	assert(m->extent_size / m->block_size % range_count == 0);
	m->head.block_count -= (range_count - 1);
	matras_dealloc(m);
}

/**
 * Return the number of allocated extents (of size m->extent_size each)
 */
matras_id_t
matras_extent_count(const struct matras *m)
{
	return m->extent_count;
}

/*
 * Create new read view.
 */
void
matras_create_read_view(struct matras *m, struct matras_view *v)
{
	*v = m->head;
	v->next_view = &m->head;
	m->head.prev_view = v;
	if (v->prev_view)
		v->prev_view->next_view = v;
}

/*
 * Delete a read view.
 */
void
matras_destroy_read_view(struct matras *m, struct matras_view *v)
{
	assert(v != &m->head);
	if (!v->next_view)
		return;
	struct matras_view *next_view = v->next_view;
	struct matras_view *prev_view = v->prev_view;
	next_view->prev_view = prev_view;
	if (prev_view)
		prev_view->next_view = next_view;
	v->next_view = 0;

	if (v->block_count == 0)
		return;
	if (v->root == next_view->root && next_view->block_count)
		return;
	if (prev_view && v->root == prev_view->root && prev_view->block_count)
		return;
	void **extent1 = (void **)v->root;
	void **extent1n = (void **) next_view->root;
	void **extent1p = 0;
	if (prev_view)
		extent1p = (void **) prev_view->root;
	matras_id_t step1 = m->mask1 + 1;
	matras_id_t step2 = m->mask2 + 1;
	matras_id_t i1 = 0, j1 = 0, i2, j2;
	matras_id_t ptrs_in_ext = m->extent_size / (matras_id_t)sizeof(void *);
	for (; j1 < v->block_count; i1++, j1 += step1) {
		void **extent2 = (void **)extent1[i1];
		void **extent2n = 0;
		void **extent2p = 0;
		if (next_view->block_count > j1) {
			if (extent1[i1] == extent1n[i1])
				continue;
			extent2n = (void **) extent1n[i1];
		}
		if (prev_view && prev_view->block_count > j1) {
			if (extent1[i1] == extent1p[i1])
				continue;
			extent2p = (void **) extent1p[i1];
		}
		for (i2 = j2 = 0;
		     i2 < ptrs_in_ext && j1 + j2 < v->block_count;
		     i2++, j2 += step2) {
			void **extent3 = (void **)extent2[i2];
			if (next_view->block_count > j1 + j2) {
				if (extent2[i2] == extent2n[i2])
					continue;
			}
			if (prev_view && prev_view->block_count > j1 + j2) {
				if (extent2[i2] == extent2p[i2])
					continue;
			}
			matras_free_extent(m, extent3);
		}
		matras_free_extent(m, extent2);
	}
	matras_free_extent(m, extent1);
}

/*
 * Notify matras that memory at given ID will be changed.
 * Returns (perhaps new) address of memory associated with that block.
 * Returns NULL on memory error
 * Only needed (and does any work) if some versions are used.
 */
void *
matras_touch(struct matras *m, matras_id_t id)
{
	assert(id < m->head.block_count);

	if (!m->head.prev_view)
		return matras_get(m, id);

	if (m->head.prev_view->block_count) {
		matras_id_t extent_id = id >> m->shift2;
		matras_id_t next_last_id = m->head.prev_view->block_count - 1;
		matras_id_t next_last_extent_id = next_last_id >> m->shift2;
		if (extent_id > next_last_extent_id)
			return matras_get(m, id);
	}

	/* see "Shifts and masks explanation" for details */
	matras_id_t n1 = id >> m->shift1;
	matras_id_t n2 = (id & m->mask1) >> m->shift2;
	matras_id_t n3 = id & m->mask2;

	void **extent1 = (void **)m->head.root;
	void **extent1p = (void **)m->head.prev_view->root;
	if (extent1 == extent1p) {
		void *new_extent = matras_alloc_extent(m);
		if (!new_extent)
			return 0;
		memcpy(new_extent, extent1, m->extent_size);
		m->head.root = new_extent;
		extent1 = (void **)new_extent;
	}

	void **extent2 = (void **)extent1[n1];
	void **extent2p = (void **) extent1p[n1];
	if (extent2 == extent2p) {
		void *new_extent = matras_alloc_extent(m);
		if (!new_extent)
			return 0;
		memcpy(new_extent, extent2, m->extent_size);
		extent1[n1] = new_extent;
		extent2 = (void **)new_extent;
	}

	char *extent3 = (char *)extent2[n2];
	char *extent3p = (char *) extent2p[n2];
	if (extent3 == extent3p) {
		void *new_extent = matras_alloc_extent(m);
		if (!new_extent)
			return 0;
		memcpy(new_extent, extent3, m->extent_size);
		extent2[n2] = new_extent;
		extent3 = (char *)new_extent;
	}

	return &extent3[n3 * m->block_size];
}
