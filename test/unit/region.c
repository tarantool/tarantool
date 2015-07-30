#include "small/region.h"
#include "small/quota.h"
#include "unit.h"
#include <stdio.h>

struct slab_cache cache;
struct slab_arena arena;
struct quota quota;

void
region_basic()
{
	header();

	struct region region;

	region_create(&region, &cache);

	fail_unless(region_used(&region) == 0);

	void *ptr = region_alloc_nothrow(&region, 10);

	fail_unless(ptr);

	fail_unless(region_used(&region) == 10);

	ptr = region_alloc_nothrow(&region, 10000000);
	fail_unless(ptr);

	fail_unless(region_used(&region) == 10000010);

	region_free(&region);

	fail_unless(region_used(&region) == 0);

	printf("name of a new region: %s.\n", region_name(&region));

	region_set_name(&region, "region");

	printf("set new region name: %s.\n", region_name(&region));

	region_set_name(&region, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

	printf("region name is truncated: %s.\n", region_name(&region));

	footer();
}

void
region_test_truncate()
{
	header();

	struct region region;

	region_create(&region, &cache);

	void *ptr = region_alloc_nothrow(&region, 10);

	fail_unless(ptr);

	size_t used = region_used(&region);

	region_alloc_nothrow(&region, 10000);
	region_alloc_nothrow(&region, 10000000);

	region_truncate(&region, used);

	fail_unless(region_used(&region) == used);

	region_free(&region);

	footer();
}

int main()
{
	quota_init(&quota, UINT_MAX);
	slab_arena_create(&arena, &quota, 0,
			  4000000, MAP_PRIVATE);
	slab_cache_create(&cache, &arena);

	region_basic();
	region_test_truncate();

	slab_cache_destroy(&cache);
}
