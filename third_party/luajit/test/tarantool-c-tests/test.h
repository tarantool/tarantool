#ifndef TARANTOOL_LUAJIT_TEST_H
#define TARANTOOL_LUAJIT_TEST_H

#include <stdio.h>
#include <stdlib.h>

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

/*
 * Test module, based on TAP 14 specification [1].
 * [1]: https://testanything.org/tap-version-14-specification.html
 * Version 13 is set for better compatibility on old machines.
 *
 * TODO:
 * * Helpers assert macros:
 *   - assert_uint_equal if needed
 *   - assert_uint_not_equal if needed
 *   - assert_memory_equal if needed
 *   - assert_memory_not_equal if needed
 * * Pragmas.
 */

#define TAP_VERSION 13

#define TEST_EXIT_SUCCESS 0
#define TEST_EXIT_FAILURE 1

#define TEST_JMP_STATUS_SHIFT 2
#define TEST_LJMP_EXIT_SUCCESS (TEST_EXIT_SUCCESS + TEST_JMP_STATUS_SHIFT)
#define TEST_LJMP_EXIT_FAILURE (TEST_EXIT_FAILURE + TEST_JMP_STATUS_SHIFT)

#define TEST_NORET __attribute__((noreturn))

typedef int (*test_func)(void *test_state);
struct test_unit {
	const char *name;
	test_func f;
};

/* API declaration. */

/*
 * Print formatted message with the corresponding indent.
 * If you want to leave a comment, use `test_comment()` instead.
 */
void test_message(const char *fmt, ...);

/* Need for `skip_all()`, please, don't use it. */
void _test_print_skip_all(const char *group_name, const char *reason);
/* End test via `longjmp()`, please, don't use it. */
TEST_NORET void _test_exit(int status);

void test_set_skip_reason(const char *reason);
void test_set_todo_reason(const char *reason);
/*
 * Save formatted diagnostic data. Each entry separated with \n.
 */
void test_save_diag_data(const char *fmt, ...);

/* Internal, it is better to use `test_run_group()` instead. */
int _test_run_group(const char *group_name, const struct test_unit tests[],
		    size_t n_tests, void *test_state);

/* Initialize `test_unit` structure. */
#define test_unit_def(f) {#f, f}

#define lengthof(arr) (sizeof(arr) / sizeof((arr)[0]))

/*
 * __func__ is the name for a test group, "main" for the parent
 * test.
 */
#define test_run_group(t_arr, t_state) \
	_test_run_group(__func__, t_arr, lengthof(t_arr), t_state)

#define SKIP_DIRECTIVE " # SKIP "
#define TODO_DIRECTIVE " # TODO "

#define skip_all(reason) ({						\
	_test_print_skip_all(__func__, reason);				\
	TEST_EXIT_SUCCESS;						\
})

static inline int skip(const char *reason)
{
	test_set_skip_reason(reason);
	return TEST_EXIT_SUCCESS;
}

static inline int todo(const char *reason)
{
	test_set_todo_reason(reason);
	return TEST_EXIT_FAILURE;
}

#define bail_out(reason) do {						\
	/*								\
	 * For backwards compatibility with TAP13 Harnesses,		\
	 * Producers _should_ emit a "Bail out!" line at the root	\
	 * indentation level whenever a Subtest bails out [1].		\
	 */								\
	printf("Bail out! %s\n", reason);				\
	exit(TEST_EXIT_FAILURE);					\
} while (0)

/* `fmt` should always be a format string here. */
#define test_comment(fmt, ...) test_message("# " fmt, __VA_ARGS__)

/*
 * This is a set of useful assert macros like the standard C
 * libary's assert(3) macro.
 *
 * On an assertion failure an assert macro will save the
 * diagnostic to the special buffer, to be reported via YAML
 * Diagnostic block and finish a test function with
 * `return TEST_EXIT_FAILURE`.
 *
 * Due to limitations of the C language `assert_true()` and
 * `assert_false()` macros can only display the expression that
 * caused the assertion failure. Type specific assert macros,
 * `assert_{type}_equal()` and `assert_{type}_not_equal()`, save
 * the data that caused the assertion failure which increases data
 * visibility aiding debugging of failing test cases.
 */

#define LOCATION_FMT "location:\t%s:%d\n"
#define ASSERT_NAME_FMT(name) "failed_assertion:\t" #name "\n"
#define ASSERT_EQUAL_FMT(name_type, type_fmt)				\
	LOCATION_FMT							\
	ASSERT_NAME_FMT(assert_ ## name_type ## _equal)			\
	"got: "      type_fmt "\n"					\
	"expected: " type_fmt "\n"

#define ASSERT_NOT_EQUAL_FMT(type_fmt)					\
	LOCATION_FMT							\
	ASSERT_NAME_FMT(assert_ ## name_type ## _not_equal)		\
	"got:   "      type_fmt "\n"					\
	"unexpected: " type_fmt "\n"

#define assert_true(cond) do {						\
	if (!(cond)) {							\
		test_save_diag_data(LOCATION_FMT			\
				    "condition_failed:\t'" #cond "'\n",	\
				    __FILE__, __LINE__);		\
		_test_exit(TEST_LJMP_EXIT_FAILURE);			\
	}								\
} while (0)

#define assert_false(cond) assert_true(!(cond))

#define assert_general(cond, fmt, ...) do {				\
	if (!(cond)) {							\
		test_save_diag_data(fmt, __VA_ARGS__);			\
		_test_exit(TEST_LJMP_EXIT_FAILURE);			\
	}								\
} while (0)

#define assert_ptr_equal(got, expected) do {				\
	assert_general((got) == (expected),				\
		       ASSERT_EQUAL_FMT(ptr, "%p"),			\
		       __FILE__, __LINE__, (got), (expected)		\
	);								\
} while (0)

#define assert_ptr_not_equal(got, unexpected) do {			\
	assert_general((got) != (unexpected),				\
		       ASSERT_NOT_EQUAL_FMT(ptr, "%p"),			\
		       __FILE__, __LINE__, (got), (unexpected)		\
	);								\
} while (0)


#define assert_int_equal(got, expected) do {				\
	assert_general((got) == (expected),				\
		       ASSERT_EQUAL_FMT(int, "%d"),			\
		       __FILE__, __LINE__, (got), (expected)		\
	);								\
} while (0)

#define assert_int_not_equal(got, unexpected) do {			\
	assert_general((got) != (unexpected),				\
		       ASSERT_NOT_EQUAL_FMT(int, "%d"),			\
		       __FILE__, __LINE__, (got), (unexpected)		\
	);								\
} while (0)

#define assert_sizet_equal(got, expected) do {				\
	assert_general((got) == (expected),				\
		       ASSERT_EQUAL_FMT(sizet, "%lu"),			\
		       __FILE__, __LINE__, (got), (expected)		\
	);								\
} while (0)

#define assert_sizet_not_equal(got, unexpected) do {			\
	assert_general((got) != (unexpected),				\
		       ASSERT_NOT_EQUAL_FMT(sizet, "%lu"),		\
		       __FILE__, __LINE__, (got), (unexpected)		\
	);								\
} while (0)

/* Check that doubles are __exactly__ the same. */
#define assert_double_equal(got, expected) do {				\
	assert_general((got) == (expected),				\
		       ASSERT_EQUAL_FMT(double, "%lf"),			\
		       __FILE__, __LINE__, (got), (expected)		\
	);								\
} while (0)

/* Check that doubles are not __exactly__ the same. */
#define assert_double_not_equal(got, unexpected) do {			\
	assert_general((got) != (unexpected),				\
		       ASSERT_NOT_EQUAL_FMT(double, "%lf"),		\
		       __FILE__, __LINE__, (got), (unexpected)		\
	);								\
} while (0)

#define assert_str_equal(got, expected) do {				\
	assert_general(strcmp(got, expected) == 0,			\
		       ASSERT_EQUAL_FMT(str, "%s"),			\
		       __FILE__, __LINE__, (got), (expected)		\
	);								\
} while (0)

#define assert_str_not_equal(got, unexpected) do {			\
	assert_general(strcmp(got, expected) != 0,			\
		       ASSERT_NOT_EQUAL_FMT(str, "%s"),			\
		       __FILE__, __LINE__, (got), (unexpected)		\
	);								\
} while (0)

#endif /* TARANTOOL_LUAJIT_TEST_H */
