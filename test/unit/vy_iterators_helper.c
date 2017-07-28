#include "vy_iterators_helper.h"
#include "memory.h"
#include "fiber.h"

struct tuple_format *vy_key_format = NULL;
struct vy_cache_env cache_env;

void
vy_iterator_C_test_init(size_t cache_size)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init();
	vy_cache_env_create(&cache_env, cord_slab_cache(), cache_size);
	vy_key_format = tuple_format_new(&vy_tuple_format_vtab, NULL, 0, 0);
	tuple_format_ref(vy_key_format, 1);
}

void
vy_iterator_C_test_finish()
{
	tuple_format_ref(vy_key_format, -1);
	vy_cache_env_destroy(&cache_env);
	tuple_free();
	fiber_free();
	memory_free();
}

struct tuple *
vy_new_simple_stmt(struct tuple_format *format,
		   struct tuple_format *upsert_format,
		   struct tuple_format *format_with_colmask,
		   const struct vy_stmt_template *templ)
{
	if (templ == NULL)
		return NULL;
	/* Calculate binary size. */
	int i = 0;
	size_t size = 0;
	while (templ->fields[i] != vyend) {
		fail_if(i > MAX_FIELDS_COUNT);
		if (templ->fields[i] >= 0)
			size += mp_sizeof_uint(templ->fields[i]);
		else
			size += mp_sizeof_int(templ->fields[i]);
		++i;
	}
	size += mp_sizeof_array(i);
	fail_if(templ->optimize_update && templ->type == IPROTO_UPSERT);
	if (templ->optimize_update)
		format = format_with_colmask;

	/* Encode the statement. */
	char *buf = (char *) malloc(size);
	fail_if(buf == NULL);
	char *pos = mp_encode_array(buf, i);
	i = 0;
	struct tuple *ret = NULL;
	while (templ->fields[i] != vyend) {
		if (templ->fields[i] >= 0)
			pos = mp_encode_uint(pos, templ->fields[i]);
		else
			pos = mp_encode_int(pos, templ->fields[i]);
		++i;
	}

	/*
	 * Create the result statement, using one of the formats.
	 */
	if (templ->type == IPROTO_REPLACE || templ->type == IPROTO_DELETE) {
		ret = vy_stmt_new_replace(format, buf, pos);
		fail_if(ret == NULL);
		if (templ->type == IPROTO_REPLACE)
			goto end;

		struct tuple *tmp = vy_stmt_new_surrogate_delete(format, ret);
		fail_if(tmp == NULL);
		tuple_unref(ret);
		ret = tmp;
		goto end;
	}
	if (templ->type == IPROTO_UPSERT) {
		/*
		 * Create the upsert statement without operations.
		 * Validation of result of UPSERT operations
		 * applying is not a test for the iterators.
		 * For the iterators only UPSERT type is
		 * important.
		 */
		struct iovec operations[1];
		char tmp[16];
		char *ops = mp_encode_array(tmp, 1);
		ops = mp_encode_array(ops, 0);
		operations[0].iov_base = tmp;
		operations[0].iov_len = ops - tmp;
		fail_if(templ->optimize_update);
		ret = vy_stmt_new_upsert(upsert_format, buf, pos,
					 operations, 1);
		fail_if(ret == NULL);
		goto end;
	}
	if (templ->type == IPROTO_SELECT) {
		const char *key = buf;
		uint part_count = mp_decode_array(&key);
		ret = vy_stmt_new_select(vy_key_format, key, part_count);
		fail_if(ret == NULL);
		goto end;
	}
	fail_if(true);
end:
	free(buf);
	vy_stmt_set_lsn(ret, templ->lsn);
	if (templ->optimize_update)
		vy_stmt_set_column_mask(ret, 0);
	return ret;
}

const struct tuple *
vy_mem_insert_template(struct vy_mem *mem, const struct vy_stmt_template *templ)
{
	struct tuple *stmt =
		vy_new_simple_stmt(mem->format, mem->upsert_format,
				   mem->format_with_colmask, templ);
	struct tuple *region_stmt = vy_stmt_dup_lsregion(stmt, mem->allocator,
							 mem->generation);
	assert(region_stmt != NULL);
	tuple_unref(stmt);
	if (templ->type == IPROTO_UPSERT)
		vy_mem_insert_upsert(mem, region_stmt);
	else
		vy_mem_insert(mem, region_stmt);
	return region_stmt;
}

void
vy_cache_insert_templates_chain(struct vy_cache *cache,
				struct tuple_format *format,
				const struct vy_stmt_template *chain,
				uint length,
				const struct vy_stmt_template *key_templ,
				enum iterator_type order)
{
	struct tuple *key =
		vy_new_simple_stmt(format, NULL, NULL, key_templ);
	struct tuple *prev_stmt = NULL;
	struct tuple *stmt = NULL;

	for (uint i = 0; i < length; ++i) {
		stmt = vy_new_simple_stmt(format, NULL, NULL, &chain[i]);
		vy_cache_add(cache, stmt, prev_stmt, key, order);
		if (i != 0)
			tuple_unref(prev_stmt);
		prev_stmt = stmt;
		stmt = NULL;
	}
	tuple_unref(key);
	if (prev_stmt != NULL)
		tuple_unref(prev_stmt);
	if (stmt != NULL)
		tuple_unref(stmt);
}

void
vy_cache_on_write_template(struct vy_cache *cache, struct tuple_format *format,
			   const struct vy_stmt_template *templ)
{
	struct tuple *written = vy_new_simple_stmt(format, NULL, NULL, templ);
	vy_cache_on_write(cache, written, NULL);
	tuple_unref(written);
}

void
init_read_views_list(struct rlist *rlist, struct vy_read_view *rvs,
		     const int *vlsns, int count)
{
	rlist_create(rlist);
	for (int i = 0; i < count; ++i) {
		rvs[i].vlsn = vlsns[i];
		rlist_add_tail_entry(rlist, &rvs[i], in_read_views);
	}
}

struct vy_mem *
create_test_mem(struct lsregion *region, struct key_def *def)
{
	/* Create format */
	struct tuple_format *format = tuple_format_new(&vy_tuple_format_vtab,
						       &def, def->part_count,
						       0);
	fail_if(format == NULL);

	/* Create format with column mask */
	struct tuple_format *format_with_colmask =
		vy_tuple_format_new_with_colmask(format);
	assert(format_with_colmask != NULL);

	/* Create upsert format */
	struct tuple_format *format_upsert =
		vy_tuple_format_new_upsert(format);
	assert(format_upsert != NULL);

	/* Create mem */
	struct vy_mem *mem = vy_mem_new(region, 1, def, format,
					format_with_colmask, format_upsert, 0);
	fail_if(mem == NULL);
	return mem;
}

bool
vy_stmt_are_same(const struct tuple *actual,
		 const struct vy_stmt_template *expected,
		 struct tuple_format *format,
		 struct tuple_format *upsert_format,
		 struct tuple_format *format_with_colmask)
{
	if (vy_stmt_type(actual) != expected->type)
		return false;
	struct tuple *tmp = vy_new_simple_stmt(format, upsert_format,
					       format_with_colmask, expected);
	fail_if(tmp == NULL);
	uint32_t a_len, b_len;
	const char *a, *b;
	if (vy_stmt_type(actual) == IPROTO_UPSERT) {
		a = vy_upsert_data_range(actual, &a_len);
	} else {
		a = tuple_data_range(actual, &a_len);
	}
	if (vy_stmt_type(tmp) == IPROTO_UPSERT) {
		b = vy_upsert_data_range(tmp, &b_len);
	} else {
		b = tuple_data_range(tmp, &b_len);
	}
	if (a_len != b_len) {
		tuple_unref(tmp);
		return false;
	}
	if (vy_stmt_lsn(actual) != expected->lsn) {
		tuple_unref(tmp);
		return false;
	}
	bool rc = memcmp(a, b, a_len) == 0;
	tuple_unref(tmp);
	return rc;
}
