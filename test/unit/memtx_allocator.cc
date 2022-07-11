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
	/* Trigger garbage collection before checking count. */
	struct tuple *tuple = alloc_tuple();
	fail_if(tuple == NULL);
	free_tuple(tuple);
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
	memtx_allocators_enter_delayed_free_mode();
	free_tuple(tuple);
	is(alloc_tuple_count(), 1, "count after free");
	memtx_allocators_leave_delayed_free_mode();
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
	memtx_allocators_enter_delayed_free_mode();
	memtx_allocators_enter_delayed_free_mode();
	free_tuple(tuple);
	is(alloc_tuple_count(), 1, "count after free");
	memtx_allocators_leave_delayed_free_mode();
	is(alloc_tuple_count(), 1, "count after first read view closed");
	memtx_allocators_leave_delayed_free_mode();
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

	memtx_allocators_enter_delayed_free_mode();
	is(alloc_tuple_count(), 0, "count before alloc");
	struct tuple *tuple = alloc_tuple();
	is(alloc_tuple_count(), 1, "count after alloc");
	free_tuple(tuple);
	is(alloc_tuple_count(), 0, "count after free");
	memtx_allocators_leave_delayed_free_mode();

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
	struct tuple *tuple = alloc_tuple();
	is(alloc_tuple_count(), 1, "count after alloc");
	tuple_set_flag(tuple, TUPLE_IS_TEMPORARY);
	memtx_allocators_enter_delayed_free_mode();
	free_tuple(tuple);
	is(alloc_tuple_count(), 0, "count after free");
	memtx_allocators_leave_delayed_free_mode();

	footer();
	check_plan();
}

static int
test_main()
{
	plan(5);
	header();

	test_alloc_stats();
	test_free_delayed_if_alloc_before_read_view();
	test_free_delayed_until_all_read_views_closed();
	test_free_not_delayed_if_alloc_after_read_view();
	test_free_not_delayed_if_temporary();

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
