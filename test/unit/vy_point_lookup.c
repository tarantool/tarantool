#include "trivia/util.h"
#include "unit.h"
#include "vy_lsm.h"
#include "vy_cache.h"
#include "vy_run.h"
#include "fiber.h"
#include <bit/bit.h>
#include <crc32.h>
#include <box/vy_point_lookup.h>
#include "vy_iterators_helper.h"
#include "vy_write_iterator.h"
#include "identifier.h"

uint32_t schema_version;

static int
write_run(struct vy_run *run, const char *dir_name,
	  struct vy_lsm *lsm, struct vy_stmt_stream *wi)
{
	struct vy_run_writer writer;
	if (vy_run_writer_create(&writer, run, dir_name,
				 lsm->space_id, lsm->index_id,
				 lsm->cmp_def, lsm->key_def,
				 4096, 0.1) != 0)
		goto fail;

	if (wi->iface->start(wi) != 0)
		goto fail_abort_writer;
	int rc;
	struct tuple *stmt = NULL;
	while ((rc = wi->iface->next(wi, &stmt)) == 0 && stmt != NULL) {
		rc = vy_run_writer_append_stmt(&writer, stmt);
		if (rc != 0)
			break;
	}
	wi->iface->stop(wi);

	if (rc == 0)
		rc = vy_run_writer_commit(&writer);
	if (rc != 0)
		goto fail_abort_writer;

	return 0;

fail_abort_writer:
	vy_run_writer_abort(&writer);
fail:
	return -1;
}

static void
test_basic()
{
	header();
	plan(15);

	/** Suppress info messages from vy_run_writer. */
	say_set_log_level(S_WARN);

	const size_t QUOTA = 100 * 1024 * 1024;
	int64_t generation = 0;
	struct slab_cache *slab_cache = cord_slab_cache();

	int rc;
	struct vy_lsm_env lsm_env;
	rc = vy_lsm_env_create(&lsm_env, ".", &generation, NULL, NULL);
	is(rc, 0, "vy_lsm_env_create");

	struct vy_run_env run_env;
	vy_run_env_create(&run_env);

	struct vy_cache_env cache_env;
	vy_cache_env_create(&cache_env, slab_cache);
	vy_cache_env_set_quota(&cache_env, QUOTA);

	struct vy_cache cache;
	uint32_t fields[] = { 0 };
	uint32_t types[] = { FIELD_TYPE_UNSIGNED };
	struct key_def *key_def = box_key_def_new(fields, types, 1);
	isnt(key_def, NULL, "key_def is not NULL");

	vy_cache_create(&cache, &cache_env, key_def);
	struct tuple_format *format = tuple_format_new(&vy_tuple_format_vtab,
						       &key_def, 1, 0, NULL, 0,
						       NULL);
	isnt(format, NULL, "tuple_format_new is not NULL");
	tuple_format_ref(format);

	struct index_opts index_opts = index_opts_default;
	struct index_def *index_def =
		index_def_new(512, 0, "primary", sizeof("primary") - 1, TREE,
			      &index_opts, key_def, NULL);

	struct vy_lsm *pk = vy_lsm_new(&lsm_env, &cache_env, &mem_env,
				       index_def, format, NULL);
	isnt(pk, NULL, "lsm is not NULL")

	struct vy_range *range = vy_range_new(1, NULL, NULL, pk->cmp_def);

	isnt(pk, NULL, "range is not NULL")
	vy_lsm_add_range(pk, range);

	struct rlist read_views = RLIST_HEAD_INITIALIZER(read_views);

	char dir_tmpl[] = "./vy_point_test.XXXXXX";
	char *dir_name = mkdtemp(dir_tmpl);
	isnt(dir_name, NULL, "temp dir name is not NULL")
	char path[PATH_MAX];
	strcpy(path, dir_name);
	strcat(path, "/512");
	rc = mkdir(path, 0777);
	is(rc, 0, "temp dir create (2)");
	strcat(path, "/0");
	rc = mkdir(path, 0777);
	is(rc, 0, "temp dir create (3)");

	/* Filling the LSM tree with test data */
	/* Prepare variants */
	const size_t num_of_keys = 100;
	bool in_mem1[num_of_keys]; /* UPSERT value += 1, lsn 4 */
	bool in_mem2[num_of_keys]; /* UPSERT value += 2, lsn 3 */
	bool in_run1[num_of_keys]; /* UPSERT value += 4, lsn 2 */
	bool in_run2[num_of_keys]; /* UPSERT value += 8, lsn 1 */
	bool in_cache[num_of_keys];
	uint32_t expect[num_of_keys];
	int64_t expect_lsn[num_of_keys];
	for (size_t i = 0; i < num_of_keys; i++) {
		in_mem1[i] = i & 1;
		in_mem2[i] = i & 2;
		in_run1[i] = i & 4;
		in_run2[i] = i & 8;
		in_cache[i] = i & 16;
		expect[i] = (in_mem1[i] ? 1 : 0) + (in_mem2[i] ? 2 : 0) +
			    (in_run1[i] ? 4 : 0) + (in_run2[i] ? 8 : 0);
		expect_lsn[i] = expect[i] == 0 ? 0 : 5 - bit_ctz_u32(expect[i]);
	}

	for (size_t i = 0; i < num_of_keys; i++) {
		if (!in_cache[i])
			continue;
		if (expect[i] != 0) {
			struct vy_stmt_template tmpl_key =
				STMT_TEMPLATE(0, SELECT, i);
			struct vy_stmt_template tmpl_val =
				STMT_TEMPLATE(expect_lsn[i], REPLACE, i, expect[i]);
			vy_cache_insert_templates_chain(&cache, format,
							&tmpl_val, 1, &tmpl_key,
							ITER_EQ);
		}
	}

	/* create second mem */
	for (size_t i = 0; i < num_of_keys; i++) {
		if (!in_mem2[i])
			continue;
		struct vy_stmt_template tmpl_val =
			STMT_TEMPLATE(3, UPSERT, i, 2);
		tmpl_val.upsert_field = 1;
		tmpl_val.upsert_value = 2;
		vy_mem_insert_template(pk->mem, &tmpl_val);
	}

	rc = vy_lsm_rotate_mem(pk);
	is(rc, 0, "vy_lsm_rotate_mem");

	/* create first mem */
	for (size_t i = 0; i < num_of_keys; i++) {
		if (!in_mem1[i])
			continue;
		struct vy_stmt_template tmpl_val =
			STMT_TEMPLATE(4, UPSERT, i, 1);
		tmpl_val.upsert_field = 1;
		tmpl_val.upsert_value = 1;
		vy_mem_insert_template(pk->mem, &tmpl_val);
	}

	/* create second run */
	struct vy_mem *run_mem =
		vy_mem_new(pk->mem->env, *pk->env->p_generation,
			   pk->cmp_def, pk->mem_format,
			   pk->mem_format_with_colmask, 0);

	for (size_t i = 0; i < num_of_keys; i++) {
		if (!in_run2[i])
			continue;
		struct vy_stmt_template tmpl_val =
			STMT_TEMPLATE(1, UPSERT, i, 8);
		tmpl_val.upsert_field = 1;
		tmpl_val.upsert_value = 8;
		vy_mem_insert_template(run_mem, &tmpl_val);
	}
	struct vy_stmt_stream *write_stream
		= vy_write_iterator_new(pk->cmp_def, pk->disk_format,
					true, true, &read_views);
	vy_write_iterator_new_mem(write_stream, run_mem);
	struct vy_run *run = vy_run_new(&run_env, 1);
	isnt(run, NULL, "vy_run_new");

	rc = write_run(run, dir_name, pk, write_stream);
	is(rc, 0, "vy_run_write");

	write_stream->iface->close(write_stream);
	vy_mem_delete(run_mem);

	vy_lsm_add_run(pk, run);
	struct vy_slice *slice = vy_slice_new(1, run, NULL, NULL, pk->cmp_def);
	vy_range_add_slice(range, slice);
	vy_run_unref(run);

	/* create first run */
	run_mem =
		vy_mem_new(pk->mem->env, *pk->env->p_generation,
			   pk->cmp_def, pk->mem_format,
			   pk->mem_format_with_colmask, 0);

	for (size_t i = 0; i < num_of_keys; i++) {
		if (!in_run1[i])
			continue;
		struct vy_stmt_template tmpl_val =
			STMT_TEMPLATE(2, UPSERT, i, 4);
		tmpl_val.upsert_field = 1;
		tmpl_val.upsert_value = 4;
		vy_mem_insert_template(run_mem, &tmpl_val);
	}
	write_stream
		= vy_write_iterator_new(pk->cmp_def, pk->disk_format,
					true, true, &read_views);
	vy_write_iterator_new_mem(write_stream, run_mem);
	run = vy_run_new(&run_env, 2);
	isnt(run, NULL, "vy_run_new");

	rc = write_run(run, dir_name, pk, write_stream);
	is(rc, 0, "vy_run_write");

	write_stream->iface->close(write_stream);
	vy_mem_delete(run_mem);

	vy_lsm_add_run(pk, run);
	slice = vy_slice_new(1, run, NULL, NULL, pk->cmp_def);
	vy_range_add_slice(range, slice);
	vy_run_unref(run);

	/* Compare with expected */
	bool results_ok = true;
	bool has_errors = false;
	for (int64_t vlsn = 0; vlsn <= 6; vlsn++) {
		struct vy_read_view rv;
		rv.vlsn = vlsn == 6 ? INT64_MAX : vlsn;
		const struct vy_read_view *prv = &rv;

		for (size_t i = 0; i < num_of_keys; i++) {
			uint32_t expect = 0;
			int64_t expect_lsn = 0;
			if (in_run2[i] && vlsn >= 1) {
				expect += 8;
				expect_lsn = 1;
			}
			if (in_run1[i] && vlsn >= 2) {
				expect += 4;
				expect_lsn = 2;
			}
			if (in_mem2[i] && vlsn >= 3) {
				expect += 2;
				expect_lsn = 3;
			}
			if (in_mem1[i] && vlsn >= 4) {
				expect += 1;
				expect_lsn = 4;
			}

			struct vy_stmt_template tmpl_key =
				STMT_TEMPLATE(0, SELECT, i);
			struct tuple *key = vy_new_simple_stmt(format,
					pk->mem_format_with_colmask, &tmpl_key);
			struct tuple *res;
			rc = vy_point_lookup(pk, NULL, &prv, key, &res);
			tuple_unref(key);
			if (rc != 0) {
				has_errors = true;
				continue;
			}
			if (expect == 0) {
				/* No value expected. */
				if (res != NULL)
					results_ok = false;
				continue;
			} else {
				if (res == NULL) {
					results_ok = false;
					continue;
				}
			}
			uint32_t got = 0;
			tuple_field_u32(res, 1, &got);
			if (got != expect && expect_lsn != vy_stmt_lsn(res))
				results_ok = false;
			tuple_unref(res);
		}
	}

	is(results_ok, true, "select results");
	is(has_errors, false, "no errors happened");

	vy_lsm_unref(pk);
	index_def_delete(index_def);
	tuple_format_unref(format);
	vy_cache_destroy(&cache);
	key_def_delete(key_def);
	vy_cache_env_destroy(&cache_env);
	vy_run_env_destroy(&run_env);
	vy_lsm_env_destroy(&lsm_env);

	strcpy(path, "rm -rf ");
	strcat(path, dir_name);
	system(path);

	check_plan();
	footer();
}

int
main()
{
	plan(1);

	vy_iterator_C_test_init(128 * 1024);
	crc32_init();

	test_basic();

	vy_iterator_C_test_finish();

	return check_plan();
}
