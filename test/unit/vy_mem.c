#include <trivia/config.h>
#include "memory.h"
#include "fiber.h"
#include "vy_history.h"
#include "vy_iterators_helper.h"

static void
test_basic(void)
{
	header();

	plan(12);

	/* Create key_def */
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def = box_key_def_new(fields, types, 1);
	assert(key_def != NULL);
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
	is(mem->version, 8, "vy_mem->version")
	vy_mem_commit_stmt(mem, entry);
	is(mem->version, 9, "vy_mem->version")

	/* Clean up */
	vy_mem_delete(mem);
	key_def_delete(key_def);

	fiber_gc();
	footer();

	check_plan();
}

static void
test_iterator_restore_after_insertion()
{
	header();

	plan(1);

	/* Create key_def */
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def = box_key_def_new(fields, types, 1);
	assert(key_def != NULL);

	/* Create format */
	struct tuple_format *format = vy_stmt_format_new(&stmt_env, &key_def, 1,
							 NULL, 0, 0, NULL);
	assert(format != NULL);
	tuple_format_ref(format);

	/* Create lsregion */
	struct lsregion lsregion;
	struct slab_cache *slab_cache = cord_slab_cache();
	lsregion_create(&lsregion, slab_cache->arena);

	struct vy_entry select_key = vy_entry_key_new(stmt_env.key_format,
						      key_def, NULL, 0);

	struct mempool history_node_pool;
	mempool_create(&history_node_pool, cord_slab_cache(),
		       sizeof(struct vy_history_node));

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
				     &prv);
		struct vy_entry e;
		struct vy_history history;
		vy_history_create(&history, &history_node_pool);
		int rc = vy_mem_iterator_next(&itr, &history);
		e = vy_history_last_stmt(&history);
		assert(rc == 0);
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
			assert(rc == 0);
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
			assert(rc == 0);
		}
		if (j != expected_count)
			wrong_output = true;
		if (wrong_output) {
			i_fail = i;
			break;
		}

		vy_history_cleanup(&history);
		vy_mem_delete(mem);
		lsregion_gc(&lsregion, 2);
	}

	ok(!wrong_output, "check wrong_output %d", i_fail);

	/* Clean up */
	mempool_destroy(&history_node_pool);

	tuple_unref(select_key.stmt);
	tuple_unref(restore_on_key.stmt);
	tuple_unref(restore_on_key_reverse.stmt);

	tuple_format_unref(format);
	lsregion_destroy(&lsregion);
	key_def_delete(key_def);

	fiber_gc();

	check_plan();

	footer();
}

int
main(int argc, char *argv[])
{
	vy_iterator_C_test_init(0);

	test_basic();
	test_iterator_restore_after_insertion();

	vy_iterator_C_test_finish();
	return 0;
}
