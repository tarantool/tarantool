#include "trivia/util.h"
#include "vy_iterators_helper.h"
#include "vy_history.h"
#include "fiber.h"
#include "unit.h"

const struct vy_stmt_template key_template = STMT_TEMPLATE(0, SELECT, vyend);

static void
test_basic()
{
	header();
	plan(6);
	struct vy_cache cache;
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def;
	struct tuple_format *format;
	create_test_cache(fields, types, lengthof(fields), &cache, &key_def,
			  &format);
	struct tuple *select_all = vy_new_simple_stmt(format, NULL,
						      &key_template);

	struct mempool history_node_pool;
	mempool_create(&history_node_pool, cord_slab_cache(),
		       sizeof(struct vy_history_node));

	/*
	 * Fill the cache with 3 chains.
	 */
	const struct vy_stmt_template chain1[] = {
		STMT_TEMPLATE(1, REPLACE, 100),
		STMT_TEMPLATE(2, REPLACE, 200),
		STMT_TEMPLATE(3, REPLACE, 300),
		STMT_TEMPLATE(4, REPLACE, 400),
		STMT_TEMPLATE(5, REPLACE, 500),
		STMT_TEMPLATE(6, REPLACE, 600),
	};
	vy_cache_insert_templates_chain(&cache, format, chain1,
					lengthof(chain1), &key_template,
					ITER_GE);
	is(cache.cache_tree.size, 6, "cache is filled with 6 statements");

	const struct vy_stmt_template chain2[] = {
		STMT_TEMPLATE(10, REPLACE, 1001),
		STMT_TEMPLATE(11, REPLACE, 1002),
		STMT_TEMPLATE(12, REPLACE, 1003),
		STMT_TEMPLATE(13, REPLACE, 1004),
		STMT_TEMPLATE(14, REPLACE, 1005),
		STMT_TEMPLATE(15, REPLACE, 1006),
	};
	vy_cache_insert_templates_chain(&cache, format, chain2,
					lengthof(chain2), &key_template,
					ITER_GE);
	is(cache.cache_tree.size, 12, "cache is filled with 12 statements");

	const struct vy_stmt_template chain3[] = {
		STMT_TEMPLATE(16, REPLACE, 1107),
		STMT_TEMPLATE(17, REPLACE, 1108),
		STMT_TEMPLATE(18, REPLACE, 1109),
		STMT_TEMPLATE(19, REPLACE, 1110),
		STMT_TEMPLATE(20, REPLACE, 1111),
		STMT_TEMPLATE(21, REPLACE, 1112),
	};
	vy_cache_insert_templates_chain(&cache, format, chain3,
					lengthof(chain3), &key_template,
					ITER_GE);
	is(cache.cache_tree.size, 18, "cache is filled with 18 statements");

	/*
	 * Try to restore opened and positioned iterator.
	 * At first, start the iterator and make several iteration
	 * steps.
	 * At second, change cache version be insertion a new
	 * statement.
	 * At third, restore the opened on the first step
	 * iterator on the several statements back.
	 *
	 *    Key1   Key2   NewKey   Key3   Key4   Key5
	 *     ^              ^              ^
	 * restore to      new stmt     current position
	 *     |                             |
	 *     +- - - - < - - - - < - - - - -+
	 */
	struct vy_cache_iterator itr;
	struct vy_read_view rv;
	rv.vlsn = INT64_MAX;
	const struct vy_read_view *rv_p = &rv;
	vy_cache_iterator_open(&itr, &cache, ITER_GE, select_all, &rv_p);

	/* Start iterator and make several steps. */
	struct tuple *ret;
	bool unused;
	struct vy_history history;
	vy_history_create(&history, &history_node_pool);
	for (int i = 0; i < 4; ++i)
		vy_cache_iterator_next(&itr, &history, &unused);
	ret = vy_history_last_stmt(&history);
	ok(vy_stmt_are_same(ret, &chain1[3], format, NULL),
	   "next_key * 4");

	/*
	 * Emulate new statement insertion: break the first chain
	 * and insert into the cache the new statement.
	 */
	const struct vy_stmt_template to_insert =
		STMT_TEMPLATE(22, REPLACE, 201);
	vy_cache_on_write_template(&cache, format, &to_insert);
	vy_cache_insert_templates_chain(&cache, format, &to_insert, 1,
					&key_template, ITER_GE);

	/*
	 * Restore after the cache had changed. Restoration
	 * makes position of the iterator be one statement after
	 * the last_stmt. So restore on chain1[0], but the result
	 * must be chain1[1].
	 */
	struct tuple *last_stmt = vy_new_simple_stmt(format, NULL, &chain1[0]);
	ok(vy_cache_iterator_restore(&itr, last_stmt, &history, &unused) >= 0,
	   "restore");
	ret = vy_history_last_stmt(&history);
	ok(vy_stmt_are_same(ret, &chain1[1], format, NULL),
	   "restore on position after last");
	tuple_unref(last_stmt);

	vy_history_cleanup(&history);
	vy_cache_iterator_close(&itr);

	mempool_destroy(&history_node_pool);
	tuple_unref(select_all);
	destroy_test_cache(&cache, key_def, format);
	check_plan();
	footer();
}

int
main()
{
	vy_iterator_C_test_init(1LLU * 1024LLU * 1024LLU * 1024LLU);

	test_basic();

	vy_iterator_C_test_finish();
	return 0;
}
