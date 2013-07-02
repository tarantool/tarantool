#include "lib/small/slab_cache.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include "unit.h"


enum { NRUNS = 25, ITERATIONS = 1000, MAX_ALLOC = SLAB_MAX_SIZE + 9999 };
static struct slab *runs[NRUNS];

int main()
{
	srand(time(0));

	struct slab_cache cache;
	slab_cache_create(&cache);

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
