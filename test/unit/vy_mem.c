#include "memory.h"
#include "fiber.h"
#include <small/slab_cache.h>
#include "vy_iterators_helper.h"

void
test_basic(void)
{
	header();

	plan(12);

	/* Create key_def */
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def = box_key_def_new(fields, types, 1);
	assert(key_def != NULL);
	/* Create lsregion */
	struct lsregion lsregion;
	struct slab_cache *slab_cache = cord_slab_cache();
	lsregion_create(&lsregion, slab_cache->arena);
	struct vy_mem *mem = create_test_mem(&lsregion, key_def);

	is(mem->min_lsn, INT64_MAX, "mem->min_lsn on empty mem");
	is(mem->max_lsn, -1, "mem->max_lsn on empty mem");
	const struct vy_stmt_template stmts[] = {
		STMT_TEMPLATE(100, REPLACE, 1), STMT_TEMPLATE(101, REPLACE, 1),
		STMT_TEMPLATE(102, REPLACE, 1), STMT_TEMPLATE(103, REPLACE, 1),
		STMT_TEMPLATE(104, REPLACE, 1)
	};

	/* Check min/max lsn */
	const struct tuple *stmt = vy_mem_insert_template(mem, &stmts[0]);
	is(mem->min_lsn, INT64_MAX, "mem->min_lsn after prepare");
	is(mem->max_lsn, -1, "mem->max_lsn after prepare");
	vy_mem_commit_stmt(mem, stmt);
	is(mem->min_lsn, 100, "mem->min_lsn after commit");
	is(mem->max_lsn, 100, "mem->max_lsn after commit");

	/* Check vy_mem_older_lsn */
	const struct tuple *older = stmt;
	stmt = vy_mem_insert_template(mem, &stmts[1]);
	is(vy_mem_older_lsn(mem, stmt), older, "vy_mem_older_lsn 1");
	is(vy_mem_older_lsn(mem, older), NULL, "vy_mem_older_lsn 2");
	vy_mem_commit_stmt(mem, stmt);

	/* Check rollback  */
	const struct tuple *olderolder = stmt;
	older = vy_mem_insert_template(mem, &stmts[2]);
	stmt = vy_mem_insert_template(mem, &stmts[3]);
	is(vy_mem_older_lsn(mem, stmt), older, "vy_mem_rollback 1");
	vy_mem_rollback_stmt(mem, older);
	is(vy_mem_older_lsn(mem, stmt), olderolder, "vy_mem_rollback 2");

	/* Check version  */
	stmt = vy_mem_insert_template(mem, &stmts[4]);
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
