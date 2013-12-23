#include "small/slab_arena.h"
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
	       arena->prealloc, arena->maxalloc,
	       arena->used, arena->slab_size);
}

int main()
{
	struct slab_arena arena;
	slab_arena_create(&arena, 0, 0, 0, MAP_PRIVATE);
	slab_arena_print(&arena);
	slab_arena_destroy(&arena);
	slab_arena_create(&arena, 1, 1, 1, MAP_PRIVATE);
	slab_arena_print(&arena);
	void *ptr = slab_map(&arena);
	slab_arena_print(&arena);
	void *ptr1 = slab_map(&arena);
	printf("going beyond the limit: %p\n", ptr1);
	slab_arena_print(&arena);
	slab_unmap(&arena, ptr);
	slab_unmap(&arena, ptr1);
	slab_arena_print(&arena);

	slab_arena_destroy(&arena);
	slab_arena_create(&arena, 2000000, 3000000, 1, MAP_PRIVATE);
	slab_arena_print(&arena);
	slab_arena_destroy(&arena);
}
