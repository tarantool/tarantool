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

/**
 * Compare two tuple_constraint_field_id objects.
 */
static int
field_id_cmp(const struct tuple_constraint_field_id *def1,
	     const struct tuple_constraint_field_id *def2)
{
	if (def1->id != def2->id)
		return def1->id < def2->id ? -1 : 1;
	if (def1->name_len != def2->name_len)
		return def1->name_len < def2->name_len ? -1 : 1;
	return memcmp(def1->name, def2->name, def1->name_len);
}

/**
 * Compare tuple_constraint_fkey_def part of constraint definition.
 */
static int
tuple_constraint_def_cmp_fkey(const struct tuple_constraint_fkey_def *def1,
			      const struct tuple_constraint_fkey_def *def2)
{
	if (def1->space_id != def2->space_id)
		return def1->space_id < def2->space_id ? -1 : 1;
	return field_id_cmp(&def1->field, &def2->field);
}

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
	if (def1->type != def2->type)
		return def1->type < def2->type ? -1 : 1;
	if (def1->type == CONSTR_FUNC)
		return def1->func.id < def2->func.id ? -1 :
		       def1->func.id > def2->func.id;
	assert(def1->type == CONSTR_FKEY);
	return tuple_constraint_def_cmp_fkey(&def1->fkey, &def2->fkey);
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

	uint32_t old_count = *count;
	struct tuple_constraint_def *old_def = *def;
	uint32_t map_size = mp_decode_map(data);
	*count += map_size;
	if (*count == 0)
		return 0;

	int bytes;
	*def = region_alloc_array(region, struct tuple_constraint_def,
				  *count, &bytes);
	if (*def == NULL) {
		diag_set(OutOfMemory, bytes, "region", "array of constraints");
		return -1;
	}
	for (uint32_t i = 0; i < old_count; i++)
		(*def)[i] = old_def[i];
	struct tuple_constraint_def *new_def = *def + old_count;

	for (size_t i = 0; i < map_size; i++)
		new_def[i].type = CONSTR_FUNC;

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
 * Decode space_id from msgpack and set a proper diag if failed.
 */
static int
space_id_decode(const char **data, uint32_t *space_id,
		uint32_t errcode, uint32_t field_no)
{
	if (mp_typeof(**data) != MP_UINT) {
		diag_set(ClientError, errcode, field_no,
			 "foreign key: space must be a number");
		return -1;
	}
	*space_id = mp_decode_uint(data);
	return 0;
}

/**
 * Decode tuple_constraint_field_id object from msgpack and set a proper
 * diag if failed.
 */
static int
field_id_decode(const char **data, struct tuple_constraint_field_id *def,
		struct region *region, uint32_t errcode, uint32_t field_no)
{
	if (mp_typeof(**data) == MP_UINT) {
		def->id = mp_decode_uint(data);
		def->name = "";
		def->name_len = 0;
	} else if (mp_typeof(**data) == MP_STR) {
		const char *str = mp_decode_str(data, &def->name_len);
		char *str_copy = region_alloc(region, def->name_len + 1);
		if (str_copy == NULL) {
			diag_set(OutOfMemory, def->name_len + 1,
				 "region", "string");
			return -1;
		}
		memcpy(str_copy, str, def->name_len);
		str_copy[def->name_len] = 0;
		def->name = str_copy;
		def->id = 0;
	} else {
		diag_set(ClientError, errcode, field_no,
			 "foreign key: field must be number or string");
		return -1;
	}
	return 0;
}

int
tuple_constraint_def_decode_fkey(const char **data,
				 struct tuple_constraint_def **def,
				 uint32_t *count, struct region *region,
				 uint32_t errcode, uint32_t field_no)
{
	/*
	 * Expected normal form of foreign keys: {name1=data1, name2=data2..},
	 * where dataX has form: {space=id, field=id/name}
	 */
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(ClientError, errcode, field_no,
			 "foreign key field is expected to be a MAP");
		return -1;
	}

	uint32_t old_count = *count;
	struct tuple_constraint_def *old_def = *def;
	uint32_t map_size = mp_decode_map(data);
	*count += map_size;
	if (*count == 0)
		return 0;

	int bytes;
	*def = region_alloc_array(region, struct tuple_constraint_def,
				  *count, &bytes);
	if (*def == NULL) {
		diag_set(OutOfMemory, bytes, "region", "array of constraints");
		return -1;
	}
	for (uint32_t i = 0; i < old_count; i++)
		(*def)[i] = old_def[i];
	struct tuple_constraint_def *new_def = *def + old_count;

	for (size_t i = 0; i < *count; i++) {
		if (mp_typeof(**data) != MP_STR) {
			diag_set(ClientError, errcode, field_no,
				 "foreign key name is expected to be a string");
			return -1;
		}
		uint32_t str_len;
		const char *str = mp_decode_str(data, &str_len);
		char *str_copy = region_alloc(region, str_len + 1);
		if (str_copy == NULL) {
			diag_set(OutOfMemory, bytes, "region",
				 "constraint name");
			return -1;
		}
		memcpy(str_copy, str, str_len);
		str_copy[str_len] = 0;
		new_def[i].name = str_copy;
		new_def[i].name_len = str_len;
		new_def[i].type = CONSTR_FKEY;
		if (mp_typeof(**data) != MP_MAP) {
			diag_set(ClientError, errcode, field_no,
				 "foreign key definition "
				 "is expected to be a map");
			return -1;
		}
		uint32_t def_size = mp_decode_map(data);
		bool has_space = false, has_field = false;
		for (size_t j = 0; j < def_size; j++) {
			if (mp_typeof(**data) != MP_STR) {
				diag_set(ClientError, errcode, field_no,
					 "foreign key definition key "
					 "is expected to be a string");
				return -1;
			}
			uint32_t key_len;
			const char *key = mp_decode_str(data, &key_len);
			bool is_space;
			if (key_len == strlen("space") &&
			    memcmp(key, "space", key_len) == 0) {
				is_space = true;
				has_space = true;
			} else if (key_len == strlen("field") &&
				memcmp(key, "field", key_len) == 0) {
				is_space = false;
				has_field = true;
			} else {
				diag_set(ClientError, errcode, field_no,
					 "foreign key definition is expected "
					 "to be {space=.., field=..}");
				return -1;
			}
			int rc;
			struct tuple_constraint_fkey_def *fk = &new_def[i].fkey;
			if (is_space) {
				rc = space_id_decode(data, &fk->space_id,
						     errcode, field_no);
			} else {
				rc = field_id_decode(data, &fk->field, region,
						     errcode, field_no);
			}
			if (rc != 0)
				return rc;
		}
		if (!has_space || !has_field) {
			diag_set(ClientError, errcode, field_no,
				 "foreign key definition is expected "
				 "to be {space=.., field=..}");
			return -1;
		}
	}
	return 0;
}

/**
 * Copy tuple_constraint_field_id object, allocating data on given allocator.
 */
static void
field_id_copy(struct tuple_constraint_field_id *dst,
	      const struct tuple_constraint_field_id *src,
	      struct grp_alloc *all)
{
	dst->id = src->id;
	dst->name_len = src->name_len;
	if (src->name_len != 0)
		dst->name = grp_alloc_create_str(all, src->name, src->name_len);
	else
		dst->name = "";
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
	if (def->type == CONSTR_FKEY) {
		if (def->fkey.field.name_len != 0)
			grp_alloc_reserve_str(all, def->fkey.field.name_len);
	}
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
	dst->type = src->type;
	if (src->type == CONSTR_FUNC) {
		dst->func.id = src->func.id;
	} else {
		assert(src->type == CONSTR_FKEY);
		dst->fkey.space_id = src->fkey.space_id;
		field_id_copy(&dst->fkey.field, &src->fkey.field, all);
	}
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
