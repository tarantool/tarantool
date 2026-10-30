#include "unit.h"
#include "rope_common.h"

/******************************************************************/

static void
test_empty_rope()
{
	header();

	struct rope *rope = test_rope_new();

	fail_unless(rope_size(rope) == 0);

	struct rope_iter *iter = rope_iter_new(rope);

	fail_unless(rope_iter_start(iter) == NULL);
	fail_unless(rope_iter_start(iter) == NULL);

	rope_traverse(rope, str_print, NULL);
	rope_check(rope);
	rope_pretty_print(rope, str_print, NULL);

	/* rope_erase(), rope_extract() expect a non-empty rope */

	rope_iter_delete(iter);
	rope_delete(rope);

	footer();
}

static void
test_prepend()
{
	header();

	struct rope *rope = test_rope_new();
	test_rope_insert(rope, 0, " c ");
	test_rope_insert(rope, 0, " b ");
	test_rope_insert(rope, 0, " a ");
	rope_delete(rope);

	footer();
}

static void
test_append()
{
	header();

	struct rope *rope = test_rope_new();
	test_rope_insert(rope, rope_size(rope), " a ");
	test_rope_insert(rope, rope_size(rope), " b ");
	test_rope_insert(rope, rope_size(rope), " c ");
	rope_delete(rope);

	footer();
}

static void
test_insert()
{
	header();

	struct rope *rope = test_rope_new();

	test_rope_insert(rope, rope_size(rope), "   a ");
	test_rope_insert(rope, rope_size(rope) - 1, "b ");
	test_rope_insert(rope, rope_size(rope) - 2, "c ");
	test_rope_insert(rope, 1, " ");
	test_rope_insert(rope, rope_size(rope) - 1, " ");
	test_rope_insert(rope, 4, "*");
	test_rope_insert(rope, 8, "*");

	rope_delete(rope);

	footer();
}

static void
test_erase()
{
	header();

	struct rope *rope = test_rope_new();
	rope_insert(rope, rope_size(rope), "abcdefjhij", 10);
	test_rope_erase(rope, 4, 1);
	/* Rope nodes are "abcd" "fjhij". */
	test_rope_erase(rope, 3, 2);
	/* Rope nodes are "abc" "jhij". */
	test_rope_erase(rope, 4, 1);
	/* Rope nodes are "abc" "j" "ij". */
	test_rope_erase(rope, 2, 4);
	/* Rope nodes are "ab". */
	test_rope_erase(rope, 0, 2);

	rope_delete(rope);

	footer();
}

int
main()
{
	test_empty_rope();
	test_append();
	test_prepend();
	test_insert();
	test_erase();
	return 0;
}
