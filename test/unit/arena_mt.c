#include "small/slab_arena.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include "unit.h"
#include <pthread.h>

struct slab_arena arena;

int THREADS = 8;
int ITERATIONS = 1009 /* 100003 */;
int OSCILLATION = 137;
int FILL = SLAB_MIN_SIZE/sizeof(pthread_t);

void *
run(void *p __attribute__((unused)))
{
	unsigned int seed = (unsigned int) pthread_self();
	int iterations = rand_r(&seed) % ITERATIONS;
	pthread_t **slabs = slab_map(&arena);
	for (int i = 0; i < iterations; i++) {
		int oscillation = rand_r(&seed) % OSCILLATION;
		for (int osc = 0; osc  < oscillation; osc++) {
			slabs[osc] = (pthread_t *) slab_map(&arena);
			for (int fill = 0; fill < FILL; fill += 100) {
				slabs[osc][fill] = pthread_self();
			}
		}
		sched_yield();
		for (int osc = 0; osc  < oscillation; osc++) {
			for (int fill = 0; fill < FILL; fill+= 100) {
				fail_unless(slabs[osc][fill] ==
					    pthread_self());
			}
			slab_unmap(&arena, slabs[osc]);
		}
	}
	slab_unmap(&arena, slabs);
	return 0;
}

void
bench(int count)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);

	pthread_t *threads = (pthread_t *) malloc(sizeof(*threads)*count);

	int i;
	for (i = 0; i < count; i++) {
		pthread_create(&threads[i], &attr, run, NULL);
	}
	for (i = 0; i < count; i++) {
		pthread_t *thread = &threads[i];
		pthread_join(*thread, NULL);
	}
	free(threads);
}

int
main()
{
	size_t maxalloc = THREADS * (OSCILLATION + 1) * SLAB_MIN_SIZE;
	slab_arena_create(&arena, maxalloc/8, maxalloc*2,
			  SLAB_MIN_SIZE, MAP_PRIVATE);
	bench(THREADS);
	slab_arena_destroy(&arena);
	printf("ok\n");
}
