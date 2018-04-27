#include <time.h>
#include "unit.h"
#include "rope_common.h"

static char *data[] = {"a", "bc", "def", "ghij", "klmno"};

static void
test_rope_stress_small()
{
	header();

	struct rope *rope = test_rope_new();
	const int iterations = 500;
	int i = 0;
	for (i = 0; i < iterations; ++i) {
		char *d = data[((rope_size_t) rand())%5];
		int len = strlen(d);
		rope_size_t size = rope_size(rope);
		rope_size_t offset = ((rope_size_t) rand()) % (size + 1);
		rope_insert(rope, offset, d, len);
		fail_unless(size + len == rope_size(rope));
		rope_check(rope);
		size = rope_size(rope);
		offset = ((rope_size_t) rand()) % size;
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

	struct rope *rope = test_rope_new();
	const int iterations = 50000;
	int i = 0;
	for (i = 0; i < iterations; ++i) {
		char *d = data[((rope_size_t) rand())%5];
		int len = strlen(d);
		rope_size_t size = rope_size(rope);
		rope_size_t offset = ((rope_size_t) rand()) % (size + 1);
		rope_insert(rope, offset, d, len);
		fail_unless(size + len == rope_size(rope));
		size = rope_size(rope);
		offset = ((rope_size_t) rand()) % size;
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
