/*
 * pt_alloc implementation
 */

#include "matras.h"
#include <limits.h>
#include <string.h>
#ifdef WIN32
#include <intrin.h>
#pragma intrinsic (_BitScanReverse)
#ifndef _DEBUG
#define __OPTIMIZE__ 1
#endif
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
	/*stupid check*/
	assert(sizeof(void *) == sizeof(uintptr_t));

	m->block_counts[0] = 0;
	m->extent_size = extent_size;
	m->block_size = block_size;

	m->ver_occ_mask = 1;

	matras_id_t log1 = matras_log2(extent_size);
	matras_id_t log2 = matras_log2(block_size);
	matras_id_t log3 = matras_log2(sizeof(void *));
	m->log2_capacity = log1 * 3 - log2 - log3 * 2;
	m->shift1 = log1 * 2 - log2 - log3;
	m->shift2 = log1 - log2;

	m->mask1 = (((matras_id_t)1) << m->shift1) - ((matras_id_t)1);
	m->mask2 = (((matras_id_t)1) << m->shift2) - ((matras_id_t)1);

	m->alloc_func = alloc_func;
	m->free_func = free_func;
}

static inline uintptr_t *
matras_ptr(uintptr_t ptrver)
{
	return (uintptr_t *)(ptrver & matras_ptr_mask);
}

static inline uintptr_t
matras_ver(uintptr_t ptrver)
{
	return ptrver & matras_ver_mask;
}

/**
 * Free all memory used by an instance of matras.
 */
void
matras_destroy(struct matras *m)
{
	while(m->ver_occ_mask != 1) {
		matras_id_t ver = __builtin_ctzl(m->ver_occ_mask ^ 1);
		matras_destroy_read_view(m, ver);
	}
	if (m->block_counts[0]) {
		uintptr_t *extent1 = matras_ptr(m->roots[0]);
		matras_id_t id = m->block_counts[0];
		matras_id_t i, j;

		matras_id_t n1 = id >> m->shift1;
		id &= m->mask1;

		/* free not fully loaded extents */
		if (id) {
			matras_id_t n2 = id >> m->shift2;
			id &= m->mask2;
			if (id)
				n2++;

			uintptr_t *extent2 = matras_ptr(extent1[n1]);
			for (j = 0; j < n2; j++) {
				uintptr_t *extent3 = matras_ptr(extent2[j]);
				m->free_func(extent3);
			}
			m->free_func(extent2);
		}

		/* free fully loaded extents */
		matras_id_t n2 = m->extent_size / sizeof(void *);
		for ( i = 0; i < n1; i++) {
			uintptr_t *extent2 = matras_ptr(extent1[i]);
			for (j = 0; j < n2; j++) {
				uintptr_t *extent3 = matras_ptr(extent2[j]);
				m->free_func(extent3);
			}
			m->free_func(extent2);
		}

		m->free_func(extent1);
		m->block_counts[0] = 0;
	}
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
	m->block_counts[0] = 0;
	m->ver_occ_mask = 1;
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
	if (m->block_counts[0])
		assert(matras_log2(m->block_counts[0]) < m->log2_capacity);
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
	matras_id_t id = m->block_counts[0];
	matras_id_t extent1_not_empty = id;
	matras_id_t n1 = id >> m->shift1;
	id &= m->mask1;
	matras_id_t extent2_not_empty = id;
	matras_id_t n2 = id >> m->shift2;
	id &= m->mask2;
	matras_id_t extent3_not_empty = id;
	matras_id_t n3 = id;

	uintptr_t *extent1, *extent2, *extent3;
	uintptr_t new_tag = (~(m->ver_occ_mask ^ 1)) & matras_ver_mask;

	if (extent1_not_empty) {
		extent1 = matras_ptr(m->roots[0]);
	} else {
		extent1 = (uintptr_t *)m->alloc_func();
		assert(((uintptr_t)extent1 & matras_ver_mask) == 0);
		if (!extent1)
			return 0;
		m->roots[0] = (uintptr_t)extent1 | new_tag;
	}

	if (extent2_not_empty) {
		extent2 = matras_ptr(extent1[n1]);
	} else {
		extent2 = (uintptr_t *)m->alloc_func();
		assert(((uintptr_t)extent2 & matras_ver_mask) == 0);
		if (!extent2) {
			if (!extent1_not_empty) /* means - was empty */
				m->free_func(extent1);
			return 0;
		}
		extent1[n1] = (uintptr_t)extent2 | new_tag;
	}

	if (extent3_not_empty) {
		extent3 = matras_ptr(extent2[n2]);
	} else {
		extent3 = (uintptr_t *)m->alloc_func();
		assert(((uintptr_t)extent3 & matras_ver_mask) == 0);
		if (!extent3) {
			if (!extent1_not_empty) /* means - was empty */
				m->free_func(extent1);
			if (!extent2_not_empty) /* means - was empty */
				m->free_func(extent2);
			return 0;
		}
		extent2[n2] = (uintptr_t)extent3 | new_tag;
	}

	*result_id = m->block_counts[0]++;
	return (void *)((char*)extent3 + n3 * m->block_size);
}

/*
 * Deallocate last block (block with maximum ID)
 */
void
matras_dealloc(struct matras *m)
{
	assert(m->block_counts[0]);
	matras_id_t last = m->block_counts[0] - 1;
	matras_touch(m, last);
	m->block_counts[0] = last;
	/* Current block_count is the ID of deleting block */

	/* See "Shifts and masks explanation" for details */
	/* Deleting extents in same way (but reverse order) like in matras_alloc
	 * See matras_alloc for details. */
	matras_id_t id = m->block_counts[0];
	matras_id_t extent1_free = !id;
	matras_id_t n1 = id >> m->shift1;
	id &= m->mask1;
	matras_id_t extent2_free = !id;
	matras_id_t n2 = id >> m->shift2;
	id &= m->mask2;
	matras_id_t extent3_free = !id;

	if (extent1_free || extent2_free || extent3_free) {
		uintptr_t *extent1, *extent2, *extent3;
		extent1 = matras_ptr(m->roots[0]);
		extent2 = matras_ptr(extent1[n1]);
		extent3 = matras_ptr(extent2[n2]);
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
	assert(m->block_counts[0] % range_count == 0);
	assert(m->extent_size / m->block_size % range_count == 0);
	void *res = matras_alloc(m, id);
	if (res)
		m->block_counts[0] += (range_count - 1);
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
	assert(m->block_counts[0] % range_count == 0);
	assert(m->extent_size / m->block_size % range_count == 0);
	m->block_counts[0] -= (range_count - 1);
	matras_dealloc(m);
}

/**
 * Return the number of allocated extents (of size m->extent_size each)
 */
matras_id_t
matras_extent_count(const struct matras *m)
{
	/* matras stores data in a 3-level tree of extents.
	 * Let's calculate extents count level by level, starting from leafs
	 * Last level of the tree consists of extents that stores blocks,
	 * so we can calculate number of extents by block count: */
	matras_id_t c = (m->block_counts[0] + (m->extent_size / m->block_size - 1))
		/ (m->extent_size / m->block_size);
	matras_id_t res = c;

	/* two upper levels consist of extents that stores pointers to extents,
	 * so we can calculate number of extents by lower level extent count:*/
	matras_id_t i;
	for (i = 0; i < 2; i++) {
		c = (c + (m->extent_size / sizeof(void *) - 1))
			/ (m->extent_size / sizeof(void *));
		res += c;
	}

	return res;
}

/*
 * Create new version of matras memory.
 * Return 0 if all version IDs are occupied.
 */
matras_id_t
matras_create_read_view(struct matras *m)
{
	matras_id_t ver_id;
#ifdef WIN32
	unsigned long res = 0;
	unsigned char nonzero = _BitScanForward(&res, m->ver_occ_mask);
	assert(nonzero); (void)nonzero;
	ver_id = (matras_id_t) res;
#else
	ver_id = (matras_id_t)
		__builtin_ctzl(~m->ver_occ_mask);
#endif
	assert(ver_id > 0);
	if (ver_id >= MATRAS_VERSION_COUNT)
		return 0;
	m->ver_occ_mask |= ((uintptr_t)1) << ver_id;
	m->roots[ver_id] = m->roots[0];
	m->block_counts[ver_id] = m->block_counts[0];
	return ver_id;
}

/*
 * Delete memory version by specified ID.
 */
void
matras_destroy_read_view(struct matras *m, matras_id_t ver_id)
{
	assert(ver_id);
	uintptr_t me = ((uintptr_t)1) << ver_id;
	assert(m->ver_occ_mask & me);
	if (m->block_counts[ver_id]) {
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[ver_id]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			uintptr_t *extent1, *extent2;
			uintptr_t owners;
			extent1 = matras_ptr(m->roots[ver_id]);
			extent2 = matras_ptr(extent1[n1]);
			owners = matras_ver(extent2[n2]) & m->ver_occ_mask;
			assert(owners & me);
			if (owners == me) {
				m->free_func(matras_ptr(extent2[n2]));
			} else {
				uintptr_t run = owners;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((uintptr_t)1) << oth_ver;
					uintptr_t *ver_extent1, *ver_extent2;
					ver_extent1 = matras_ptr(m->roots[oth_ver]);
					ver_extent2 = matras_ptr(ver_extent1[n1]);
					assert((matras_ver(ver_extent2[n2]) &
						m->ver_occ_mask & ~me) == (owners & ~me));
					assert(matras_ptr(ver_extent2[n2]) ==
						matras_ptr(extent2[n2]));
					ver_extent2[n2] &= ~me;
				} while (run);
			}
		}
		step = m->mask1 + 1;
		for (j = 0; j < m->block_counts[ver_id]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			uintptr_t *extent1;
			extent1 = matras_ptr(m->roots[ver_id]);
			uintptr_t owners;
			owners = matras_ver(extent1[n1]) & m->ver_occ_mask;
			assert(owners & me);
			if (owners == me) {
				m->free_func(matras_ptr(extent1[n1]));
			} else {
				uintptr_t run = owners;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((uintptr_t)1) << oth_ver;
					uintptr_t *ver_extent1;
					ver_extent1 = matras_ptr(m->roots[oth_ver]);
					assert((matras_ver(ver_extent1[n1]) &
						m->ver_occ_mask & ~me) == (owners & ~me));
					assert(matras_ptr(ver_extent1[n1]) ==
						matras_ptr(extent1[n1]));
					ver_extent1[n1] &= ~me;
				} while (run);
			}
		}
		uintptr_t owners;
		owners = matras_ver(m->roots[ver_id]) & m->ver_occ_mask;
		assert(owners & me);
		if (owners == me) {
			m->free_func(matras_ptr(m->roots[ver_id]));
		} else {
			uintptr_t run = owners;
			do {
				uint32_t oth_ver = __builtin_ctzl(run);
				run ^= ((uintptr_t)1) << oth_ver;
				assert((matras_ver(m->roots[oth_ver]) &
					m->ver_occ_mask & ~me) == (owners & ~me));
				assert(matras_ptr(m->roots[oth_ver]) ==
					matras_ptr(m->roots[ver_id]));
				m->roots[oth_ver] &= ~me;
			} while (run);
		}
		m->block_counts[ver_id] = 0;
	}
	if (m->block_counts[0]) {
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[0]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			uintptr_t *extent1, *extent2;
			extent1 = matras_ptr(m->roots[0]);
			extent2 = matras_ptr(extent1[n1]);
			uintptr_t run =
				matras_ver(extent2[n2]) &
				m->ver_occ_mask;
			do {
				uint32_t oth_ver = __builtin_ctzl(run);
				run ^= ((uintptr_t)1) << oth_ver;
				uintptr_t *ver_extent1, *ver_extent2;
				ver_extent1 = matras_ptr(m->roots[oth_ver]);
				ver_extent2 = matras_ptr(ver_extent1[n1]);
				ver_extent2[n2] |= me;
			} while (run);
		}
		step = m->mask1 + 1;
		for (j = 0; j < m->block_counts[0]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			uintptr_t *extent1;
			extent1 = matras_ptr(m->roots[0]);
			uintptr_t run =
				matras_ver(extent1[n1]) & m->ver_occ_mask;
			do {
				uint32_t oth_ver = __builtin_ctzl(run);
				run ^= ((uintptr_t)1) << oth_ver;
				uintptr_t *ver_extent1;
				ver_extent1 = matras_ptr(m->roots[oth_ver]);
				ver_extent1[n1] |= me;
			} while (run);
		}
		uintptr_t run =
			m->roots[0] & m->ver_occ_mask;
		do {
			uint32_t oth_ver = __builtin_ctzl(run);
			run ^= ((uintptr_t)1) << oth_ver;
			m->roots[oth_ver] |= me;
		} while (run);
	}
	m->ver_occ_mask ^= me;
}

/*
 * Notify matras that memory at given ID will be changed.
 * Returns true if ok, and false if failed to allocate memory.
 */
void *
matras_touch(struct matras *m, matras_id_t id)
{
	assert(id < m->block_counts[0]);

	/* see "Shifts and masks explanation" for details */
	matras_id_t n1 = id >> m->shift1;
	matras_id_t n2 = (id & m->mask1) >> m->shift2;
	matras_id_t n3 = id & m->mask2;

	uintptr_t *l1 = &m->roots[0];
	uintptr_t *l2 = &matras_ptr(*l1)[n1];
	uintptr_t *l3 = &matras_ptr(*l2)[n2];
	uintptr_t owner_mask3 = matras_ver(*l3) & m->ver_occ_mask;
	assert(owner_mask3 & 1);
	if (owner_mask3 == 1)
		return &((char *)matras_ptr(*l3))[n3 * m->block_size]; /* private page */

	uintptr_t *new_extent3 =
		(uintptr_t *)m->alloc_func();
	assert(((uintptr_t)new_extent3 & matras_ver_mask) == 0);
	if (!new_extent3)
		return 0;

	uintptr_t owner_mask1 = matras_ver(*l1) & m->ver_occ_mask;
	assert(owner_mask1 & 1);
	uintptr_t owner_mask2 = matras_ver(*l2) & m->ver_occ_mask;
	assert(owner_mask2 & 1);
	uintptr_t *new_extent1 = 0, *new_extent2 = 0;
	if (owner_mask1 != 1) {
		new_extent1 = (uintptr_t *)m->alloc_func();
		assert(((uintptr_t)new_extent1 & matras_ver_mask) == 0);
		if (!new_extent1) {
			m->free_func(new_extent3);
			return 0;
		}
	}
	if (owner_mask2 != 1) {
		new_extent2 = (uintptr_t *)m->alloc_func();
		assert(((uintptr_t)new_extent2 & matras_ver_mask) == 0);
		if (!new_extent2) {
			m->free_func(new_extent3);
			if (owner_mask1 != 1)
				m->free_func(new_extent1);
			return 0;
		}
	}
	uintptr_t new_tag = (~(m->ver_occ_mask ^ 1)) & matras_ver_mask;

	if (owner_mask1 != 1) {
		memcpy(new_extent1, matras_ptr(*l1), m->extent_size);
		*l1 = (uintptr_t)new_extent1 | new_tag;
		uintptr_t run = owner_mask1 ^ 1;
		uintptr_t oth_tag = run & m->ver_occ_mask;
		do {
			uint32_t ver = __builtin_ctzl(run);
			run ^= ((uintptr_t)1) << ver;
			m->roots[ver] = (m->roots[ver] & matras_ptr_mask) | oth_tag;
		} while (run);
		l2 = &matras_ptr(*l1)[n1];
	}

	if (owner_mask2 != 1) {
		memcpy(new_extent2, matras_ptr(*l2), m->extent_size);
		*l2 = (uintptr_t)new_extent2 | new_tag;
		uintptr_t run = owner_mask2 ^ 1;
		uintptr_t oth_tag = run & m->ver_occ_mask;
		do {
			uint32_t ver = __builtin_ctzl(run);
			run ^= ((uintptr_t)1) << ver;
			uintptr_t *ptr = &matras_ptr(m->roots[ver])[n1];
			*ptr = (*ptr & matras_ptr_mask) | oth_tag;
		} while (run);
		l3 = &matras_ptr(*l2)[n2];
	}

	memcpy(new_extent3, matras_ptr(*l3), m->extent_size);
	*l3 = (uintptr_t)new_extent3 | new_tag;
	uintptr_t run = owner_mask3 ^ 1;
	uintptr_t oth_tag = run & m->ver_occ_mask;
	do {
		uint32_t ver = __builtin_ctzl(run);
		run ^= ((uintptr_t)1) << ver;
		uintptr_t *ptr = &matras_ptr(matras_ptr(m->roots[ver])[n1])[n2];
		*ptr = (*ptr & matras_ptr_mask) | oth_tag;
	} while (run);

	return &((char *)new_extent3)[n3 * m->block_size];
}

/*
 * Debug check that ensures internal consistency.
 * Must return 0. If it returns not 0, smth is terribly wrong.
 */
uintptr_t
matras_debug_selfcheck(const struct matras *m)
{
	uintptr_t res = 0, i;
	for (i = 0; i < MATRAS_VERSION_COUNT; i++) {
		uintptr_t me = ((uintptr_t) 1) << i;
		if (!(m->ver_occ_mask & me))
			continue;
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[i]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			if (!(matras_ver(m->roots[i]) & me))
				res |= (1 | (me << 12));
			if ((matras_ver(m->roots[i]) & m->ver_occ_mask) != me) {
				uintptr_t run = matras_ver(m->roots[i])
					& m->ver_occ_mask;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((uintptr_t) 1) << oth_ver;
					if (matras_ver(m->roots[oth_ver])
						!= matras_ver(m->roots[i]))
						res |= (8 | (me << 12));
				} while (run);
			}
			if (!(matras_ver(matras_ptr(m->roots[i])[n1]) & me))
				res |= (2 | (me << 12));
			if ((matras_ver(matras_ptr(m->roots[i])[n1]) & m->ver_occ_mask) != me) {
				uintptr_t run =
					matras_ver(matras_ptr(m->roots[i])[n1])
						& m->ver_occ_mask;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((uintptr_t) 1) << oth_ver;
					if (matras_ver(matras_ptr(m->roots[oth_ver])[n1])
						!= matras_ver(matras_ptr(m->roots[i])[n1]))
						res |= (0x80 | (me << 12));
				} while (run);
			}
			if (!(matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2])& me))
				res |= (4 | (me << 12));
			if ((matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2]) & m->ver_occ_mask)
				!= me) {
				uintptr_t run =
					matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2])
						& m->ver_occ_mask;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((uintptr_t) 1) << oth_ver;

					if (matras_ver(matras_ptr(matras_ptr(m->roots[oth_ver])[n1])[n2])
						!= matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2]))
						res |= (0x800 | (me << 12));
				} while (run);
			}
		}
	}
	{
		i = 0;
		uintptr_t me = ((uintptr_t) 1) << i;
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[i]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			if ((matras_ver(m->roots[i]) & ~m->ver_occ_mask)
				!= (matras_ver_mask & ~m->ver_occ_mask))
				res |= (0x10 | (me << 12));
			if ((matras_ver(matras_ptr(m->roots[i])[n1]) & ~m->ver_occ_mask)
				!= (matras_ver_mask & ~m->ver_occ_mask))
				res |= (0x20 | (me << 12));
			if ((matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2]) & ~m->ver_occ_mask)
				!= (matras_ver_mask & ~m->ver_occ_mask))
				res |= (0x40 | (me << 12));
		}
	}
	for (i = 1; i < MATRAS_VERSION_COUNT; i++) {
		uintptr_t me = ((uintptr_t) 1) << i;
		if (!(m->ver_occ_mask & me))
			continue;
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[i]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			if (!(matras_ver(m->roots[i]) & 1))
				if ((matras_ver(m->roots[i]) & ~m->ver_occ_mask) != 0)
					res |= (0x100 | (me << 12));
			if (!(matras_ver(matras_ptr(m->roots[i])[n1]) & 1))
				if ((matras_ver(matras_ptr(m->roots[i])[n1]) & ~m->ver_occ_mask)
					!= 0)
					res |= (0x200 | (me << 12));
			if (!(matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2]) & 1))
				if ((matras_ver(matras_ptr(matras_ptr(m->roots[i])[n1])[n2])
					& ~m->ver_occ_mask) != 0)
					res |= (0x400 | (me << 12));
		}
	}
	return res;
}
