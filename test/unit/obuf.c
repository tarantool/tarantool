#include "small/quota.h"
#include "small/obuf.h"
#include "small/slab_cache.h"
#include "unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
	OBJSIZE_MIN = sizeof(int),
	OBJSIZE_MAX = 5000,
	OBJECTS_MAX = 1000,
	OSCILLATION_MAX = 1024,
	ITERATIONS_MAX = 5000,
};

/** Keep global to easily inspect the core. */
long seed;

void
alloc_checked(struct obuf *buf)
{
	int pos = rand() % OBJECTS_MAX;
	int size = rand() % OBJSIZE_MAX;
	if (size < OBJSIZE_MIN || size > OBJSIZE_MAX)
		size = OBJSIZE_MIN;

	obuf_alloc_nothrow(buf, size);
}

static void
basic_alloc_streak(struct obuf *buf)
{
	int oscillation = rand() % OSCILLATION_MAX;
	int i;

	for (i = 0; i < oscillation; ++i)
		alloc_checked(buf);
}

void
obuf_basic(struct slab_cache *slabc)
{
	int i;
	header();

	struct obuf buf;
	obuf_create(&buf, slabc, 16320);

	for (i = 0; i < ITERATIONS_MAX; i++) {
		basic_alloc_streak(&buf);
		obuf_reset(&buf);
		fail_unless(obuf_used(&buf) == 0);
	}
	obuf_destroy(&buf);
	fail_unless(slab_cache_used(slabc) == 0);
	slab_cache_check(slabc);

	footer();
}

int main()
{
	struct slab_cache cache;
	struct slab_arena arena;
	struct quota quota;

	seed = time(0);

	srand(seed);

	quota_init(&quota, UINT_MAX);

	slab_arena_create(&arena, &quota, 0, 4000000,
			  MAP_PRIVATE);
	slab_cache_create(&cache, &arena);

	obuf_basic(&cache);

	slab_cache_destroy(&cache);
}

