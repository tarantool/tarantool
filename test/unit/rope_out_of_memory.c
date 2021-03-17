#include "unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocated;
static size_t freed;
static size_t remaining_memory;

static inline void *
mem_alloc_sanitizer(void *data, size_t size)
{
    (void) data;
    if (remaining_memory <= 0){ return NULL;}
    allocated ++;
    remaining_memory--;
    return malloc(size);
}

static inline void
mem_free_sanitizer(void *data, void *ptr)
{
    freed++;
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
prepare_test(int available_memory){
    remaining_memory = available_memory;
    allocated = 0;
    freed = 0;
    return rope_new(NULL);;
}

static void
lots_of_memory_test(){
    header();
    plan(1);

    struct rope *rope = prepare_test(100);
    rope_insert(rope, rope_size(rope), "a", 1);
    rope_insert(rope, rope_size(rope), "a", 1);
    rope_insert(rope, rope_size(rope), "a", 1);
    rope_delete(rope);

    is(allocated, freed, "allocated == freed");
    check_plan();
    footer();
}

static void
out_of_memory_stress_test(){
    header();
    plan(90);

    for(int memory = 10; memory < 100; ++memory){
        struct rope *rope = prepare_test(memory);
        for(int insert_num = 0; insert_num < memory + 10; ++insert_num){
            rope_insert(rope, rope_size(rope) / 2, "abacaba", 7);
        }
        rope_delete(rope);
        is(allocated, freed, "allocated == freed");
    }

    check_plan();
    footer();
}

int
main()
{
    lots_of_memory_test();
    out_of_memory_stress_test();
    return 0;
}