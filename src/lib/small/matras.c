/*
 * pt_alloc implementation
 */

#include "matras.h"
#include <limits.h>

/*
 * Binary logarithm of value (exact if the value is a power of 2,
 * approximate (floored) otherwise)
 */
static matras_id_t
pt_log2(matras_id_t val)
{
	assert(val > 0);
	return sizeof(unsigned long) * CHAR_BIT -
		__builtin_clzl((unsigned long) val) - 1;
}

/**
 * Initialize an empty instance of pointer translating
 * block allocator. Does not allocate memory.
 */
void
matras_create(struct matras *m, matras_id_t extent_size, matras_id_t block_size,
	      prov_alloc_func alloc_func, prov_free_func free_func)
{
	/*extent_size must be power of 2 */
	assert((extent_size & (extent_size - 1)) == 0);
	/*block_size must be power of 2 */
	assert((block_size & (block_size - 1)) == 0);

	m->extent = 0;
	m->block_count = 0;
	m->extent_size = extent_size;
	m->block_size = block_size;

	matras_id_t log1 = pt_log2(extent_size);
	matras_id_t log2 = pt_log2(block_size);
	matras_id_t log3 = pt_log2(sizeof(void *));
	m->log2_capacity = log1 * 3 - log2 - log3 * 2;
	m->shift1 = log1 * 2 - log2 - log3;
	m->shift2 = log1 - log2;

	m->mask1 = (((matras_id_t)1) << m->shift1) - ((matras_id_t)1);
	m->mask2 = (((matras_id_t)1) << m->shift2) - ((matras_id_t)1);

	m->alloc_func = alloc_func;
	m->free_func = free_func;
}

/**
 * Free all memory used by an instance of matras.
 */
void
matras_destroy(struct matras *m)
{
	if (m->block_count) {
		void* extent1 = m->extent;
		matras_id_t id = m->block_count;

		matras_id_t n1 = id >> m->shift1;
		id &= m->mask1;

		/* free not fully loaded extents */
		if (id) {
			matras_id_t n2 = id >> m->shift2;
			id &= m->mask2;
			if (id)
				n2++;

			void *extent2 = ((void **)extent1)[n1];
			for (matras_id_t j = 0; j < n2; j++) {
				void *extent3 = ((void **)extent2)[j];
				m->free_func(extent3);
			}
			m->free_func(extent2);
		}

		/* free fully loaded extents */
		matras_id_t n2 = m->extent_size / sizeof(void *);
		for (matras_id_t i = 0; i < n1; i++) {
			void *extent2 = ((void **)extent1)[i];
			for (matras_id_t j = 0; j < n2; j++) {
				void *extent3 = ((void **)extent2)[j];
				m->free_func(extent3);
			}
			m->free_func(extent2);
		}

		m->free_func(extent1);
		m->block_count = 0;
	}
#ifndef __OPTIMIZE__
	m->extent = (void *)0xDEADBEEF;
#endif
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
	if (m->block_count)
		assert(pt_log2(m->block_count) < m->log2_capacity);
	/* Current block_count is the ID of new block */

	/* See "Shifts and masks explanation" for details */
	/* Additionally we determine if we must allocate extents.
	 * Basically,
	 * if n1 == 0 && n2 == 0 && n3 == 0, we must allocate root extent,
	 * if n2 == 0 && n3 == 0, we must allocate second level extent,
	 * if n3 == 0, we must allocate third level extent.
	 * Optimization:
	 * (n1 == 0 && n2 == 0 && n3 == 0) is identical to (id == 0)
	 * (n2 == 0 && n3 == 0) is identical to (id ^ mask1 == 0)
	 */
	matras_id_t id = m->block_count;
	matras_id_t extent1_not_empty = id;
	matras_id_t n1 = id >> m->shift1;
	id &= m->mask1;
	matras_id_t extent2_not_empty = id;
	matras_id_t n2 = id >> m->shift2;
	id &= m->mask2;
	matras_id_t extent3_not_empty = id;
	matras_id_t n3 = id;

	void *extent1, *extent2, *extent3;

	if (extent1_not_empty) {
		extent1 = m->extent;
	} else {
		extent1 = m->alloc_func();
		if (!extent1)
			return 0;
		m->extent = extent1;
	}

	if (extent2_not_empty) {
		extent2 = ((void **)extent1)[n1];
	} else {
		extent2 = m->alloc_func();
		if (!extent2) {
			if (!extent1_not_empty) /* means - was empty */
				m->free_func(extent1);
			return 0;
		}
		((void **)extent1)[n1] = extent2;
	}

	if (extent3_not_empty) {
		extent3 = ((void **)extent2)[n2];
	} else {
		extent3 = m->alloc_func();
		if (!extent3) {
			if (!extent1_not_empty) /* means - was empty */
				m->free_func(extent1);
			if (!extent2_not_empty) /* means - was empty */
				m->free_func(extent2);
			return 0;
		}
		((void **)extent2)[n2] = extent3;
	}

	*result_id = m->block_count++;
	return (void*)((char*)extent3 + n3 * m->block_size);
}

/*
 * Deallocate last block (block with maximum ID)
 */
void
matras_dealloc(struct matras *m)
{
	assert(m->block_count);
	m->block_count--;
	/* Current block_count is the ID of deleting block */

	/* See "Shifts and masks explanation" for details */
	/* Deleting extents in same way (but reverse order) like in matras_alloc
	 * See matras_alloc for details. */
	matras_id_t id = m->block_count;
	matras_id_t extent1_free = !id;
	matras_id_t n1 = id >> m->shift1;
	id &= m->mask1;
	matras_id_t extent2_free = !id;
	matras_id_t n2 = id >> m->shift2;
	id &= m->mask2;
	matras_id_t extent3_free = !id;

	if (extent1_free || extent2_free || extent3_free) {
		void *extent1, *extent2, *extent3;
		extent1 = m->extent;
		extent2 = ((void **)extent1)[n1];
		extent3 = ((void **)extent2)[n2];
		if (extent3_free)
			m->free_func(extent3);
		if (extent2_free)
			m->free_func(extent2);
		if (extent1_free)
			m->free_func(extent1);
		return;
	}
}

/**
 * Return the number of allocated extents (of size m->extent_size each)
 */
matras_id_t
matras_extents_count(const struct matras *m)
{
	/* matras stores data in a 3-level tree of extents.
	 * Let's calculate extents count level by level, starting from leafs
	 * Last level of the tree consists of extents that stores blocks,
	 * so we can calculate number of extents by block count: */
	matras_id_t c = (m->block_count + (m->extent_size / m->block_size - 1))
		/ (m->extent_size / m->block_size);
	matras_id_t res = c;

	/* two upper levels consist of extents that stores pointers to extents,
	 * so we can calculate number of extents by lower level extent count:*/
	for (matras_id_t i = 0; i < 2; i++) {
		c = (c + (m->extent_size / sizeof(void *) - 1))
			/ (m->extent_size / sizeof(void *));
		res += c;
	}

	return res;
}
