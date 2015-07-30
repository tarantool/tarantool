#include "small/quota.h"
#include "small/ibuf.h"
#include "small/slab_cache.h"
#include "unit.h"
#include <stdio.h>

struct slab_cache cache;
struct slab_arena arena;
struct quota quota;

void
ibuf_basic()
{
	header();

	struct ibuf ibuf;

	ibuf_create(&ibuf, &cache, 16320);

	fail_unless(ibuf_used(&ibuf) == 0);

	void *ptr = ibuf_alloc_nothrow(&ibuf, 10);

	fail_unless(ptr);

	fail_unless(ibuf_used(&ibuf) == 10);

	ptr = ibuf_alloc_nothrow(&ibuf, 1000000);
	fail_unless(ptr);

	fail_unless(ibuf_used(&ibuf) == 1000010);

	ibuf_reset(&ibuf);

	fail_unless(ibuf_used(&ibuf) == 0);

	footer();
}

int main()
{
	quota_init(&quota, UINT_MAX);
	slab_arena_create(&arena, &quota, 0,
			  4000000, MAP_PRIVATE);
	slab_cache_create(&cache, &arena);

	ibuf_basic();

	slab_cache_destroy(&cache);
}
