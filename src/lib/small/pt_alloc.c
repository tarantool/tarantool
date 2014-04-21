/*
 * pt_alloc implementation
 */

#include "pt_alloc.h"
#include <limits.h>
#ifndef __OPTIMIZE__ /* Assert is not used in optimized build */
#include <assert.h>
#endif

/*
 * Assert is turned off in optimized build
 */
#ifndef __OPTIMIZE__
#define pt_assert(e) assert(e)
#else
#define pt_assert(e) do {} while(0)
#endif

/*
 * Binary logarithm of value (exact if the value is a power of 2,
 * appoximate otherwise)
 */
static PT_ID_T
pt_log2(PT_ID_T val)
{
	pt_assert(val > 0);
	return sizeof(unsigned long) * CHAR_BIT -
		__builtin_clzl((unsigned long) val) - 1;
}

/*
 * Construction
 */
void
pt3_construct(pt3 *p, PT_ID_T extent_size, PT_ID_T block_size,
		prov_alloc_func alloc_func, prov_free_func free_func)
{
	/*extent_size must be power of 2 */
	pt_assert((extent_size & (extent_size - 1)) == 0);
	/*block_size must be power of 2 */
	pt_assert((block_size & (block_size - 1)) == 0);

	p->extent = 0;
	p->created = 0;
	p->extent_size = extent_size;
	p->block_size = block_size;

	PT_ID_T log1 = pt_log2(extent_size);
	PT_ID_T log2 = pt_log2(block_size);
	PT_ID_T log3 = pt_log2(sizeof(void *));
	p->log2_capacity = log1 * 3 - log2 - log3 * 2;
	p->shift1 = log1 * 2 - log2 - log3;
	p->shift2 = log1 - log2;

	p->mask1 = (((PT_ID_T)1) << p->shift1) - ((PT_ID_T)1);
	p->mask2 = (((PT_ID_T)1) << p->shift2) - ((PT_ID_T)1);

	p->alloc_func = alloc_func;
	p->free_func = free_func;
}

void
pt2_construct(pt2 *p, PT_ID_T extent_size, PT_ID_T block_size,
		prov_alloc_func alloc_func, prov_free_func free_func)
{
	/*extent_size must be power of 2 */
	pt_assert((extent_size & (extent_size - 1)) == 0);
	/*block_size must be power of 2 */
	pt_assert((block_size & (block_size - 1)) == 0);

	p->extent = 0;
	p->created = 0;
	p->extent_size = extent_size;
	p->block_size = block_size;

	PT_ID_T log1 = pt_log2(extent_size);
	PT_ID_T log2 = pt_log2(block_size);
	PT_ID_T log3 = pt_log2(sizeof(void *));
	p->log2_capacity = log1 * 2 - log2 - log3;
	p->shift = log1 - log2;

	p->mask = (((PT_ID_T)1) << p->shift) - ((PT_ID_T)1);

	p->alloc_func = alloc_func;
	p->free_func = free_func;
}

/*
 * Destruction
 */
void
pt3_destroy(pt3 *p)
{
	if (p->created) {
		void* extent1 = p->extent;
		PT_ID_T id = p->created;

		PT_ID_T index1 = id >> p->shift1;
		id &= p->mask1;

		if (id) {
			PT_ID_T index2 = id >> p->shift2;
			id &= p->mask2;
			if (id)
				index2++;

			void *extent2 = ((void **)extent1)[index1];
			for (PT_ID_T j = 0; j < index2; j++) {
				void *extent3 = ((void **)extent2)[j];
				p->free_func(extent3);
			}
			p->free_func(extent2);
		}

		PT_ID_T index2 = p->extent_size / sizeof(void *);
		for (PT_ID_T i = 0; i < index1; i++) {
			void *extent2 = ((void **)extent1)[i];
			for (PT_ID_T j = 0; j < index2; j++) {
				void *extent3 = ((void **)extent2)[j];
				p->free_func(extent3);
			}
			p->free_func(extent2);
		}

		p->free_func(extent1);
		p->created = 0;
	}
#ifndef __OPTIMIZE__
	p->extent = (void *)0xDEADBEEF;
#endif
}

void
pt2_destroy(pt2 *p)
{
	if (p->created) {
		PT_ID_T id = p->created;
		PT_ID_T index1 = id >> p->shift;
		id &= p->mask;
		if (id)
			index1++;

		void* extent1 = p->extent;
		for (PT_ID_T i = 0; i < index1; i++) {
			void *extent2 = ((void **)extent1)[i];
			p->free_func(extent2);
		}
		p->free_func(extent1);

		p->created = 0;
	}
#ifndef __OPTIMIZE__
	p->extent = (void *)0xDEADBEEF;
#endif
}


/*
 * Allocation
 */
void *
pt3_alloc(pt3 *p, PT_ID_T *result_id)
{
	if (p->created)
		pt_assert(pt_log2(p->created) < p->log2_capacity);

	PT_ID_T id = p->created;
	PT_ID_T extent1_not_empty = id;
	PT_ID_T index1 = id >> p->shift1;
	id &= p->mask1;
	PT_ID_T extent2_not_empty = id;
	PT_ID_T index2 = id >> p->shift2;
	id &= p->mask2;
	PT_ID_T extent3_not_empty = id;
	PT_ID_T index3 = id;

	void *extent1, *extent2, *extent3;

	if (extent1_not_empty) {
		extent1 = p->extent;
	} else {
		extent1 = p->alloc_func();
		if (!extent1)
			return 0;
		p->extent = extent1;
	}

	if (extent2_not_empty) {
		extent2 = ((void **)extent1)[index1];
	} else {
		extent2 = p->alloc_func();
		if (!extent2) {
			if (!extent1_not_empty) /* means - was empty */
				p->free_func(extent1);
			return 0;
		}
		((void **)extent1)[index1] = extent2;
	}

	if (extent3_not_empty) {
		extent3 = ((void **)extent2)[index2];
	} else {
		extent3 = p->alloc_func();
		if (!extent3) {
			if (!extent1_not_empty) /* means - was empty */
				p->free_func(extent1);
			if (!extent2_not_empty) /* means - was empty */
				p->free_func(extent2);
			return 0;
		}
		((void **)extent2)[index2] = extent3;
	}

	*result_id = p->created++;
	return (void*)((char*)extent3 + index3 * p->block_size);
}

void *
pt2_alloc(pt2 *p, PT_ID_T *result_id)
{
	if (p->created)
		pt_assert(pt_log2(p->created) < p->log2_capacity);

	PT_ID_T id = p->created;
	PT_ID_T extent1_not_empty = id;
	PT_ID_T index1 = id >> p->shift;
	id &= p->mask;
	PT_ID_T extent2_not_empty = id;
	PT_ID_T index2 = id;

	void *extent1, *extent2;

	if (extent1_not_empty) {
		extent1 = p->extent;
	} else {
		extent1 = p->alloc_func();
		if (!extent1)
			return 0;
		p->extent = extent1;
	}

	if (extent2_not_empty) {
		extent2 = ((void **)extent1)[index1];
	} else {
		extent2 = p->alloc_func();
		if (!extent2) {
			if (!extent1_not_empty) /* means - was empty */
				p->free_func(extent1);
			return 0;
		}
		((void **)extent1)[index1] = extent2;
	}

	*result_id = p->created++;
	return (void*)((char*)extent2 + index2 * p->block_size);
}

/*
 * Restoration
 */
void *
pt3_get(const pt3 *p, PT_ID_T id)
{
	pt_assert(id < p->created);

	PT_ID_T index1 = id >> p->shift1;
	id &= p->mask1;
	PT_ID_T index2 = id >> p->shift2;
	id &= p->mask2;
	PT_ID_T index3 = id;

	return (((char***)p->extent)[index1][index2] + index3 * p->block_size);
}

void *
pt2_get(const pt2 *p, PT_ID_T id)
{
	pt_assert(id < p->created);

	PT_ID_T index1 = id >> p->shift;
	id &= p->mask;
	PT_ID_T index2 = id;

	return (((char**)p->extent)[index1] + index2 * p->block_size);
}

/*
 * Getting number of allocated chunks (of size p->chunk_size each)
 */
PT_ID_T
pt3_extents_count(const pt3 *p)
{
	PT_ID_T c = (p->created + (p->extent_size / p->block_size - 1))
		/ (p->extent_size / p->block_size);
	PT_ID_T res = c;
	for (PT_ID_T i = 0; i < 2; i++) {
		c = (c + (p->extent_size / sizeof(void *) - 1))
			/ (p->extent_size / sizeof(void *));
		res += c;
	}
	return res;
}
PT_ID_T
pt2_extents_count(const pt2 *p)
{
	PT_ID_T c = (p->created + (p->extent_size / p->block_size - 1))
		/ (p->extent_size / p->block_size);
	PT_ID_T res = c;
	for (PT_ID_T i = 0; i < 1; i++) {
		c = (c + (p->extent_size / sizeof(void *) - 1))
			/ (p->extent_size / sizeof(void *));
		res += c;
	}
	return res;
}
