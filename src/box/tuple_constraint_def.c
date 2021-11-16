/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tuple_constraint_def.h"

#include "diag.h"
#include "error.h"
#include "trivia/util.h"
#include "salad/grp_alloc.h"
#include "small/region.h"
#include "msgpuck.h"

int
tuple_constraint_def_cmp(const struct tuple_constraint_def *def1,
			 const struct tuple_constraint_def *def2,
			 bool ignore_name)
{
	int rc;
	if (!ignore_name) {
		if (def1->name_len != def2->name_len)
			return def1->name_len < def2->name_len ? -1 : 1;
		rc = memcmp(def1->name, def2->name, def1->name_len);
		if (rc != 0)
			return rc;
	}
	return def1->func.id < def2->func.id ? -1 :
	       def1->func.id > def2->func.id;
}

int
tuple_constraint_def_decode(const char **data,
			    struct tuple_constraint_def **def, uint32_t *count,
			    struct region *region,
			    uint32_t errcode, uint32_t field_no)
{
	/* Expected normal form of constraints: {name1=func1, name2=func2..}. */
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(ClientError, errcode, field_no,
			 "constraint field is expected to be a MAP");
		return -1;
	}

	uint32_t map_size = mp_decode_map(data);
	*count = map_size;
	if (*count == 0)
		return 0;

	int bytes;
	*def = region_alloc_array(region, struct tuple_constraint_def,
				  *count, &bytes);
	if (*def == NULL) {
		diag_set(OutOfMemory, bytes, "region", "array of constraints");
		return -1;
	}
	struct tuple_constraint_def *new_def = *def;

	for (size_t i = 0; i < map_size; i++) {
		if (mp_typeof(**data) != MP_STR) {
			diag_set(ClientError, errcode, field_no,
				 "constraint name is expected to be a string");
			return -1;
		}
		uint32_t str_len;
		const char *str = mp_decode_str(data, &str_len);
		char *str_copy = region_alloc(region, str_len + 1);
		if (str_copy == NULL) {
			diag_set(OutOfMemory, str_len + 1, "region",
				 i % 2 == 0 ? "constraint name"
					    : "constraint func");
			return -1;
		}
		memcpy(str_copy, str, str_len);
		str_copy[str_len] = 0;
		new_def[i].name = str_copy;
		new_def[i].name_len = str_len;

		if (mp_typeof(**data) != MP_UINT) {
			diag_set(ClientError, errcode, field_no,
				 "constraint function ID "
				 "is expected to be a number");
			return -1;
		}
		new_def[i].func.id = mp_decode_uint(data);
	}
	return 0;
}

/**
 * Reserve strings needed for given constraint definition @a dev in given
 * string @a bank.
 */
static void
tuple_constraint_def_reserve(const struct tuple_constraint_def *def,
			     struct grp_alloc *all)
{
	grp_alloc_reserve_str(all, def->name_len);
}

/**
 * Copy constraint definition from @a str to @ dst, allocating strings on
 * string @a bank.
 */
static void
tuple_constraint_def_copy(struct tuple_constraint_def *dst,
			  const struct tuple_constraint_def *src,
			  struct grp_alloc *all)
{
	dst->name = grp_alloc_create_str(all, src->name, src->name_len);
	dst->name_len = src->name_len;
	dst->func.id = src->func.id;
}

struct tuple_constraint_def *
tuple_constraint_def_array_dup(const struct tuple_constraint_def *defs,
			       size_t count)
{
	struct tuple_constraint_def *res =
		tuple_constraint_def_array_dup_raw(defs, count,
						   sizeof(*res), 0);
	return res;
}

void *
tuple_constraint_def_array_dup_raw(const struct tuple_constraint_def *defs,
				   size_t count, size_t object_size,
				   size_t additional_size)
{
	if (count == 0) {
		assert(additional_size == 0);
		return NULL;
	}

	/*
	 * The following memory layout will be created:
	 * | object | object | object | additional | tuple_constraint_def data |
	 * where the first bytes of object is struct tuple_constraint_def;
	 * additional is a region that was requested (of additional_size);
	 * tuple_constraint_def is additional data that is needed for
	 * tuple_constraint_def part of object.
	 *
	 * To make sure that tuple_constraint_def data is aligned (that will
	 * be necessary in one of the following commits) let's ensure that
	 * object size is a multiple of 8 and round up additional_size to
	 * the closest multiple of 8.
	 */
	assert(object_size % 8 == 0);
	additional_size = (additional_size + 7) & ~7;

	struct grp_alloc all = grp_alloc_initializer();
	/* Calculate needed space. */
	for (size_t i = 0; i < count; i++)
		tuple_constraint_def_reserve(&defs[i], &all);
	size_t total_size =
		object_size * count + additional_size + grp_alloc_size(&all);

	/* Allocate block. */
	char *res = xmalloc(total_size);
	grp_alloc_use(&all, res + object_size * count + additional_size);

	/* Now constraint defs in the new array. */
	for (size_t i = 0; i < count; i++) {
		struct tuple_constraint_def *def =
			(struct tuple_constraint_def *)(res + i * object_size);
		tuple_constraint_def_copy(def, &defs[i], &all);
	}

	/* If we did it correctly then there is no more space for strings. */
	assert(grp_alloc_size(&all) == 0);
	return res;
}
