/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Group alloc is a data structure that is designed for simplification of
 * allocation of several objects in one memory block. It could be anything,
 * but special attention is given to null-terminating strings. Each string is
 * allocated with a personal null termination symbol, in the end memory block
 * while all other objects will be placed in the beginning of the block. This
 * guarantee allows to play with objects' alignment.
 * Typical usage consist of two phases: gathering total needed size of memory
 * block and creation of objects in given block.
 *
 * Example of usage:
 * struct object {
 *	int *array;
 *	const char *name;
 * };
 *
 * struct object *
 * object_new(int *array, size_t array_size, const char *name, size_t name_len)
 * {
 *	// Gather required memory size.
 *	struct grp_alloc all = grp_alloc_initializer();
 *	size_t array_data_size = array_size * sizeof(*array);
 *	grp_alloc_reserve_data(&all, array_data_size);
 *	grp_alloc_reserve_str(&all, name_len);
 *	struct test *res = malloc(sizeof(struct test) + grp_alloc_size(&all));
 *	// Use memory block of required size.
 *	grp_alloc_use(&all, res + 1);
 *	res->array = grp_alloc_create_data(&all, array_data_size);
 *	memcpy(res->array, array, array_data_size);
 *	res->name = grp_alloc_create_str(&all, name, name_len);
 *	assert(grp_alloc_size(&all) == 0);
 *	return res;
 * }
 *
 */
struct grp_alloc {
	/**
	 * Points to the beginning of remaining memory block. Can be NULL in
	 * the first phase (gathering required memory).
	 */
	char *data;
	/**
	 * End of required remaining memory block.
	 */
	char *data_end;
};

/**
 * Default grp_alloc initializer. Just assign it to new grp_alloc and it's
 * ready for the first phase.
 */
static inline struct grp_alloc
grp_alloc_initializer(void)
{
	struct grp_alloc res = {NULL, NULL};
	return res;
}

/**
 * Phase 1: account am arbitrary data of @a size that is needed to be allocated.
 */
static inline void
grp_alloc_reserve_data(struct grp_alloc *bank, size_t size)
{
	bank->data_end += size;
}

/**
 * Phase 1: account a string of @a size that is needed to be allocated,
 * including char for null-termination.
 */
static inline void
grp_alloc_reserve_str(struct grp_alloc *bank, size_t size)
{
	bank->data_end += size + 1;
}

/**
 * Phase 1: account a null-terminated string that is needed to be allocated,
 * including char for null-termination.
 */
static inline void
grp_alloc_reserve_str0(struct grp_alloc *bank, const char *src)
{
	grp_alloc_reserve_str(bank, strlen(src));
}

/**
 * Phase 1 end: get total memory size required for all data.
 */
static inline size_t
grp_alloc_size(struct grp_alloc *bank)
{
	return bank->data_end - bank->data;
}

/**
 * Phase 2 begin: provide a block of memory of required size.
 */
static inline void
grp_alloc_use(struct grp_alloc *bank, void *data)
{
	bank->data_end = (char *)data + (bank->data_end - bank->data);
	bank->data = (char *)data;
}

/**
 * Phase 2: allocate an arbitrary data block with given @a size.
 */
static inline void *
grp_alloc_create_data(struct grp_alloc *bank, size_t size)
{
	char *res = bank->data;
	bank->data += size;
	return res;
}

/**
 * Phase 2: allocate and fill a string with given data @a src of given size
 * @a src_size, including null-termination. Return new string.
 */
static inline char *
grp_alloc_create_str(struct grp_alloc *bank, const char *src, size_t src_size)
{
	bank->data_end--;
	*bank->data_end = 0;
	bank->data_end -= src_size;
	memcpy(bank->data_end, src, src_size);
	return bank->data_end;
}

/**
 * Phase 2: allocate and fill a null-terminated string with given data @a src,
 * including null-termination. Return new string.
 */
static inline char *
grp_alloc_create_str0(struct grp_alloc *bank, const char *src)
{
	return grp_alloc_create_str(bank, src, strlen(src));
}

#ifdef __cplusplus
} /* extern "C" */
#endif
