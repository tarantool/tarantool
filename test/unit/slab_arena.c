#include "small/slab_arena.h"
#include "small/quota.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include "unit.h"

void
slab_arena_print(struct slab_arena *arena)
{
	printf("arena->prealloc = %zu\narena->maxalloc = %zu\n"
	       "arena->used = %zu\narena->slab_size = %u\n",
	       arena->prealloc, quota_total(arena->quota),
	       arena->used, arena->slab_size);
}

int main()
{
	struct quota quota;
	struct slab_arena arena;

	quota_init(&quota, 0);
	slab_arena_create(&arena, &quota, 0, 0, MAP_PRIVATE);
	slab_arena_print(&arena);
	slab_arena_destroy(&arena);

	quota_init(&quota, SLAB_MIN_SIZE);
	slab_arena_create(&arena, &quota, 1, 1, MAP_PRIVATE);
	slab_arena_print(&arena);
	void *ptr = slab_map(&arena);
	slab_arena_print(&arena);
	void *ptr1 = slab_map(&arena);
	printf("going beyond the limit: %s\n", ptr1 ? "(ptr)" : "(nil)");
	slab_arena_print(&arena);
	slab_unmap(&arena, ptr);
	slab_unmap(&arena, ptr1);
	slab_arena_print(&arena);
	slab_arena_destroy(&arena);

	quota_init(&quota, 2000000);
	slab_arena_create(&arena, &quota, 3000000, 1, MAP_PRIVATE);
	slab_arena_print(&arena);
	slab_arena_destroy(&arena);
}
