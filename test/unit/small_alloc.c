#include "small/small.h"
#include "small/quota.h"
#include "unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
	OBJSIZE_MIN = 3 * sizeof(int),
	OBJSIZE_MAX = 5000,
	OBJECTS_MAX = 1000,
	OSCILLATION_MAX = 1024,
	ITERATIONS_MAX = 5000,
};

struct slab_arena arena;
struct slab_cache cache;
struct small_alloc alloc;
struct quota quota;
/* Streak type - allocating or freeing */
bool allocating = true;
/** Keep global to easily inspect the core. */
long seed;

static int *ptrs[OBJECTS_MAX];

static inline void
free_checked(int *ptr)
{
	fail_unless(ptr[0] < OBJECTS_MAX &&
		    ptr[ptr[1]/sizeof(int)-1] == ptr[0]);
	int pos = ptr[0];
	fail_unless(ptrs[pos] == ptr);
	ptrs[pos][0] = ptrs[pos][ptr[1]/sizeof(int)-1] = INT_MAX;
	smfree(&alloc, ptrs[pos], ptrs[pos][1]);
	ptrs[pos] = NULL;
}

static inline void *
alloc_checked()
{
	int pos = rand() % OBJECTS_MAX;
	int size = rand() % OBJSIZE_MAX;
	if (size < OBJSIZE_MIN || size > OBJSIZE_MAX)
		size = OBJSIZE_MIN;

	if (ptrs[pos]) {
		assert(ptrs[pos][0] == pos);
		free_checked(ptrs[pos]);
	}
	if (! allocating)
		return NULL;
	ptrs[pos] = smalloc_nothrow(&alloc, size);
	ptrs[pos][0] = pos;
	ptrs[pos][1] = size;
	ptrs[pos][size/sizeof(int)-1] = pos;
//	printf("size: %d\n", size);
	return ptrs[pos];
}


static void
basic_alloc_streak()
{
	int oscillation = rand() % OSCILLATION_MAX;
	int i;

	for (i = 0; i < oscillation; ++i) {
		alloc_checked();
	}
}

void
small_alloc_basic()
{
	int i;
	header();

	small_alloc_create(&alloc, &cache, OBJSIZE_MIN, 1.3);

	for (i = 0; i < ITERATIONS_MAX; i++) {
		basic_alloc_streak();
		allocating = ! allocating;
	}

	small_alloc_destroy(&alloc);

	footer();
}

int main()
{
	seed = time(0);

	srand(seed);

	quota_init(&quota, UINT_MAX);

	slab_arena_create(&arena, &quota, 0, 4000000,
			  MAP_PRIVATE);
	slab_cache_create(&cache, &arena);

	small_alloc_basic();

	slab_cache_destroy(&cache);
}
