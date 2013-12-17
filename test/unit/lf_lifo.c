#include "small/lf_lifo.h"
#include "unit.h"
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

static void *
mmap_aligned(size_t size)
{
	assert((size & (size - 1)) == 0);
        void *map = mmap(NULL, 2 * size,
                         PROT_READ | PROT_WRITE, MAP_PRIVATE |
                         MAP_ANONYMOUS, -1, 0);

        /* Align the mapped address around slab size. */
        size_t offset = (intptr_t) map & (size - 1);

        if (offset != 0) {
                munmap(map, size - offset);
                map += size - offset;
                munmap(map + size, offset);
        } else {
                /* The address is returned aligned. */
                munmap(map + size, size);
        }
        return map;
}

#define MAP_SIZE 0x10000

int main()
{
	struct lf_lifo head;
	void *val1 = mmap_aligned(MAP_SIZE);
	void *val2 = mmap_aligned(MAP_SIZE);
	void *val3 = mmap_aligned(MAP_SIZE);
	lf_lifo_init(&head);

	fail_unless(lf_lifo_pop(&head) == NULL);
	fail_unless(lf_lifo_pop(lf_lifo_push(&head, val1)) == val1);
	fail_unless(lf_lifo_pop(lf_lifo_push(&head, val1)) == val1);
	lf_lifo_push(lf_lifo_push(lf_lifo_push(&head, val1), val2), val3);
	fail_unless(lf_lifo_pop(&head) == val3);
	fail_unless(lf_lifo_pop(&head) == val2);
	fail_unless(lf_lifo_pop(&head) == val1);
	fail_unless(lf_lifo_pop(&head) == NULL);

	lf_lifo_init(&head);

	/* Test overflow of ABA counter. */

	int i = 0;
	do {
		lf_lifo_push(&head, val1);
		fail_unless(lf_lifo_pop(&head) == val1);
		fail_unless(lf_lifo_pop(&head) == NULL);
		i++;
	} while (head.next != 0);

	munmap(val1, MAP_SIZE);
	munmap(val2, MAP_SIZE);
	munmap(val3, MAP_SIZE);

	printf("success\n");

	return 0;
}
