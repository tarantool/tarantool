#include "box/tuple.h"

#include "unit.h"

#include "memory.h"
#include "fiber.h"
#include "vy_stmt.h"
#include "vy_mem.h"
#include <small/slab_cache.h>
#include <small/lsregion.h>

static struct tuple *
vy_mem_insert_helper(struct vy_mem *mem, int key, int64_t lsn)
{
	char data[16];
	char *end = data;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, key);
	assert(end <= data + sizeof(data));
	struct tuple *stmt = vy_stmt_new_replace(mem->format, data, end);
	assert(stmt != NULL);
	vy_stmt_set_lsn(stmt, lsn);
	struct tuple *region_stmt = vy_stmt_dup_lsregion(stmt, mem->allocator,
							 mem->generation);
	assert(region_stmt != NULL);
	tuple_unref(stmt);
	vy_mem_insert(mem, region_stmt);
	return region_stmt;
}

void
test_basic(void)
{
	header();

	plan(13);

	/* Create key_def */
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def = box_key_def_new(fields, types, 1);
	assert(key_def != NULL);

	/* Create format */
	struct tuple_format *format = tuple_format_new(&vy_tuple_format_vtab,
							&key_def, 1, 0);
	assert(format != NULL);

	/* Create format with column mask */
	struct tuple_format *format_with_colmask =
		vy_tuple_format_new_with_colmask(format);
	assert(format_with_colmask != NULL);

	/* Create upsert format */
	struct tuple_format *format_upsert =
		vy_tuple_format_new_upsert(format);
	assert(format_upsert != NULL);

	/* Create lsregion */
	struct lsregion lsregion;
	struct slab_cache *slab_cache = cord_slab_cache();
	lsregion_create(&lsregion, slab_cache->arena);

	/* Create mem */
	int64_t generation = 1;
	struct vy_mem *mem = vy_mem_new(&lsregion, generation, key_def,
					format, format_with_colmask,
					format_upsert, 0);
	ok(mem != NULL, "vy_mem_new");
	is(mem->min_lsn, INT64_MAX, "mem->min_lsn on empty mem");
	is(mem->max_lsn, -1, "mem->max_lsn on empty mem");

	/* Check min/max lsn */
	struct tuple *stmt;
	stmt = vy_mem_insert_helper(mem, 1, 100);
	is(mem->min_lsn, INT64_MAX, "mem->min_lsn after prepare");
	is(mem->max_lsn, -1, "mem->max_lsn after prepare");
	vy_mem_commit_stmt(mem, stmt);
	is(mem->min_lsn, 100, "mem->min_lsn after commit");
	is(mem->max_lsn, 100, "mem->max_lsn after commit");

	/* Check vy_mem_older_lsn */
	struct tuple *older = stmt;
	stmt = vy_mem_insert_helper(mem, 1, 101);
	is(vy_mem_older_lsn(mem, stmt), older, "vy_mem_older_lsn 1");
	is(vy_mem_older_lsn(mem, older), NULL, "vy_mem_older_lsn 2");
	vy_mem_commit_stmt(mem, stmt);

	/* Check rollback  */
	struct tuple *olderolder = stmt;
	older = vy_mem_insert_helper(mem, 1, 102);
	stmt = vy_mem_insert_helper(mem, 1, 103);
	is(vy_mem_older_lsn(mem, stmt), older, "vy_mem_rollback 1");
	vy_mem_rollback_stmt(mem, older);
	is(vy_mem_older_lsn(mem, stmt), olderolder, "vy_mem_rollback 2");

	/* Check version  */
	stmt = vy_mem_insert_helper(mem, 1, 104);
	is(mem->version, 6, "vy_mem->version")
	vy_mem_commit_stmt(mem, stmt);
	is(mem->version, 6, "vy_mem->version")

	/* Clean up */
	vy_mem_delete(mem);
	lsregion_destroy(&lsregion);
	box_key_def_delete(key_def);

	fiber_gc();
	footer();

	check_plan();
}

int
main(int argc, char *argv[])
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init();

	test_basic();

	tuple_free();
	fiber_free();
	memory_free();
	return 0;
}
