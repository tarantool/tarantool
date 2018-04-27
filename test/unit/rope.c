#include "unit.h"
#include "rope_common.h"

static void
test_rope_extract(struct rope *rope, rope_size_t pos)
{
	printf("extract pos = %zu: ", (size_t) pos);
	struct rope_node *node = rope_extract_node(rope, pos);
	rope_check(rope);
	str_print(node->data, node->leaf_size);
	printf("\n");
}

static inline void
test_rope_cut(struct rope *rope, rope_size_t offset, rope_size_t size)
{
	printf("erase offset = %zu, size = %zu \n", (size_t) offset, (size_t) size);
	while (size-- > 0)
		rope_erase(rope, offset);
	rope_pretty_print(rope, str_print);
	rope_check(rope);
}


static void
test_rope()
{
	struct rope *rope = test_rope_new();
	test_rope_insert(rope, rope_size(rope), "who's gonna be");

	test_rope_insert(rope, rope_size(rope), "<Mr.X>");
	test_rope_insert(rope, rope_size(rope), ", Mr. <black!?!>Black");
	test_rope_insert(rope, rope_size(rope), ", but they <know-something-");

	test_rope_insert(rope, 0, "guys all ");

	test_rope_insert(rope, 9, "five fighting over ");
	test_rope_insert(rope, 0, "<yes, got got>You got four of ");
	test_rope_insert(rope, rope_size(rope), "special> don't know each other");
	test_rope_insert(rope, -1, ", so nobody wants to back.");
	test_rope_insert(rope, rope_size(rope) - 1, " down");
	test_rope_insert(rope, -1, "<point-point>");

	test_rope_cut(rope, 0,  5);
	test_rope_cut(rope, 0, 9);
	test_rope_cut(rope, 179, 7);
	test_rope_cut(rope, 173, 1);
	test_rope_cut(rope, 58, 7);
	test_rope_cut(rope, 63, 10);
	test_rope_cut(rope, 79, 25);
	test_rope_cut(rope, 25, 5);
	test_rope_cut(rope, 126, 5);

	test_rope_extract(rope, 0);
	test_rope_extract(rope, 5);
	test_rope_extract(rope, 19);
	test_rope_extract(rope, 59);
	test_rope_extract(rope, 124);

	rope_delete(rope);
}

int
main()
{
	test_rope();
	return 0;
}
