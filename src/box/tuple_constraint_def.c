/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tuple_constraint_def.h"

#include "diag.h"
#include "error.h"
#include "schema_def.h"
#include "identifier.h"
#include "trivia/util.h"
#include "salad/grp_alloc.h"
#include "small/region.h"
#include "msgpuck.h"
#include "tt_static.h"

#include <PMurHash.h>

const char *tuple_constraint_type_strs[] = {
	/* [CONSTR_FUNC]        = */ "constraint",
	/* [CONSTR_FKEY]        = */ "foreign_key",
};

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
	if (def1->field_mapping_size != def2->field_mapping_size)
		return def1->field_mapping_size < def2->field_mapping_size ? -1
									   : 1;
	if (def1->field_mapping_size == 0)
		return field_id_cmp(&def1->field, &def2->field);

	for (uint32_t i = 0; i < def1->field_mapping_size; i++) {
		int rc;
		rc = field_id_cmp(&def1->field_mapping[i].local_field,
				  &def2->field_mapping[i].local_field);
		if (rc != 0)
			return rc;
		rc = field_id_cmp(&def1->field_mapping[i].foreign_field,
				  &def2->field_mapping[i].foreign_field);
		if (rc != 0)
			return rc;
	}
	return 0;
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

#define CONSTRAINT_DEF_MEMBER_HASH(member)				   \
do {									   \
	PMurHash32_Process(ph, pcarry, &def->member, sizeof(def->member)); \
	size += sizeof(def->member);					   \
} while (0)

/**
 * Compute the field identifier's hash with `PMurHash32_Process` and return the
 * size of the data processed.
 */
static uint32_t
field_id_hash_process(const struct tuple_constraint_field_id *def,
		      uint32_t *ph, uint32_t *pcarry)
{
	uint32_t size = 0;
	CONSTRAINT_DEF_MEMBER_HASH(id);
	PMurHash32_Process(ph, pcarry, def->name, (int)def->name_len);
	size += def->name_len;
	return size;
}

/**
 * Compute the foreign key definition's hash with `PMurHash32_Process` and
 * return the size of the data processed.
 */
static uint32_t
tuple_constraint_def_hash_fkey_process(
	const struct tuple_constraint_fkey_def *def,
	uint32_t *ph, uint32_t *pcarry)
{
	uint32_t size = 0;
	CONSTRAINT_DEF_MEMBER_HASH(space_id);
	if (def->field_mapping_size == 0)
		return size + field_id_hash_process(&def->field, ph, pcarry);

	for (uint32_t i = 0; i < def->field_mapping_size; ++i) {
		size += field_id_hash_process(
			&def->field_mapping[i].local_field, ph, pcarry);
		size += field_id_hash_process(
			&def->field_mapping[i].foreign_field, ph, pcarry);
	}
	return size;
}

uint32_t
tuple_constraint_def_hash_process(const struct tuple_constraint_def *def,
				  uint32_t *ph, uint32_t *pcarry)
{
	uint32_t size = 0;
	PMurHash32_Process(ph, pcarry, def->name, (int)def->name_len);
	size += def->name_len;
	CONSTRAINT_DEF_MEMBER_HASH(type);
	if (def->type == CONSTR_FUNC) {
		CONSTRAINT_DEF_MEMBER_HASH(func.id);
		return size;
	}
	assert(def->type == CONSTR_FKEY);
	return size +
	       tuple_constraint_def_hash_fkey_process(&def->fkey, ph, pcarry);
}

#undef CONSTRAINT_DEF_MEMBER_HASH

int
tuple_constraint_def_decode(const char **data,
			    struct tuple_constraint_def **def, uint32_t *count,
			    struct region *region)
{
	/* Expected normal form of constraints: {name1=func1, name2=func2..}. */
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(IllegalParams,
			 "constraint field is expected to be a MAP");
		return -1;
	}

	uint32_t old_count = *count;
	struct tuple_constraint_def *old_def = *def;
	uint32_t map_size = mp_decode_map(data);
	*count += map_size;
	if (*count == 0)
		return 0;

	*def = xregion_alloc_array(region, struct tuple_constraint_def, *count);
	for (uint32_t i = 0; i < old_count; i++)
		(*def)[i] = old_def[i];
	struct tuple_constraint_def *new_def = *def + old_count;

	for (size_t i = 0; i < map_size; i++)
		new_def[i].type = CONSTR_FUNC;

	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(**data) != MP_STR) {
			diag_set(IllegalParams,
				 "constraint name is expected to be a string");
			return -1;
		}
		uint32_t str_len;
		const char *str = mp_decode_str(data, &str_len);
		if (str_len > BOX_NAME_MAX) {
			diag_set(IllegalParams,
				 "constraint name is too long");
			return -1;
		}
		if (identifier_check(str, str_len) != 0) {
			diag_set(IllegalParams,
				 "constraint name isn't a valid identifier");
			return -1;
		}
		char *str_copy = xregion_alloc(region, str_len + 1);
		memcpy(str_copy, str, str_len);
		str_copy[str_len] = 0;
		new_def[i].name = str_copy;
		new_def[i].name_len = str_len;

		if (mp_typeof(**data) != MP_UINT) {
			diag_set(IllegalParams,
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
space_id_decode(const char **data, uint32_t *space_id)
{
	if (mp_typeof(**data) != MP_UINT) {
		diag_set(IllegalParams,
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
		struct region *region)
{
	if (mp_typeof(**data) == MP_UINT) {
		def->id = mp_decode_uint(data);
		def->name = "";
		def->name_len = 0;
	} else if (mp_typeof(**data) == MP_STR) {
		const char *str = mp_decode_str(data, &def->name_len);
		char *str_copy = xregion_alloc(region, def->name_len + 1);
		memcpy(str_copy, str, def->name_len);
		str_copy[def->name_len] = 0;
		def->name = str_copy;
		def->id = 0;
	} else {
		diag_set(IllegalParams,
			 "foreign key: field must be number or string");
		return -1;
	}
	return 0;
}

/**
 * Helper function of tuple_constraint_def_decode_fkey.
 * Decode foreign key field mapping, that is expected to be MP_MAP with
 * local field (id or name) -> foreign field (id or name) correspondence.
 */
static int
field_mapping_decode(const char **data,
		     struct tuple_constraint_fkey_def *fkey,
		     struct region *region)
{
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(IllegalParams,
			 "field mapping is expected to be a map");
		return -1;
	}
	uint32_t mapping_size = mp_decode_map(data);
	if (mapping_size == 0) {
		diag_set(IllegalParams,
			 "field mapping is expected to be a map");
		return -1;
	}
	fkey->field_mapping_size = mapping_size;
	fkey->field_mapping = xregion_alloc_array(
		region, struct tuple_constraint_fkey_field_mapping,
		mapping_size);
	for (uint32_t i = 0 ; i < 2 * mapping_size; i++) {
		struct tuple_constraint_field_id *def = i % 2 == 0 ?
			&fkey->field_mapping[i / 2].local_field :
			&fkey->field_mapping[i / 2].foreign_field;
		int rc = field_id_decode(data, def, region);
		if (rc != 0)
			return rc;
	}
	return 0;
}

int
tuple_constraint_def_decode_fkey(const char **data,
				 struct tuple_constraint_def **def,
				 uint32_t *count, struct region *region,
				 bool is_complex)
{
	/*
	 * Expected normal form of foreign keys: {name1=data1, name2=data2..},
	 * where dataX has form: {field=id/name} or {space=id, field=id/name}
	 */
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(IllegalParams,
			 "foreign key field is expected to be a MAP");
		return -1;
	}

	uint32_t old_count = *count;
	struct tuple_constraint_def *old_def = *def;
	uint32_t map_size = mp_decode_map(data);
	*count += map_size;
	if (*count == 0)
		return 0;

	*def = xregion_alloc_array(region, struct tuple_constraint_def,
				   *count);
	for (uint32_t i = 0; i < old_count; i++)
		(*def)[i] = old_def[i];
	struct tuple_constraint_def *new_def = *def + old_count;

	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(**data) != MP_STR) {
			diag_set(IllegalParams,
				 "foreign key name is expected to be a string");
			return -1;
		}
		uint32_t str_len;
		const char *str = mp_decode_str(data, &str_len);
		if (str_len > BOX_NAME_MAX) {
			diag_set(IllegalParams,
				 "foreign key name is too long");
			return -1;
		}
		if (identifier_check(str, str_len) != 0) {
			diag_set(IllegalParams,
				 "foreign key name isn't a valid identifier");
			return -1;
		}
		char *str_copy = xregion_alloc(region, str_len + 1);
		memcpy(str_copy, str, str_len);
		str_copy[str_len] = 0;
		new_def[i].name = str_copy;
		new_def[i].name_len = str_len;
		new_def[i].type = CONSTR_FKEY;
		if (mp_typeof(**data) != MP_MAP) {
			diag_set(IllegalParams,
				 "foreign key definition "
				 "is expected to be a map");
			return -1;
		}
		new_def[i].fkey.field_mapping_size = 0;
		uint32_t def_size = mp_decode_map(data);
		bool has_space = false, has_field = false;
		struct tuple_constraint_fkey_def *fk = &new_def[i].fkey;
		for (size_t j = 0; j < def_size; j++) {
			if (mp_typeof(**data) != MP_STR) {
				diag_set(IllegalParams,
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
				diag_set(IllegalParams,
					 "foreign key definition is expected "
					 "to be {[space=..,] field=..}");
				return -1;
			}
			int rc;
			if (is_space)
				rc = space_id_decode(data, &fk->space_id);
			else if (!is_complex)
				rc = field_id_decode(data, &fk->field, region);
			else
				rc = field_mapping_decode(data, fk, region);
			if (rc != 0)
				return rc;
		}
		if (!has_space)
			fk->space_id = 0;
		if (!has_field) {
			diag_set(IllegalParams,
				 "foreign key definition is expected "
				 "to be {[space=..,] field=..}");
			return -1;
		}
	}
	return 0;
}

/**
 * Copy tuple_constraint_field_id object, allocating data on given allocator.
 */
static void
field_id_reserve(const struct tuple_constraint_field_id *def,
		 struct grp_alloc *all)
{
	/* Reservation is required only for strings. */
	if (def->name_len != 0)
		grp_alloc_reserve_str(all, def->name_len);
}

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
 * Reserve memory for field mapping of given constraint definition @a def
 * on an allocator @all.
 */
static void
field_mapping_reserve(const struct tuple_constraint_fkey_def *def,
		      struct grp_alloc *all)
{
	assert(def->field_mapping_size != 0);
	size_t bytes = def->field_mapping_size * sizeof(def->field_mapping[0]);
	grp_alloc_reserve_data(all, bytes);
	for (uint32_t i = 0; i < def->field_mapping_size; i++) {
		const struct tuple_constraint_fkey_field_mapping *f =
			&def->field_mapping[i];
		field_id_reserve(&f->local_field, all);
		field_id_reserve(&f->foreign_field, all);
	}
}

/**
 * Copy field mapping array from one definition to another.
 */
static void
field_mapping_copy(struct tuple_constraint_fkey_def *dst,
		   const struct tuple_constraint_fkey_def *src,
		   struct grp_alloc *all)
{
	assert(src->field_mapping_size != 0);
	dst->field_mapping_size = src->field_mapping_size;
	size_t bytes = src->field_mapping_size * sizeof(dst->field_mapping[0]);
	dst->field_mapping = grp_alloc_create_data(all, bytes);
	for (uint32_t i = 0; i < src->field_mapping_size; i++) {
		struct tuple_constraint_fkey_field_mapping *d =
			&dst->field_mapping[i];
		const struct tuple_constraint_fkey_field_mapping *s =
			&src->field_mapping[i];
		field_id_copy(&d->local_field, &s->local_field, all);
		field_id_copy(&d->foreign_field, &s->foreign_field, all);
	}
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
		if (def->fkey.field_mapping_size == 0)
			field_id_reserve(&def->fkey.field, all);
		else
			field_mapping_reserve(&def->fkey, all);
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
		dst->fkey.field_mapping_size = 0;
		if (src->fkey.field_mapping_size == 0)
			field_id_copy(&dst->fkey.field, &src->fkey.field, all);
		else
			field_mapping_copy(&dst->fkey, &src->fkey, all);
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

int
tuple_constraint_def_array_check(const struct tuple_constraint_def *defs,
				 size_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		const struct tuple_constraint_def *c1 = &defs[i];
		for (uint32_t j = i + 1; j < count; j++) {
			const struct tuple_constraint_def *c2 = &defs[j];
			if (strcmp(c1->name, c2->name) == 0) {
				diag_set(IllegalParams, tt_sprintf(
					"duplicate constraint name '%s'",
					c1->name));
				return -1;
			}
		}
	}
	return 0;
}
