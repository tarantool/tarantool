#include "trivia/util.h"
#include "vy_iterators_helper.h"
#include "fiber.h"

const struct vy_stmt_template key_template = STMT_TEMPLATE(0, SELECT, vyend);

static void
test_basic(void)
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
	struct vy_entry select_all = vy_new_simple_stmt(format, key_def,
							&key_template);

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
	is(vy_cache_tree_size(&cache.cache_tree), 6,
	   "cache is filled with 6 statements");

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
	is(vy_cache_tree_size(&cache.cache_tree), 12,
	   "cache is filled with 12 statements");

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
	is(vy_cache_tree_size(&cache.cache_tree), 18,
	   "cache is filled with 18 statements");

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
	vy_cache_iterator_open(&itr, &cache, ITER_GE, select_all, &rv_p,
			       /*is_prepared_ok=*/true);

	/* Start iterator and make several steps. */
	struct vy_entry ret;
	bool unused;
	struct vy_history history;
	vy_history_create(&history, &history_node_pool);
	for (int i = 0; i < 4; ++i)
		vy_cache_iterator_next(&itr, &history, &unused);
	ret = vy_history_last_stmt(&history);
	ok(vy_stmt_are_same(ret, &chain1[3], format, key_def),
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
	 * the last. So restore on chain1[0], but the result
	 * must be chain1[1].
	 */
	struct vy_entry last = vy_new_simple_stmt(format, key_def, &chain1[0]);
	ok(vy_cache_iterator_restore(&itr, last, &history, &unused) >= 0,
	   "restore");
	ret = vy_history_last_stmt(&history);
	ok(vy_stmt_are_same(ret, &chain1[1], format, key_def),
	   "restore on position after last");
	tuple_unref(last.stmt);

	vy_history_cleanup(&history);
	vy_cache_iterator_close(&itr);
	tuple_unref(select_all.stmt);
	destroy_test_cache(&cache, key_def, format);
	check_plan();
	footer();
}

static const char *
lsn_str(int64_t lsn)
{
	char *buf = tt_static_buf();
	if (lsn == INT64_MAX) {
		return "INT64_MAX";
	} else if (lsn > MAX_LSN) {
		snprintf(buf, TT_STATIC_BUF_LEN, "MAX_LSN+%lld",
			 (long long)(lsn - MAX_LSN));
	} else {
		snprintf(buf, TT_STATIC_BUF_LEN, "%lld", (long long)lsn);
	}
	return buf;
}

static const char *
iterator_type_str(int type)
{
	switch (type) {
	case ITER_EQ: return "EQ";
	case ITER_GE: return "GE";
	case ITER_GT: return "GT";
	case ITER_LE: return "LE";
	case ITER_LT: return "LT";
	default:
		unreachable();
	}
}

struct test_iterator_expected {
	struct vy_stmt_template stmt;
	bool stop;
};

static void
test_iterator_helper(
		struct vy_cache *cache, struct key_def *key_def,
		struct tuple_format *format, enum iterator_type type,
		const struct vy_stmt_template *key_template,
		int64_t vlsn, bool is_prepared_ok,
		const struct test_iterator_expected *expected,
		int expected_count, bool expected_stop)
{
	struct vy_read_view rv;
	rv.vlsn = vlsn;
	const struct vy_read_view *prv = &rv;
	struct vy_cache_iterator it;
	struct vy_history history;
	vy_history_create(&history, &history_node_pool);
	struct vy_entry key = vy_new_simple_stmt(format, key_def,
						 key_template);
	vy_cache_iterator_open(&it, cache, type, key, &prv, is_prepared_ok);
	int i;
	bool stop;
	for (i = 0; ; i++) {
		stop = false;
		fail_unless(vy_cache_iterator_next(&it, &history, &stop) == 0);
		struct vy_entry entry = vy_history_last_stmt(&history);
		if (vy_entry_is_equal(entry, vy_entry_none()))
			break;
		ok(i < expected_count && stop == expected[i].stop &&
		   vy_stmt_are_same(entry, &expected[i].stmt, format, key_def),
		   "type=%s key=%s vlsn=%s stmt=%s stop=%s",
		   iterator_type_str(type), tuple_str(key.stmt), lsn_str(vlsn),
		   vy_stmt_str(entry.stmt), stop ? "true" : "false");
	}
	ok(i == expected_count && stop == expected_stop,
	   "type=%s key=%s vlsn=%s eof stop=%s",
	   iterator_type_str(type), tuple_str(key.stmt), lsn_str(vlsn),
	   stop ? "true" : "false");
	vy_cache_iterator_close(&it);
	vy_history_cleanup(&history);
	tuple_unref(key.stmt);
}

static void
test_iterator_skip_prepared(void)
{
	header();
	plan(34);
	struct vy_cache cache;
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def;
	struct tuple_format *format;
	create_test_cache(fields, types, lengthof(fields), &cache, &key_def,
			  &format);
	struct vy_stmt_template chain[] = {
		STMT_TEMPLATE(10, REPLACE, 100),
		STMT_TEMPLATE(20, REPLACE, 200),
		STMT_TEMPLATE(MAX_LSN + 10, REPLACE, 300),
		STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 400),
		STMT_TEMPLATE(15, REPLACE, 500),
		STMT_TEMPLATE(25, REPLACE, 600),
		STMT_TEMPLATE(MAX_LSN + 15, REPLACE, 700),
	};
	vy_cache_insert_templates_chain(&cache, format, chain, lengthof(chain),
					&key_template, ITER_GE);
	/* type=GE vlsn=20 is_prepared_ok=false */
	{
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(10, REPLACE, 100), true},
			{STMT_TEMPLATE(20, REPLACE, 200), true},
			{STMT_TEMPLATE(15, REPLACE, 500), false},
		};
		test_iterator_helper(&cache, key_def, format, ITER_GE,
				     &key_template, /*vlsn=*/20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*expected_stop=*/false);
	}
	/* type=GE vlsn=MAX_LSN+10 is_prepared_ok=false */
	{
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(10, REPLACE, 100), true},
			{STMT_TEMPLATE(20, REPLACE, 200), true},
			{STMT_TEMPLATE(15, REPLACE, 500), false},
			{STMT_TEMPLATE(25, REPLACE, 600), true},
		};
		test_iterator_helper(&cache, key_def, format, ITER_GE,
				     &key_template, /*vlsn=*/MAX_LSN + 10,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*expected_stop=*/false);
	}
	/* type=GE vlsn=MAX_LSN+10 is_prepared_ok=true */
	{
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(10, REPLACE, 100), true},
			{STMT_TEMPLATE(20, REPLACE, 200), true},
			{STMT_TEMPLATE(MAX_LSN + 10, REPLACE, 300), true},
			{STMT_TEMPLATE(15, REPLACE, 500), false},
			{STMT_TEMPLATE(25, REPLACE, 600), true},
		};
		test_iterator_helper(&cache, key_def, format, ITER_GE,
				     &key_template, /*vlsn=*/MAX_LSN + 10,
				     /*is_prepared_ok=*/true,
				     expected, lengthof(expected),
				     /*expected_stop=*/false);
	}
	/* type=LE vlsn=20 is_prepared_ok=false */
	{
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(15, REPLACE, 500), false},
			{STMT_TEMPLATE(20, REPLACE, 200), false},
			{STMT_TEMPLATE(10, REPLACE, 100), true},
		};
		test_iterator_helper(&cache, key_def, format, ITER_LE,
				     &key_template, /*vlsn=*/20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*expected_stop=*/true);
	}
	/* type=LE vlsn=MAX_LSN+10 is_prepared_ok=false */
	{
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(25, REPLACE, 600), false},
			{STMT_TEMPLATE(15, REPLACE, 500), true},
			{STMT_TEMPLATE(20, REPLACE, 200), false},
			{STMT_TEMPLATE(10, REPLACE, 100), true},
		};
		test_iterator_helper(&cache, key_def, format, ITER_LE,
				     &key_template, /*vlsn=*/MAX_LSN + 10,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*expected_stop=*/true);
	}
	/* type=LE vlsn=MAX_LSN+10 is_prepared_ok=true */
	{
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(25, REPLACE, 600), false},
			{STMT_TEMPLATE(15, REPLACE, 500), true},
			{STMT_TEMPLATE(MAX_LSN + 10, REPLACE, 300), false},
			{STMT_TEMPLATE(20, REPLACE, 200), true},
			{STMT_TEMPLATE(10, REPLACE, 100), true},
		};
		test_iterator_helper(&cache, key_def, format, ITER_LE,
				     &key_template, /*vlsn=*/MAX_LSN + 10,
				     /*is_prepared_ok=*/true,
				     expected, lengthof(expected),
				     /*expected_stop=*/true);
	}
	/* type=EQ key=300 vlsn=20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 300);
		struct test_iterator_expected expected[] = {};
		test_iterator_helper(&cache, key_def, format, ITER_EQ,
				     &key, /*vlsn=*/20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*expected_stop=*/false);
	}
	/* type=EQ key=300 vlsn=MAX_LSN+10 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 300);
		struct test_iterator_expected expected[] = {};
		test_iterator_helper(&cache, key_def, format, ITER_EQ,
				     &key, /*vlsn=*/MAX_LSN + 10,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*expected_stop=*/false);
	}
	/* type=EQ key=300 vlsn=MAX_LSN+10 is_prepared_ok=true */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 300);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(MAX_LSN + 10, REPLACE, 300), true},
		};
		test_iterator_helper(&cache, key_def, format, ITER_EQ,
				     &key, /*vlsn=*/MAX_LSN + 10,
				     /*is_prepared_ok=*/true,
				     expected, lengthof(expected),
				     /*expected_stop=*/true);
	}
	destroy_test_cache(&cache, key_def, format);
	footer();
	check_plan();
}

int
main(void)
{
	vy_iterator_C_test_init(1LLU * 1024LLU * 1024LLU * 1024LLU);

	plan(2);

	test_basic();
	test_iterator_skip_prepared();

	vy_iterator_C_test_finish();
	return check_plan();
}
