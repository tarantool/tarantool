#include "box/allocator.h"
#include "box/memtx_allocator.h"
#include "box/tuple.h"
#include "box/tuple_format.h"
#include "fiber.h"
#include "memory.h"
#include "say.h"
#include "small/mempool.h"
#include "small/slab_arena.h"
#include "small/slab_cache.h"
#include "small/quota.h"
#include "unit.h"

#define ARENA_SIZE (16 * 1024 * 1024)
#define SLAB_SIZE (1024 * 1024)
#define OBJSIZE_MIN 16
#define GRANULARITY 8
#define ALLOC_FACTOR 1.05

static struct tuple_format *test_tuple_format;

static struct tuple *
test_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	assert(format == test_tuple_format);
	assert(data == NULL);
	assert(end == NULL);
	(void)data;
	(void)end;
	size_t size = sizeof(struct tuple);
	struct tuple *tuple = MemtxAllocator<SmallAlloc>::alloc_tuple(size);
	tuple_create(tuple, /*local_refs=*/0, tuple_format_id(format),
		     /*data_offset=*/size, /*bsize=*/0, /*make_compact=*/true);
	return tuple;
}

static void
test_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	assert(format == test_tuple_format);
	(void)format;
	assert(tuple_is_unreferenced(tuple));
	MemtxAllocator<SmallAlloc>::free_tuple(tuple);
}

static struct tuple_format_vtab test_tuple_format_vtab = {
	.tuple_delete = test_tuple_delete,
	.tuple_new = test_tuple_new,
};

static struct tuple *
alloc_tuple()
{
	struct tuple *tuple = tuple_new(test_tuple_format, NULL, NULL);
	fail_if(tuple == NULL);
	return tuple;
}

static struct tuple *
alloc_temp_tuple()
{
	struct tuple *tuple = alloc_tuple();
	tuple_set_flag(tuple, TUPLE_IS_TEMPORARY);
	return tuple;
}

static void
free_tuple(struct tuple *tuple)
{
	tuple_delete(tuple);
}

struct alloc_tuple_count_ctx {
	int count;
};

static int
alloc_tuple_count_cb(const void *stats_, void *ctx_)
{
	const struct mempool_stats *stats =
		(const struct mempool_stats *)stats_;
	struct alloc_tuple_count_ctx *ctx =
		(struct alloc_tuple_count_ctx *)ctx_;
	ctx->count += stats->objcount;
	return 0;
}

static int
alloc_tuple_count()
{
	while (MemtxAllocator<SmallAlloc>::collect_garbage()) {
	}
	struct alloc_tuple_count_ctx ctx;
	struct allocator_stats unused;
	ctx.count = 0;
	SmallAlloc::stats(&unused, alloc_tuple_count_cb, &ctx);
	return ctx.count;
}

/**
 * Checks allocator statistics after allocating and freeing some tuples.
 */
static void
test_alloc_stats()
{
	plan(5);
	header();

	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple[15];
	for (int i = 0; i < 10; ++i)
		tuple[i] = alloc_tuple();
	is(alloc_tuple_count(), 10, "count after alloc 1");
	for (int i = 10; i < 15; ++i)
		tuple[i] = alloc_tuple();
	is(alloc_tuple_count(), 15, "count after alloc 2");
	for (int i = 0; i < 5; ++i)
		free_tuple(tuple[i]);
	is(alloc_tuple_count(), 10, "count after free 1");
	for (int i = 5; i < 15; ++i)
		free_tuple(tuple[i]);
	is(alloc_tuple_count(), 0, "count after free 2");

	footer();
	check_plan();
}

/**
 * Checks that freeing of a tuple is delayed if there is a read view that was
 * created after the tuple was allocated.
 */
static void
test_free_delayed_if_alloc_before_read_view()
{
	plan(4);
	header();

	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple = alloc_tuple();
	is(alloc_tuple_count(), 1, "count after alloc");
	memtx_allocators_read_view rv = memtx_allocators_open_read_view({});
	free_tuple(tuple);
	is(alloc_tuple_count(), 1, "count after free");
	memtx_allocators_close_read_view(rv);
	is(alloc_tuple_count(), 0, "count after read view closed");

	footer();
	check_plan();
}

/**
 * Checks that freeing of a tuple is delayed until the last read view from
 * which it is visible is closed.
 */
static void
test_free_delayed_until_all_read_views_closed()
{
	plan(5);
	header();

	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple = alloc_tuple();
	is(alloc_tuple_count(), 1, "count after alloc");
	memtx_allocators_read_view rv1 = memtx_allocators_open_read_view({});
	memtx_allocators_read_view rv2 = memtx_allocators_open_read_view({});
	free_tuple(tuple);
	is(alloc_tuple_count(), 1, "count after free");
	memtx_allocators_close_read_view(rv1);
	is(alloc_tuple_count(), 1, "count after first read view closed");
	memtx_allocators_close_read_view(rv2);
	is(alloc_tuple_count(), 0, "count after second read view closed");

	footer();
	check_plan();
}

/**
 * Checks that freeing of a tuple is not delayed if it was allocated after
 * the last read view was created.
 */
static void
test_free_not_delayed_if_alloc_after_read_view()
{
	plan(3);
	header();

	memtx_allocators_read_view rv = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple = alloc_tuple();
	is(alloc_tuple_count(), 1, "count after alloc");
	free_tuple(tuple);
	is(alloc_tuple_count(), 0, "count after free");
	memtx_allocators_close_read_view(rv);

	footer();
	check_plan();
}

/**
 * Checks that freeing of a temporary tuple is never delayed.
 */
static void
test_free_not_delayed_if_temporary()
{
	plan(3);
	header();

	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple = alloc_temp_tuple();
	is(alloc_tuple_count(), 1, "count after alloc");
	memtx_allocators_read_view rv = memtx_allocators_open_read_view({});
	free_tuple(tuple);
	is(alloc_tuple_count(), 0, "count after free");
	memtx_allocators_close_read_view(rv);

	footer();
	check_plan();
}

/**
 * Checks that tuples are freed as soon as all read views that can access it
 * are closed, even if other (newer or older) read views still exist.
 */
static void
test_tuple_gc()
{
	plan(11);
	header();

	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple11 = alloc_tuple();
	struct tuple *tuple12 = alloc_tuple();
	struct tuple *tuple13 = alloc_tuple();
	struct tuple *tuple14 = alloc_tuple();
	memtx_allocators_read_view rv1 = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 4, "count after rv1 opened");
	free_tuple(tuple11);
	struct tuple *tuple22 = alloc_tuple();
	struct tuple *tuple23 = alloc_tuple();
	struct tuple *tuple24 = alloc_tuple();
	memtx_allocators_read_view rv2 = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 7, "count after rv2 opened");
	free_tuple(tuple12);
	free_tuple(tuple22);
	struct tuple *tuple33 = alloc_tuple();
	struct tuple *tuple34 = alloc_tuple();
	memtx_allocators_read_view rv3 = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 9, "count after rv3 opened");
	free_tuple(tuple13);
	free_tuple(tuple23);
	free_tuple(tuple33);
	struct tuple *tuple44 = alloc_tuple();

	is(alloc_tuple_count(), 10, "count before rv2 closed");
	memtx_allocators_close_read_view(rv2);
	/* tuple22 is freed */
	is(alloc_tuple_count(), 9, "count after rv2 closed");

	memtx_allocators_read_view rv4 = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 9, "count after rv4 opened");
	free_tuple(tuple14);
	free_tuple(tuple24);
	free_tuple(tuple34);
	free_tuple(tuple44);

	is(alloc_tuple_count(), 9, "count before rv4 closed");
	memtx_allocators_close_read_view(rv4);
	/* tuple44 is freed */
	is(alloc_tuple_count(), 8, "count after rv4 closed");

	memtx_allocators_close_read_view(rv1);
	/* tuple11 and tuple12 are freed */
	is(alloc_tuple_count(), 6, "count after rv1 closed");

	/* tuple13, tuple14, tuple23, tuple24, tuple33, tuple34 are freed */
	memtx_allocators_close_read_view(rv3);
	is(alloc_tuple_count(), 0, "count after rv3 closed");

	footer();
	check_plan();
}

/**
 * Checks that temporary tuples are freed as soon as the last read view opened
 * with include_temporary_tuples flag is closed, even if there are still other
 * read views that may see it.
 */
static void
test_temp_tuple_gc()
{
	plan(10);
	header();

	struct memtx_read_view_opts opts;
	opts.include_temporary_tuples = true;

	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *temp_tuple11 = alloc_temp_tuple();
	struct tuple *temp_tuple12 = alloc_temp_tuple();
	struct tuple *temp_tuple13 = alloc_temp_tuple();
	struct tuple *temp_tuple14 = alloc_temp_tuple();
	struct tuple *tuple11 = alloc_tuple();
	struct tuple *tuple12 = alloc_tuple();
	struct tuple *tuple13 = alloc_tuple();
	struct tuple *tuple14 = alloc_tuple();
	memtx_allocators_read_view rv1 = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 8, "count after rv1 opened");
	free_tuple(temp_tuple11);
	free_tuple(tuple11);
	struct tuple *temp_tuple22 = alloc_temp_tuple();
	struct tuple *temp_tuple23 = alloc_temp_tuple();
	struct tuple *temp_tuple24 = alloc_temp_tuple();
	struct tuple *tuple22 = alloc_tuple();
	struct tuple *tuple23 = alloc_tuple();
	struct tuple *tuple24 = alloc_tuple();
	memtx_allocators_read_view rv2 = memtx_allocators_open_read_view(opts);
	/* temp_tuple11 is freed */
	is(alloc_tuple_count(), 13, "count after rv2 opened");
	free_tuple(temp_tuple12);
	free_tuple(temp_tuple22);
	free_tuple(tuple12);
	free_tuple(tuple22);
	struct tuple *temp_tuple33 = alloc_temp_tuple();
	struct tuple *temp_tuple34 = alloc_temp_tuple();
	struct tuple *tuple33 = alloc_tuple();
	struct tuple *tuple34 = alloc_tuple();
	memtx_allocators_read_view rv3 = memtx_allocators_open_read_view({});
	is(alloc_tuple_count(), 17, "count after rv3 opened");
	free_tuple(temp_tuple13);
	free_tuple(temp_tuple23);
	free_tuple(temp_tuple33);
	free_tuple(tuple13);
	free_tuple(tuple23);
	free_tuple(tuple33);
	struct tuple *temp_tuple44 = alloc_temp_tuple();
	struct tuple *tuple44 = alloc_tuple();
	memtx_allocators_read_view rv4 = memtx_allocators_open_read_view(opts);
	/* temp_tuple33 is freed */
	is(alloc_tuple_count(), 18, "count after rv4 opened");
	free_tuple(temp_tuple14);
	free_tuple(temp_tuple24);
	free_tuple(temp_tuple34);
	free_tuple(temp_tuple44);
	free_tuple(tuple14);
	free_tuple(tuple24);
	free_tuple(tuple34);
	free_tuple(tuple44);
	is(alloc_tuple_count(), 18, "count before rv4 closed");
	memtx_allocators_close_read_view(rv4);
	/* temp_tuple34, temp_tuple44, tuple44 are freed */
	is(alloc_tuple_count(), 15, "count after rv4 closed");
	memtx_allocators_close_read_view(rv3);
	/* tuple33 and tuple34 are freed */
	is(alloc_tuple_count(), 13, "count after rv3 closed");
	memtx_allocators_close_read_view(rv2);
	/*
	 * temp_tuple12, temp_tuple13, temp_tuple14,
	 * temp_tuple22, temp_tuple23, temp_tuple24,
	 * tuple22, tuple23, tuple24 are freed.
	 */
	is(alloc_tuple_count(), 4, "count after rv2 closed");
	memtx_allocators_close_read_view(rv1);
	/* tuple11, tuple12, tuple13, tuple14 are freed */
	is(alloc_tuple_count(), 0, "count after rv1 closed");

	footer();
	check_plan();
}

static int
test_main()
{
	plan(7);
	header();

	test_alloc_stats();
	test_free_delayed_if_alloc_before_read_view();
	test_free_delayed_until_all_read_views_closed();
	test_free_not_delayed_if_alloc_after_read_view();
	test_free_not_delayed_if_temporary();
	test_tuple_gc();
	test_temp_tuple_gc();

	footer();
	return check_plan();
}

int
main()
{
	say_logger_init("/dev/null", S_INFO, /*nonblock=*/true, "plain",
			/*background=*/false);
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);
	struct quota quota;
	quota_init(&quota, QUOTA_MAX);
	struct slab_arena arena;
	tuple_arena_create(&arena, &quota, ARENA_SIZE, SLAB_SIZE,
			   /*dontdump=*/false, "test");
	struct slab_cache cache;
	slab_cache_create(&cache, &arena);
	float actual_alloc_factor;
	allocator_settings alloc_settings;
	allocator_settings_init(&alloc_settings, &cache, OBJSIZE_MIN,
				GRANULARITY, ALLOC_FACTOR,
				&actual_alloc_factor, &quota);
	memtx_allocators_init(&alloc_settings);
	test_tuple_format = simple_tuple_format_new(
		&test_tuple_format_vtab, /*engine=*/NULL,
		/*keys=*/NULL, /*key_count=*/0);
	fail_if(test_tuple_format == NULL);

	int rc = test_main();

	tuple_format_delete(test_tuple_format);
	memtx_allocators_destroy();
	slab_cache_destroy(&cache);
	tuple_arena_destroy(&arena);
	tuple_free();
	fiber_free();
	memory_free();
	say_logger_free();
	return rc;
}
