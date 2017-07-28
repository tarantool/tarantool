#include <trivia/config.h>
#include "memory.h"
#include "fiber.h"
#include <small/slab_cache.h>
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

static void
test_iterator_initial_restore()
{
	header();

	plan(3);

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

	const uint64_t count = 100;
	for (uint64_t i = 0; i < count; i++) {
		const struct vy_stmt_template stmts[2] = {
			STMT_TEMPLATE(200, REPLACE, i * 2 + 1),
			STMT_TEMPLATE(300, REPLACE, i * 2 + 1)
		};
		vy_mem_insert_template(mem, &stmts[0]);
		vy_mem_insert_template(mem, &stmts[1]);
	}

	/* initial restore */
	bool wrong_rc = false;
	bool wrong_lsn = false;
	bool wrong_value = false;
	int i_fail = 0;
	for (uint64_t i = 0; i < (count * 2 + 1) * 3; i++) {
		uint64_t key = i % (count * 2 + 1);
		int64_t lsn = (i / (count * 2 + 1)) * 100 + 100;
		bool value_is_expected = lsn != 100 && (key % 2 == 1);
		char data[16];
		char *end = data;
		end = mp_encode_uint(end, key);
		assert(end <= data + sizeof(data));
		struct tuple *stmt = vy_stmt_new_select(mem->format, data, 1);

		struct vy_mem_iterator itr;
		struct vy_mem_iterator_stat stats = {0, {0, 0}};
		struct vy_read_view rv;
		rv.vlsn = lsn;
		const struct vy_read_view *prv = &rv;
		vy_mem_iterator_open(&itr, &stats, mem, ITER_EQ, stmt,
				     &prv, NULL);
		struct tuple *t;
		bool stop = false;
		int rc = itr.base.iface->restore(&itr.base, NULL, &t, &stop);

		if (rc != 0) {
			wrong_rc = true;
			i_fail = i;
			itr.base.iface->cleanup(&itr.base);
			itr.base.iface->close(&itr.base);
			continue;
		}

		if (value_is_expected) {
			if (t == NULL) {
				wrong_value = true;
				i_fail = i;
				itr.base.iface->cleanup(&itr.base);
				itr.base.iface->close(&itr.base);
				continue;
			}
			if (vy_stmt_lsn(t) != lsn) {
				wrong_lsn = true;
				i_fail = i;
			}
			uint32_t got_val;
			if (tuple_field_u32(t, 0, &got_val) ||
			    got_val != key) {
				wrong_value = true;
				i_fail = i;
			}
		} else {
			if (t != NULL) {
				wrong_value = true;
				i_fail = i;
			}
		}

		itr.base.iface->cleanup(&itr.base);
		itr.base.iface->close(&itr.base);

		tuple_unref(stmt);
	}

	ok(!wrong_rc, "check rc %d", i_fail);
	ok(!wrong_lsn, "check lsn %d", i_fail);
	ok(!wrong_value, "check value %d", i_fail);

	/* Clean up */
	vy_mem_delete(mem);
	lsregion_destroy(&lsregion);
	box_key_def_delete(key_def);

	fiber_gc();

	check_plan();

	footer();
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
	struct tuple_format *format = tuple_format_new(&vy_tuple_format_vtab,
						       &key_def, 1, 0);
	assert(format != NULL);
	tuple_format_ref(format, 1);

	/* Create lsregion */
	struct lsregion lsregion;
	struct slab_cache *slab_cache = cord_slab_cache();
	lsregion_create(&lsregion, slab_cache->arena);

	struct tuple *select_key = vy_stmt_new_select(format, "", 0);

	uint64_t restore_on_value = 20;
	uint64_t restore_on_value_reverse = 60;
	char data[16];
	char *end = data;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, restore_on_value);
	struct tuple *restore_on_key = vy_stmt_new_replace(format, data, end);
	vy_stmt_set_lsn(restore_on_key, 100);
	end = data;
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, restore_on_value_reverse);
	struct tuple *restore_on_key_reverse = vy_stmt_new_replace(format, data, end);
	vy_stmt_set_lsn(restore_on_key_reverse, 100);

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
		struct vy_mem *mem = create_test_mem(&lsregion, key_def);
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
				     &prv, NULL);
		struct tuple *t;
		bool stop = false;
		int rc = itr.base.iface->next_key(&itr.base, &t, &stop);
		assert(rc == 0);
		size_t j = 0;
		while (t != NULL) {
			if (j >= expected_count) {
				wrong_output = true;
				break;
			}
			uint32_t val = 42;
			tuple_field_u32(t, 0, &val);
			if (val != expected_values[j] ||
			    vy_stmt_lsn(t) != expected_lsns[j]) {
				wrong_output = true;
				break;
			}
			j++;
			if (direct && val >= middle_value)
				break;
			else if(!direct && val <= middle_value)
				break;
			int rc = itr.base.iface->next_key(&itr.base, &t, &stop);
			assert(rc == 0);
		}
		if (t == NULL && j != expected_count)
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
			rc = itr.base.iface->restore(&itr.base, restore_on_key, &t, &stop);
		else
			rc = itr.base.iface->restore(&itr.base, restore_on_key_reverse, &t, &stop);

		j = 0;
		while (t != NULL) {
			if (j >= expected_count) {
				wrong_output = true;
				break;
			}
			uint32_t val = 42;
			tuple_field_u32(t, 0, &val);
			if (val != expected_values[j] ||
			    vy_stmt_lsn(t) != expected_lsns[j]) {
				wrong_output = true;
				break;
			}
			j++;
			int rc = itr.base.iface->next_key(&itr.base, &t, &stop);
			assert(rc == 0);
		}
		if (j != expected_count)
			wrong_output = true;
		if (wrong_output) {
			i_fail = i;
			break;
		}

		vy_mem_delete(mem);
		lsregion_gc(&lsregion, 2);
	}

	ok(!wrong_output, "check wrong_output %d", i_fail);

	/* Clean up */
	tuple_unref(select_key);
	tuple_unref(restore_on_key);
	tuple_unref(restore_on_key_reverse);

	tuple_format_ref(format, -1);
	lsregion_destroy(&lsregion);
	box_key_def_delete(key_def);

	fiber_gc();

	check_plan();

	footer();
}

int
main(int argc, char *argv[])
{
	vy_iterator_C_test_init(0);

	test_basic();
	test_iterator_initial_restore();
	test_iterator_restore_after_insertion();

	vy_iterator_C_test_finish();
	return 0;
}
