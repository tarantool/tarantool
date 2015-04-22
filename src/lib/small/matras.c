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
	assert(extent_size > sizeof(struct matras_record));

	m->block_counts[0] = 0;
	m->extent_size = extent_size;
	m->block_size = block_size;

	m->ver_occ_mask = 1;

	matras_id_t log1 = matras_log2(extent_size);
	matras_id_t log2 = matras_log2(block_size);
	assert((sizeof(struct matras_record) &
	       (sizeof(struct matras_record) - 1)) == 0);
	matras_id_t log3 = matras_log2(sizeof(struct matras_record));
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
	while(m->ver_occ_mask != 1) {
		matras_id_t ver = __builtin_ctzl(m->ver_occ_mask ^ 1);
		matras_delete_version(m, ver);
	}
	if (m->block_counts[0]) {
		struct matras_record *extent1 = m->roots[0].ptr;
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

			struct matras_record *extent2 = extent1[n1].ptr;
			for (j = 0; j < n2; j++) {
				struct matras_record *extent3 = extent2[j].ptr;
				m->free_func(extent3);
			}
			m->free_func(extent2);
		}

		/* free fully loaded extents */
		matras_id_t n2 = m->extent_size / sizeof(struct matras_record);
		for ( i = 0; i < n1; i++) {
			struct matras_record *extent2 = extent1[i].ptr;
			for (j = 0; j < n2; j++) {
				struct matras_record *extent3 = extent2[j].ptr;
				m->free_func(extent3);
			}
			m->free_func(extent2);
		}

		m->free_func(extent1);
		m->block_counts[0] = 0;
	}
#ifndef __OPTIMIZE__
	m->roots[0].ptr = (struct matras_record *)(void *)(long long)0xDEADBEEF;
#endif
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

	struct matras_record *extent1, *extent2, *extent3;

	if (extent1_not_empty) {
		extent1 = m->roots[0].ptr;
	} else {
		extent1 = (struct matras_record *)m->alloc_func();
		if (!extent1)
			return 0;
		m->roots[0].ptr = extent1;
		m->roots[0].tag = ~(m->ver_occ_mask ^ 1);
	}

	if (extent2_not_empty) {
		extent2 = extent1[n1].ptr;
	} else {
		extent2 = (struct matras_record *)m->alloc_func();
		if (!extent2) {
			if (!extent1_not_empty) /* means - was empty */
				m->free_func(extent1);
			return 0;
		}
		extent1[n1].ptr = extent2;
		extent1[n1].tag = ~(m->ver_occ_mask ^ 1);
	}

	if (extent3_not_empty) {
		extent3 = extent2[n2].ptr;
	} else {
		extent3 = (struct matras_record *)m->alloc_func();
		if (!extent3) {
			if (!extent1_not_empty) /* means - was empty */
				m->free_func(extent1);
			if (!extent2_not_empty) /* means - was empty */
				m->free_func(extent2);
			return 0;
		}
		extent2[n2].ptr = extent3;
		extent2[n2].tag = ~(m->ver_occ_mask ^ 1);
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
	matras_before_change(m, last);
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
		struct matras_record *extent1, *extent2, *extent3;
		extent1 = m->roots[0].ptr;
		extent2 = extent1[n1].ptr;
		extent3 = extent2[n2].ptr;
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
matras_extents_count(const struct matras *m)
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
		c = (c + (m->extent_size / sizeof(struct matras_record) - 1))
			/ (m->extent_size / sizeof(struct matras_record));
		res += c;
	}

	return res;
}

/*
 * Create new version of matras memory.
 * Return 0 if all version IDs are occupied.
 */
matras_id_t
matras_new_version(struct matras *m)
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
	m->ver_occ_mask |= ((matras_version_tag_t)1) << ver_id;
	m->roots[ver_id] = m->roots[0];
	m->block_counts[ver_id] = m->block_counts[0];
	return ver_id;
}

/*
 * Delete memory version by specified ID.
 */
void
matras_delete_version(struct matras *m, matras_id_t ver_id)
{
	matras_version_tag_t me = ((matras_version_tag_t)1) << ver_id;
	if (m->block_counts[ver_id]) {
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[ver_id]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			matras_version_tag_t owners =
				m->roots[ver_id].ptr[n1].ptr[n2].tag &
				m->ver_occ_mask;
			assert(owners & me);
			if (owners == me) {
				m->free_func(m->roots[ver_id].ptr[n1].ptr[n2].ptr);
				m->roots[ver_id].ptr[n1].ptr[n2].tag ^= me;
#ifndef __OPTIMIZE__
				m->roots[ver_id].ptr[n1].ptr[n2].ptr =
					(struct matras_record *)(void *)
					(long long)0xDEADBEEF;
#endif
			} else {
				matras_version_tag_t run = owners;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((matras_version_tag_t)1) << oth_ver;
					assert((m->roots[oth_ver].ptr[n1].ptr[n2].tag &
						m->ver_occ_mask & ~me) ==
					       (owners & ~me));
					assert(m->roots[oth_ver].ptr[n1].ptr[n2].ptr ==
					       m->roots[ver_id].ptr[n1].ptr[n2].ptr);
					m->roots[oth_ver].ptr[n1].ptr[n2].tag &= ~me;
				} while (run);
			}
		}
		step = m->mask1 + 1;
		for (j = 0; j < m->block_counts[ver_id]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_version_tag_t owners =
				m->roots[ver_id].ptr[n1].tag & m->ver_occ_mask;
			assert(owners & me);
			if (owners == me) {
				m->free_func(m->roots[ver_id].ptr[n1].ptr);
				m->roots[ver_id].ptr[n1].tag ^= me;
#ifndef __OPTIMIZE__
				m->roots[ver_id].ptr[n1].ptr =
					(struct matras_record *)(void *)
					(long long)0xDEADBEEF;
#endif
			} else {
				matras_version_tag_t run = owners;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((matras_version_tag_t)1) << oth_ver;
					assert((m->roots[oth_ver].ptr[n1].tag &
						m->ver_occ_mask & ~me) ==
					       (owners & ~me));
					assert(m->roots[oth_ver].ptr[n1].ptr ==
					       m->roots[ver_id].ptr[n1].ptr);
					m->roots[oth_ver].ptr[n1].tag &= ~me;
				} while (run);
			}
		}
		matras_version_tag_t owners
			= m->roots[ver_id].tag & m->ver_occ_mask;
		assert(owners & me);
		if (owners == me) {
			m->free_func(m->roots[ver_id].ptr);
			m->roots[ver_id].tag ^= me;
#ifndef __OPTIMIZE__
			m->roots[ver_id].ptr =
				(struct matras_record *)(void *)
				(long long)0xDEADBEEF;
#endif
		} else {
			matras_version_tag_t run = owners;
			do {
				uint32_t oth_ver = __builtin_ctzl(run);
				run ^= ((matras_version_tag_t)1) << oth_ver;
				assert((m->roots[oth_ver].tag &
					m->ver_occ_mask & ~me) ==
				       (owners & ~me));
				assert(m->roots[oth_ver].ptr ==
				       m->roots[ver_id].ptr);
				m->roots[oth_ver].tag &= ~me;
			} while (run);
		}
		m->block_counts[ver_id] = 0;
	}
	if (m->block_counts[0]) {
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[0]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			matras_version_tag_t run =
				m->roots[0].ptr[n1].ptr[n2].tag &
				m->ver_occ_mask;
			do {
				uint32_t oth_ver = __builtin_ctzl(run);
				run ^= ((matras_version_tag_t)1) << oth_ver;
				m->roots[oth_ver].ptr[n1].ptr[n2].tag |= me;
			} while (run);
		}
		step = m->mask1 + 1;
		for (j = 0; j < m->block_counts[0]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_version_tag_t run =
				m->roots[0].ptr[n1].tag & m->ver_occ_mask;
			do {
				uint32_t oth_ver = __builtin_ctzl(run);
				run ^= ((matras_version_tag_t)1) << oth_ver;
				m->roots[oth_ver].ptr[n1].tag |= me;
			} while (run);
		}
		matras_version_tag_t run =
			m->roots[0].tag & m->ver_occ_mask;
		do {
			uint32_t oth_ver = __builtin_ctzl(run);
			run ^= ((matras_version_tag_t)1) << oth_ver;
			m->roots[oth_ver].tag |= me;
		} while (run);
	}
	m->ver_occ_mask ^= me;
}

/*
 * Notify matras that memory at given ID will be changed.
 * Returns true if ok, and false if failed to allocate memory.
 */
void *
matras_before_change(struct matras *m, matras_id_t id)
{
	assert(id < m->block_counts[0]);

	/* see "Shifts and masks explanation" for details */
	matras_id_t n1 = id >> m->shift1;
	matras_id_t n2 = (id & m->mask1) >> m->shift2;
	matras_id_t n3 = id & m->mask2;

	struct matras_record *l1 = &m->roots[0];
	struct matras_record *l2 = &l1->ptr[n1];
	struct matras_record *l3 = &l2->ptr[n2];
	matras_version_tag_t owner_mask3 = l3->tag & m->ver_occ_mask;
	assert(owner_mask3 & 1);
	if (owner_mask3 == 1)
		return &((char *)(l3->ptr))[n3 * m->block_size]; /* private page */

	struct matras_record *new_extent3 =
		(struct matras_record *)m->alloc_func();
	if (!new_extent3)
		return 0;

	matras_version_tag_t owner_mask1 = l1->tag & m->ver_occ_mask;
	assert(owner_mask1 & 1);
	matras_version_tag_t owner_mask2 = l2->tag & m->ver_occ_mask;
	assert(owner_mask2 & 1);
	struct matras_record *new_extent1 = 0, *new_extent2 = 0;
	if (owner_mask1 != 1) {
		new_extent1 = (struct matras_record *)m->alloc_func();
		if (!new_extent1) {
			m->free_func(new_extent3);
			return 0;
		}
	}
	if (owner_mask2 != 1) {
		new_extent2 = (struct matras_record *)m->alloc_func();
		if (!new_extent2) {
			m->free_func(new_extent3);
			if (owner_mask1 != 1)
				m->free_func(new_extent1);
			return 0;
		}
	}
	matras_version_tag_t new_tag = ~(m->ver_occ_mask ^ 1);

	if (owner_mask1 != 1) {
		memcpy(new_extent1, l1->ptr, m->extent_size);
		l1->tag = new_tag;
		l1->ptr = new_extent1;
		matras_version_tag_t run = owner_mask1 ^ 1;
		matras_version_tag_t oth_tag = run & m->ver_occ_mask;
		do {
			uint32_t ver = __builtin_ctzl(run);
			run ^= ((matras_version_tag_t)1) << ver;
			m->roots[ver].tag = oth_tag;
		} while (run);
		l2 = &l1->ptr[n1];
	}

	if (owner_mask2 != 1) {
		memcpy(new_extent2, l2->ptr, m->extent_size);
		l2->tag = new_tag;
		l2->ptr = new_extent2;
		matras_version_tag_t run = owner_mask2 ^ 1;
		matras_version_tag_t oth_tag = run & m->ver_occ_mask;
		do {
			uint32_t ver = __builtin_ctzl(run);
			run ^= ((matras_version_tag_t)1) << ver;
			m->roots[ver].ptr[n1].tag = oth_tag;
		} while (run);
		l3 = &l2->ptr[n2];
	}

	memcpy(new_extent3, l3->ptr, m->extent_size);
	l3->tag = new_tag;
	l3->ptr = new_extent3;
	matras_version_tag_t run = owner_mask3 ^ 1;
	matras_version_tag_t oth_tag = run & m->ver_occ_mask;
	do {
		uint32_t ver = __builtin_ctzl(run);
		run ^= ((matras_version_tag_t)1) << ver;
		m->roots[ver].ptr[n1].ptr[n2].tag = oth_tag;
	} while (run);

	return &((char *)new_extent3)[n3 * m->block_size]; ;
}

/*
 * Debug check that ensures internal consistency.
 * Must return 0. If i returns not 0, smth is terribly wrong.
 */
matras_version_tag_t
matras_debug_selfcheck(const struct matras *m)
{
	matras_version_tag_t res = 0, i;
	for (i = 0; i < MATRAS_VERSION_COUNT; i++) {
		matras_version_tag_t me = ((matras_version_tag_t) 1) << i;
		if (!(m->ver_occ_mask & me))
			continue;
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[i]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			if (!(m->roots[i].tag & me))
				res |= (1 | (me << 12));
			if ((m->roots[i].tag & m->ver_occ_mask) != me) {
				matras_version_tag_t run = m->roots[i].tag
					& m->ver_occ_mask;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((matras_version_tag_t) 1)
						<< oth_ver;
					if (m->roots[i].tag
						!= m->roots[oth_ver].tag)
						res |= (8 | (me << 12));
				} while (run);
			}
			if (!(m->roots[i].ptr[n1].tag & me))
				res |= (2 | (me << 12));
			if ((m->roots[i].ptr[n1].tag & m->ver_occ_mask) != me) {
				matras_version_tag_t run =
					m->roots[i].ptr[n1].tag
						& m->ver_occ_mask;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((matras_version_tag_t) 1)
						<< oth_ver;
					if (m->roots[i].ptr[n1].tag
						!= m->roots[oth_ver].ptr[n1].tag)
						res |= (0x80 | (me << 12));
				} while (run);
			}
			if (!(m->roots[i].ptr[n1].ptr[n2].tag & me))
				res |= (4 | (me << 12));
			if ((m->roots[i].ptr[n1].ptr[n2].tag & m->ver_occ_mask)
				!= me) {
				matras_version_tag_t run =
					m->roots[i].ptr[n1].ptr[n2].tag
						& m->ver_occ_mask;
				do {
					uint32_t oth_ver = __builtin_ctzl(run);
					run ^= ((matras_version_tag_t) 1)
						<< oth_ver;
					if (m->roots[i].ptr[n1].ptr[n2].tag
						!= m->roots[oth_ver].ptr[n1].ptr[n2].tag)
						res |= (0x800 | (me << 12));
				} while (run);
			}
		}
	}
	{
		i = 0;
		matras_version_tag_t me = ((matras_version_tag_t) 1) << i;
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[i]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			if ((m->roots[i].tag & ~m->ver_occ_mask)
				!= ~m->ver_occ_mask)
				res |= (0x10 | (me << 12));
			if ((m->roots[i].ptr[n1].tag & ~m->ver_occ_mask)
				!= ~m->ver_occ_mask)
				res |= (0x20 | (me << 12));
			if ((m->roots[i].ptr[n1].ptr[n2].tag & ~m->ver_occ_mask)
				!= ~m->ver_occ_mask)
				res |= (0x40 | (me << 12));
		}
	}
	for (i = 1; i < MATRAS_VERSION_COUNT; i++) {
		matras_version_tag_t me = ((matras_version_tag_t) 1) << i;
		if (!(m->ver_occ_mask & me))
			continue;
		matras_id_t step = m->mask2 + 1, j;
		for (j = 0; j < m->block_counts[i]; j += step) {
			matras_id_t n1 = j >> m->shift1;
			matras_id_t n2 = (j & m->mask1) >> m->shift2;
			if (!(m->roots[i].tag & 1))
				if ((m->roots[i].tag & ~m->ver_occ_mask) != 0)
					res |= (0x100 | (me << 12));
			if (!(m->roots[i].ptr[n1].tag & 1))
				if ((m->roots[i].ptr[n1].tag & ~m->ver_occ_mask)
					!= 0)
					res |= (0x200 | (me << 12));
			if (!(m->roots[i].ptr[n1].ptr[n2].tag & 1))
				if ((m->roots[i].ptr[n1].ptr[n2].tag
					& ~m->ver_occ_mask) != 0)
					res |= (0x400 | (me << 12));
		}
	}
	return res;
}
