#include "sqlInt.h"
#include "mem.h"
#include "box/key_def.h"

/**
 * Build a key definition describing how tuples of a lite ephemeral
 * space must be compared, including per-field collations (e.g.
 * unicode_ci).
 */
static struct key_def *
sql_ephemeral_space_key_def_new(const struct sql_space_info *info)
{
	uint32_t part_count = info->parts == NULL ? info->field_count :
			      info->part_count;
	struct key_part_def *parts =
		sql_xmalloc(part_count * sizeof(struct key_part_def));
	for (uint32_t i = 0; i < part_count; ++i) {
		uint32_t j = info->parts == NULL ? i : info->parts[i];
		parts[i] = key_part_def_default;
		parts[i].fieldno = j;
		enum field_type type = info->types[j];
		parts[i].type = type;
		if (!field_type1_contains_type2(FIELD_TYPE_SCALAR, NULL,
						type, NULL)) {
			const char *err =
				tt_sprintf("field type '%s' is not comparable",
					   field_type_strs[type]);
			diag_set(ClientError, ER_SQL_EXECUTE, err);
			sql_xfree(parts);
			return NULL;
		}
		parts[i].coll_id = info->coll_ids[j];
		parts[i].is_nullable = true;
		parts[i].nullable_action = ON_CONFLICT_ACTION_NONE;
	}
	struct key_def *key_def = key_def_new(parts, part_count, 0);
	sql_xfree(parts);
	return key_def;
}

struct sql_ephemeral_space *
sql_ephemeral_new(const struct sql_space_info *info)
{
	struct key_def *key_def = sql_ephemeral_space_key_def_new(info);
	if (key_def == NULL)
		return NULL;
	struct sql_ephemeral_space *eph_space =
		sql_xmalloc(sizeof(struct sql_ephemeral_space));
	eph_space->field_count = info->field_count;
	eph_space->header = info->types;
	eph_space->tuple_count = 0;
	eph_space->max_tuple_count = 0;
	eph_space->key_def = key_def;
	return eph_space;
}

int
sql_ephemeral_insert(struct sql_ephemeral_space *space, const char *tuple,
		     const char *tuple_end)
{
	if (space->max_tuple_count == 0) {
		space->tuples = sql_xmalloc(sizeof(char *));
		space->tuple_sizes = sql_xmalloc(sizeof(uint32_t));
		space->max_tuple_count++;
	} else if (space->max_tuple_count == space->tuple_count) {
		space->max_tuple_count *= 2;
		space->tuples = sql_xrealloc(space->tuples,
					     space->max_tuple_count *
					     sizeof(char *));
		space->tuple_sizes = sql_xrealloc(space->tuple_sizes,
						  space->max_tuple_count *
						  sizeof(char *));
	}
	uint32_t size_tuple = tuple_end - tuple;
	space->tuples[space->tuple_count] = sql_xmalloc(size_tuple);
	memcpy(space->tuples[space->tuple_count], tuple, size_tuple);
	space->tuple_sizes[space->tuple_count] = size_tuple;
	space->tuple_count++;
	return 0;
}

int
sql_ephemeral_find(struct sql_ephemeral_space *space, struct UnpackedRecord *key)
{
	for (uint32_t i = 0; i < space->tuple_count; i++) {
		if (sqlVdbeRecordCompareMsgpack(space->tuples[i], key) == 0)
			return i;
	}
	return -1;
}

void
sql_ephemeral_delete(struct sql_ephemeral_space *eph_space)
{
	if (eph_space == NULL)
		return;
	for (uint32_t i = 0; i < eph_space->tuple_count; i++) {
		sql_xfree(eph_space->tuples[i]);
	}
	if (eph_space->max_tuple_count > 0) {
		sql_xfree(eph_space->tuples);
		sql_xfree(eph_space->tuple_sizes);
	}
	key_def_delete(eph_space->key_def);
	sql_xfree(eph_space);
}
