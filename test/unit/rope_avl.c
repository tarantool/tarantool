#include "unit.h"
#include "rope_common.h"

/******************************************************************/

static void
test_avl_rotations()
{
	header();

	struct rope *rope = test_rope_new();

	/* counterclockwise single rotation. */
	test_rope_insert(rope, 0, "1");
	test_rope_insert(rope, 1, "2");
	test_rope_insert(rope, 2, "<");

	/* clockwise single rotation */
	test_rope_insert(rope, 0, "0");
	test_rope_insert(rope, 0, ">");

	/* counterclockwise double rotation */
	test_rope_insert(rope, 1, "*");
	/* clocckwise double rotation */
	test_rope_insert(rope, 3, "p");
	test_rope_insert(rope, 3, "p");
	test_rope_insert(rope, 3, "p");
	test_rope_insert(rope, 3, "p");
	test_rope_insert(rope, 3, "*");

	rope_delete(rope);

	footer();
}

int
main()
{
	test_avl_rotations();
	return 0;
}
