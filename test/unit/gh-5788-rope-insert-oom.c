#include "unit.h"
#include <string.h>

/** Counting the number of allocated nodes */
static size_t allocated_count;
/** Counting the number of freed nodes */
static size_t freed_count;
/** Number of remaining node allocations */
static size_t remaining_allocations_count;

static inline void *
mem_alloc_sanitizer(void *data, size_t size)
{
        (void) data;
        if (remaining_allocations_count == 0)
                return NULL;
        allocated_count++;
        remaining_allocations_count--;
        return malloc(size);
}

static inline void
mem_free_sanitizer(void *data, void *ptr)
{
        freed_count++;
        (void) data;
        free(ptr);
}

static inline char *
str_getn(void *ctx, char *data, size_t size, size_t offset)
{
        (void) ctx;
        return data + offset;
}


#define ROPE_ALLOC_F mem_alloc_sanitizer
#define ROPE_FREE_F mem_free_sanitizer
#define ROPE_SPLIT_F str_getn
#define rope_data_t char *
#define rope_ctx_t void *

#include "salad/rope.h"

static inline struct rope *
prepare_test(int max_alloc_count)
{
        remaining_allocations_count = max_alloc_count;
        allocated_count = 0;
        freed_count = 0;
        return rope_new(NULL);;
}

/**
 * Test for bug fix #5788.
 * Try to exit from rope_insert due to out of memory
 * by limiting the available node allocations and
 * check that the number of allocated
 * nodes and the number of freed nodes are the same.
 */
static void
out_of_memory_stress_test()
{
        header();

        const int iterations = 20;
        plan(iterations);
        for (int max_allocs = 1; max_allocs <= iterations; ++max_allocs) {
                struct rope *rope = prepare_test(max_allocs);
                for(int i = 0; i <= max_allocs; ++i){
                        rope_insert(rope, rope_size(rope) / 2, "abcdefg", 7);
                }
                rope_delete(rope);
                is(allocated_count, freed_count, "all allocated nodes freed");
        }

        check_plan();
        footer();
}

int
main()
{
        out_of_memory_stress_test();
        return 0;
}
