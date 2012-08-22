#ifndef INCLUDES_TARANTOOL_TEST_UNIT_ROPE_COMMON_H
#define INCLUDES_TARANTOOL_TEST_UNIT_ROPE_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static inline void *
str_getn(void *data, size_t size, size_t offset)
{
	return (char *) data + offset;
}

static inline void
str_print(void *data, size_t n)
{
	printf("%.*s", (int) n, (char *) data);
}

static inline void *
mem_alloc(void *data, size_t size)
{
	(void) data;
	return malloc(size);
}

static inline void
mem_free(void *data, void *ptr)
{
	(void) data;
	free(ptr);
}

static inline struct rope *
test_rope_new()
{
	return rope_new(str_getn, mem_alloc, mem_free, NULL);
}

static inline void
test_rope_insert(struct rope *rope, rsize_t offset, char *str)
{
	printf("insert offset = %zu, str = '%s'\n", (size_t) offset, str);
	rope_insert(rope, offset, str, strlen(str));
	rope_pretty_print(rope, str_print);
	rope_check(rope);
}

static inline void
test_rope_erase(struct rope *rope, rsize_t offset)
{
	printf("erase offset = %zu\n", (size_t) offset);
	rope_erase(rope, offset);
	rope_pretty_print(rope, str_print);
	rope_check(rope);
}

#endif
