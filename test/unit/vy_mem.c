#include <trivia/config.h>
#include "memory.h"
#include "fiber.h"
#include "vy_iterators_helper.h"

static struct key_def *key_def;
static struct tuple_format *format;

static void
test_basic(void)
{
	header();

	plan(9);

	struct vy_mem *mem = create_test_mem(key_def);
	is(mem->dump_lsn, -1, "mem->dump_lsn on empty mem");
	const struct vy_stmt_template stmts[] = {
		STMT_TEMPLATE(100, REPLACE, 1), STMT_TEMPLATE(101, REPLACE, 1),
		STMT_TEMPLATE(102, REPLACE, 1), STMT_TEMPLATE(103, REPLACE, 1),
		STMT_TEMPLATE(104, REPLACE, 1)
	};

	/* Check dump lsn */
	struct vy_entry entry = vy_mem_insert_template(mem, &stmts[0]);
	is(mem->dump_lsn, -1, "mem->dump_lsn after prepare");
	vy_mem_commit_stmt(mem, entry);
	is(mem->dump_lsn, 100, "mem->dump_lsn after commit");

	/* Check vy_mem_older_lsn */
	struct vy_entry older = entry;
	entry = vy_mem_insert_template(mem, &stmts[1]);
	ok(vy_entry_is_equal(vy_mem_older_lsn(mem, entry), older),
	   "vy_mem_older_lsn 1");
	ok(vy_entry_is_equal(vy_mem_older_lsn(mem, older), vy_entry_none()),
	   "vy_mem_older_lsn 2");
	vy_mem_commit_stmt(mem, entry);

	/* Check rollback  */
	struct vy_entry olderolder = entry;
	older = vy_mem_insert_template(mem, &stmts[2]);
	entry = vy_mem_insert_template(mem, &stmts[3]);
	ok(vy_entry_is_equal(vy_mem_older_lsn(mem, entry), older),
	   "vy_mem_rollback 1");
	vy_mem_rollback_stmt(mem, older);
	ok(vy_entry_is_equal(vy_mem_older_lsn(mem, entry), olderolder),
	   "vy_mem_rollback 2");

	/* Check version  */
	entry = vy_mem_insert_template(mem, &stmts[4]);
	is(mem->version, 8, "vy_mem->version");
	vy_mem_commit_stmt(mem, entry);
	is(mem->version, 9, "vy_mem->version");

	/* Clean up */
	vy_mem_delete(mem);

	footer();

	check_plan();
}

static void
test_iterator_restore_after_insertion(void)
{
	header();

	plan(1);

	struct vy_entry select_key = vy_entry_key_new(stmt_env.key_format,
						      key_def, NULL, 0);
	uint64_t restore_on_value = 20;
	uint64_t restore_on_value_reverse = 60;
	char data[16];
	char *end = data;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, restore_on_value);
	struct vy_entry restore_on_key;
	restore_on_key.stmt = vy_stmt_new_replace(format, data, end);
	restore_on_key.hint = vy_stmt_hint(restore_on_key.stmt, key_def);
	vy_stmt_set_lsn(restore_on_key.stmt, 100);
	end = data;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, restore_on_value_reverse);
	struct vy_entry restore_on_key_reverse;
	restore_on_key_reverse.stmt = vy_stmt_new_replace(format, data, end);
	restore_on_key_reverse.hint = vy_stmt_hint(restore_on_key_reverse.stmt,
						   key_def);
	vy_stmt_set_lsn(restore_on_key_reverse.stmt, 100);

	bool wrong_output = false;
	int i_fail = 0;

	for (uint64_t i = 0; i < ((1000ULL * 3) << 2); i++) {
		uint64_t v = i;
		bool direct = !(v & 1);
		v >>= 1;
		bool has40_50 = v & 1;
		v >>= 1;
		bool has40_150 = v & 1;
		v >>= 1;
		const size_t possible_count = 9;
		uint64_t middle_value = possible_count / 2 * 10; /* 40 */
		bool hasX_100[possible_count]; /* X = 0,10,20,30,40,50,60,70,80 */
		bool addX_100[possible_count]; /* X = 0,10,20,30,40,50,60,70,80 */
		bool add_smth = false;
		for (size_t j = 0; j < possible_count; j++) {
			uint64_t trinity = v % 3;
			v /= 3;
			hasX_100[j] = trinity == 1;
			addX_100[j] = trinity == 2;
			add_smth = add_smth || addX_100[j];
		}
		if (!add_smth)
			continue;
		uint64_t expected_count = 0;
		uint64_t expected_values[possible_count];
		int64_t expected_lsns[possible_count];
		if (direct) {
			for (size_t j = 0; j < possible_count; j++) {
				if (hasX_100[j]) {
					expected_values[expected_count] = j * 10;
					expected_lsns[expected_count] = 100;
					expected_count++;
				} else if (j == possible_count / 2 && has40_50) {
					expected_values[expected_count] = middle_value;
					expected_lsns[expected_count] = 50;
					expected_count++;
				}
			}
		} else {
			for (size_t k = possible_count; k > 0; k--) {
				size_t j = k - 1;
				if (hasX_100[j]) {
					expected_values[expected_count] = j * 10;
					expected_lsns[expected_count] = 100;
					expected_count++;
				} else if (j == possible_count / 2 && has40_50) {
					expected_values[expected_count] = middle_value;
					expected_lsns[expected_count] = 50;
					expected_count++;
				}
			}
		}

		/* Create mem */
		struct vy_mem *mem = create_test_mem(key_def);
		if (has40_50) {
			const struct vy_stmt_template temp =
				STMT_TEMPLATE(50, REPLACE, 40);
			vy_mem_insert_template(mem, &temp);
		}
		if (has40_150) {
			const struct vy_stmt_template temp =
				STMT_TEMPLATE(150, REPLACE, 40);
			vy_mem_insert_template(mem, &temp);
		}
		for (size_t j = 0; j < possible_count; j++) {
			if (hasX_100[j]) {
				const struct vy_stmt_template temp =
					STMT_TEMPLATE(100, REPLACE, j * 10);
				vy_mem_insert_template(mem, &temp);
			}
		}

		struct vy_mem_iterator itr;
		struct vy_mem_iterator_stat stats = {0, {0, 0}};
		struct vy_read_view rv;
		rv.vlsn = 100;
		const struct vy_read_view *prv = &rv;
		vy_mem_iterator_open(&itr, &stats, mem,
				     direct ? ITER_GE : ITER_LE, select_key,
				     &prv, /*is_prepared_ok=*/true);
		struct vy_entry e;
		struct vy_history history;
		vy_history_create(&history, &history_node_pool);
		int rc = vy_mem_iterator_next(&itr, &history);
		e = vy_history_last_stmt(&history);
		fail_unless(rc == 0);
		size_t j = 0;
		while (e.stmt != NULL) {
			if (j >= expected_count) {
				wrong_output = true;
				break;
			}
			uint32_t val = 42;
			tuple_field_u32(e.stmt, 0, &val);
			if (val != expected_values[j] ||
			    vy_stmt_lsn(e.stmt) != expected_lsns[j]) {
				wrong_output = true;
				break;
			}
			j++;
			if (direct && val >= middle_value)
				break;
			else if(!direct && val <= middle_value)
				break;
			int rc = vy_mem_iterator_next(&itr, &history);
			e = vy_history_last_stmt(&history);
			fail_unless(rc == 0);
		}
		if (e.stmt == NULL && j != expected_count)
			wrong_output = true;
		if (wrong_output) {
			i_fail = i;
			break;
		}


		for (size_t j = 0; j < possible_count; j++) {
			if (addX_100[j]) {
				const struct vy_stmt_template temp =
					STMT_TEMPLATE(100, REPLACE, j * 10);
				vy_mem_insert_template(mem, &temp);
			}
		}

		expected_count = 0;
		if (direct) {
			for (size_t j = 0; j < possible_count; j++) {
				if (j * 10 <= restore_on_value)
					continue;
				if (hasX_100[j] || addX_100[j]) {
					expected_values[expected_count] = j * 10;
					expected_lsns[expected_count] = 100;
					expected_count++;
				} else if (j == possible_count / 2 && has40_50) {
					expected_values[expected_count] = middle_value;
					expected_lsns[expected_count] = 50;
					expected_count++;
				}
			}
		} else {
			for (size_t k = possible_count; k > 0; k--) {
				size_t j = k - 1;
				if (j * 10 >= restore_on_value_reverse)
					continue;
				if (hasX_100[j] || addX_100[j]) {
					expected_values[expected_count] = j * 10;
					expected_lsns[expected_count] = 100;
					expected_count++;
				} else if (j == possible_count / 2 && has40_50) {
					expected_values[expected_count] = middle_value;
					expected_lsns[expected_count] = 50;
					expected_count++;
				}
			}
		}

		if (direct)
			rc = vy_mem_iterator_restore(&itr, restore_on_key, &history);
		else
			rc = vy_mem_iterator_restore(&itr, restore_on_key_reverse, &history);
		e = vy_history_last_stmt(&history);

		j = 0;
		while (e.stmt != NULL) {
			if (j >= expected_count) {
				wrong_output = true;
				break;
			}
			uint32_t val = 42;
			tuple_field_u32(e.stmt, 0, &val);
			if (val != expected_values[j] ||
			    vy_stmt_lsn(e.stmt) != expected_lsns[j]) {
				wrong_output = true;
				break;
			}
			j++;
			int rc = vy_mem_iterator_next(&itr, &history);
			e = vy_history_last_stmt(&history);
			fail_unless(rc == 0);
		}
		if (j != expected_count)
			wrong_output = true;
		if (wrong_output) {
			i_fail = i;
			break;
		}

		vy_history_cleanup(&history);
		vy_mem_delete(mem);
	}

	ok(!wrong_output, "check wrong_output %d", i_fail);

	/* Clean up */
	tuple_unref(select_key.stmt);
	tuple_unref(restore_on_key.stmt);
	tuple_unref(restore_on_key_reverse.stmt);

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
	int64_t min_skipped_plsn;
};

static void
test_iterator_helper(
		struct vy_mem *mem, enum iterator_type type,
		const struct vy_stmt_template *key_template,
		int64_t vlsn, bool is_prepared_ok,
		const struct test_iterator_expected *expected,
		int expected_count, int64_t min_skipped_plsn)
{
	struct vy_read_view rv;
	rv.vlsn = vlsn;
	const struct vy_read_view *prv = &rv;
	struct vy_mem_iterator it;
	struct vy_mem_iterator_stat stat;
	memset(&stat, 0, sizeof(stat));
	struct vy_history history;
	vy_history_create(&history, &history_node_pool);
	struct vy_entry key = vy_new_simple_stmt(format, key_def,
						 key_template);
	vy_mem_iterator_open(&it, &stat, mem, type, key, &prv, is_prepared_ok);
	int i;
	for (i = 0; ; i++) {
		fail_unless(vy_mem_iterator_next(&it, &history) == 0);
		struct vy_entry entry = vy_history_last_stmt(&history);
		if (vy_entry_is_equal(entry, vy_entry_none()))
			break;
		ok(i < expected_count &&
		   it.min_skipped_plsn == expected[i].min_skipped_plsn &&
		   vy_stmt_are_same(entry, &expected[i].stmt, format, key_def),
		   "type=%s key=%s vlsn=%s min_skipped_plsn=%s stmt=%s",
		   iterator_type_str(type), tuple_str(key.stmt), lsn_str(vlsn),
		   lsn_str(it.min_skipped_plsn), vy_stmt_str(entry.stmt));
	}
	ok(i == expected_count && it.min_skipped_plsn == min_skipped_plsn,
	   "type=%s key=%s vlsn=%s min_skipped_plsn=%s eof",
	   iterator_type_str(type), tuple_str(key.stmt), lsn_str(vlsn),
	   lsn_str(it.min_skipped_plsn));
	vy_mem_iterator_close(&it);
	vy_history_cleanup(&history);
	tuple_unref(key.stmt);
}

static void
test_iterator_skip_prepared(void)
{
	header();
	plan(44);
	struct vy_stmt_template stmt_templates[] = {
		STMT_TEMPLATE(10, REPLACE, 100, 1),
		STMT_TEMPLATE(20, REPLACE, 100, 2),
		STMT_TEMPLATE(MAX_LSN + 10, REPLACE, 100, 3),
		STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 100, 4),
		STMT_TEMPLATE(15, REPLACE, 200, 1),
		STMT_TEMPLATE(25, REPLACE, 200, 2),
		STMT_TEMPLATE(MAX_LSN + 15, REPLACE, 300, 1),
		STMT_TEMPLATE(MAX_LSN + 5, REPLACE, 400, 1),
		STMT_TEMPLATE(MAX_LSN + 25, REPLACE, 400, 2),
		STMT_TEMPLATE_FLAGS(10, REPLACE, VY_STMT_SKIP_READ, 500, 1),
		STMT_TEMPLATE_FLAGS(15, REPLACE, VY_STMT_SKIP_READ, 500, 2),
		STMT_TEMPLATE_FLAGS(5, REPLACE, VY_STMT_SKIP_READ, 600, 1),
		STMT_TEMPLATE(10, REPLACE, 600, 2),
		STMT_TEMPLATE_FLAGS(15, REPLACE, VY_STMT_SKIP_READ, 600, 3),
		STMT_TEMPLATE(30, REPLACE, 600, 4),
		STMT_TEMPLATE_FLAGS(45, REPLACE, VY_STMT_SKIP_READ, 600, 5),
		STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 600, 5),
	};
	struct vy_mem *mem = create_test_mem(key_def);
	for (int i = 0; i < (int)lengthof(stmt_templates); i++) {
		vy_mem_insert_template(mem, &stmt_templates[i]);
	}
	/* type=GE key=100 vlsn=20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 100);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(20, REPLACE, 100, 2), INT64_MAX},
			{STMT_TEMPLATE(15, REPLACE, 200, 1), INT64_MAX},
			{STMT_TEMPLATE(10, REPLACE, 600, 2), INT64_MAX},
		};
		test_iterator_helper(mem, ITER_GE, &key, /*vlsn=*/20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=GE key=100 vlsn=MAX_LSN+1 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 100);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(20, REPLACE, 100, 2), INT64_MAX},
			{STMT_TEMPLATE(25, REPLACE, 200, 2), INT64_MAX},
			{STMT_TEMPLATE(30, REPLACE, 600, 4), INT64_MAX},
		};
		test_iterator_helper(mem, ITER_GE, &key, /*vlsn=*/MAX_LSN + 1,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=GE key=100 vlsn=MAX_LSN+20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 100);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(20, REPLACE, 100, 2), MAX_LSN + 10},
			{STMT_TEMPLATE(25, REPLACE, 200, 2), MAX_LSN + 10},
			{STMT_TEMPLATE(30, REPLACE, 600, 4), MAX_LSN + 5},
		};
		test_iterator_helper(mem, ITER_GE, &key, /*vlsn=*/MAX_LSN + 20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/MAX_LSN + 5);
	}
	/* type=GE key=100 vlsn=MAX_LSN+20 is_prepared_ok=true */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 100);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 100, 4),
				INT64_MAX},
			{STMT_TEMPLATE(25, REPLACE, 200, 2), INT64_MAX},
			{STMT_TEMPLATE(MAX_LSN + 15, REPLACE, 300, 1),
				INT64_MAX},
			{STMT_TEMPLATE(MAX_LSN + 5, REPLACE, 400, 1),
				INT64_MAX},
			{STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 600, 5),
				INT64_MAX},
		};
		test_iterator_helper(mem, ITER_GE, &key, /*vlsn=*/MAX_LSN + 20,
				     /*is_prepared_ok=*/true,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=LT key=1000 vlsn=20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 1000);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(10, REPLACE, 600, 2), INT64_MAX},
			{STMT_TEMPLATE(15, REPLACE, 200, 1), INT64_MAX},
			{STMT_TEMPLATE(20, REPLACE, 100, 2), INT64_MAX},
		};
		test_iterator_helper(mem, ITER_LT, &key, /*vlsn=*/20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=LT key=1000 vlsn=MAX_LSN+1 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 1000);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(30, REPLACE, 600, 4), INT64_MAX},
			{STMT_TEMPLATE(25, REPLACE, 200, 2), INT64_MAX},
			{STMT_TEMPLATE(20, REPLACE, 100, 2), INT64_MAX},
		};
		test_iterator_helper(mem, ITER_LT, &key, /*vlsn=*/MAX_LSN + 1,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=LT key=1000 vlsn=MAX_LSN+20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 1000);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(30, REPLACE, 600, 4), MAX_LSN + 20},
			{STMT_TEMPLATE(25, REPLACE, 200, 2), MAX_LSN + 5},
			{STMT_TEMPLATE(20, REPLACE, 100, 2), MAX_LSN + 5},
		};
		test_iterator_helper(mem, ITER_LT, &key, /*vlsn=*/MAX_LSN + 20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/MAX_LSN + 5);
	}
	/* type=LT key=1000 vlsn=MAX_LSN+20 is_prepared_ok=true */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 1000);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 600, 5),
				INT64_MAX},
			{STMT_TEMPLATE(MAX_LSN + 5, REPLACE, 400, 1),
				INT64_MAX},
			{STMT_TEMPLATE(MAX_LSN + 15, REPLACE, 300, 1),
				INT64_MAX},
			{STMT_TEMPLATE(25, REPLACE, 200, 2), INT64_MAX},
			{STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 100, 4),
				INT64_MAX},
		};
		test_iterator_helper(mem, ITER_LT, &key, /*vlsn=*/MAX_LSN + 20,
				     /*is_prepared_ok=*/true,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=EQ key=600 vlsn=20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 600);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(10, REPLACE, 600, 2), INT64_MAX},
		};
		test_iterator_helper(mem, ITER_EQ, &key, /*vlsn=*/20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=EQ key=600 vlsn=MAX_LSN+1 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 600);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(30, REPLACE, 600, 4), INT64_MAX},
		};
		test_iterator_helper(mem, ITER_EQ, &key, /*vlsn=*/MAX_LSN + 1,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	/* type=EQ key=600 vlsn=MAX_LSN+20 is_prepared_ok=false */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 600);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(30, REPLACE, 600, 4), MAX_LSN + 20},
		};
		test_iterator_helper(mem, ITER_EQ, &key, /*vlsn=*/MAX_LSN + 20,
				     /*is_prepared_ok=*/false,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/MAX_LSN + 20);
	}
	/* type=EQ key=600 vlsn=MAX_LSN+20 is_prepared_ok=true */
	{
		struct vy_stmt_template key = STMT_TEMPLATE(0, SELECT, 600);
		struct test_iterator_expected expected[] = {
			{STMT_TEMPLATE(MAX_LSN + 20, REPLACE, 600, 5),
				INT64_MAX},
		};
		test_iterator_helper(mem, ITER_EQ, &key, /*vlsn=*/MAX_LSN + 20,
				     /*is_prepared_ok=*/true,
				     expected, lengthof(expected),
				     /*min_skipped_plsn=*/INT64_MAX);
	}
	vy_mem_delete(mem);
	footer();
	check_plan();
}

int
main(void)
{
	vy_iterator_C_test_init(0);

	plan(3);

	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	key_def = box_key_def_new(fields, types, 1);
	fail_if(key_def == NULL);
	format = vy_simple_stmt_format_new(&stmt_env, &key_def, 1);
	fail_if(format == NULL);
	tuple_format_ref(format);

	test_basic();
	test_iterator_restore_after_insertion();
	test_iterator_skip_prepared();

	tuple_format_unref(format);
	key_def_delete(key_def);
	vy_iterator_C_test_finish();

	return check_plan();
}
