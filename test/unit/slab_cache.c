#include "small/slab_cache.h"
#include "small/quota.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include "unit.h"


enum { NRUNS = 25, ITERATIONS = 1000, MAX_ALLOC = 5000000 };
static struct slab *runs[NRUNS];

int main()
{
	srand(time(0));

	struct quota quota;
	struct slab_arena arena;
	struct slab_cache cache;

	quota_init(&quota, UINT_MAX);

	slab_arena_create(&arena, &quota, 0, 4000000, MAP_PRIVATE);
	slab_cache_create(&cache, &arena);

	int i = 0;

	while (i < ITERATIONS) {
		int run = random() % NRUNS;
		int size = random() % MAX_ALLOC;
		if (runs[run]) {
			slab_put(&cache, runs[run]);
		}
		runs[run] = slab_get(&cache, size);
		fail_unless(runs[run]);
		slab_cache_check(&cache);
		i++;
	}

	slab_cache_destroy(&cache);
}
