#include <rope.h>
#include <time.h>
#include "unit.h"
#include "rope_common.h"

static char *data[] = {"a", "bc", "def", "ghij", "klmno"};

static void
test_rope_stress_small()
{
	header();

	struct rope *rope = rope_new(str_getn, mem_alloc, mem_free, NULL);
	const int iterations = 500;
	int i = 0;
	for (i = 0; i < iterations; ++i) {
		char *d = data[((rsize_t) rand())%5];
		int len = strlen(d);
		rsize_t size = rope_size(rope);
		rsize_t offset = ((rsize_t) rand()) % (size + 1);
		rope_insert(rope, offset, d, len);
		fail_unless(size + len == rope_size(rope));
		rope_check(rope);
		size = rope_size(rope);
		offset = ((rsize_t) rand()) % size;
		if (offset == size)
			offset--;
		rope_erase(rope, offset);
		fail_unless(size == rope_size(rope) + 1);
		rope_check(rope);
	}
	rope_delete(rope);

	footer();
}

static void
test_rope_stress_large()
{
	header();

	struct rope *rope = rope_new(str_getn, mem_alloc, mem_free, NULL);
	const int iterations = 50000;
	int i = 0;
	for (i = 0; i < iterations; ++i) {
		char *d = data[((rsize_t) rand())%5];
		int len = strlen(d);
		rsize_t size = rope_size(rope);
		rsize_t offset = ((rsize_t) rand()) % (size + 1);
		rope_insert(rope, offset, d, len);
		fail_unless(size + len == rope_size(rope));
		size = rope_size(rope);
		offset = ((rsize_t) rand()) % size;
		if (offset == size)
			offset--;
		rope_erase(rope, offset);
		fail_unless(size == rope_size(rope) + 1);
		if (i % 1000 == 0)
			rope_check(rope);
	}
	rope_delete(rope);

	footer();
}
int
main()
{
	srand(time(NULL));
	test_rope_stress_small();
	test_rope_stress_large();
	return 0;
}
