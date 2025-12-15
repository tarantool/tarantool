#include "test.h"

/*
 * Test module, based on TAP 14 specification [1].
 * [1]: https://testanything.org/tap-version-14-specification.html
 */

/* Need for `PATH_MAX` in diagnostic definition. */
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
/* Need for `strchr()` in diagnostic parsing. */
#include <string.h>

/*
 * Test level: 0 for the parent test, >0 for any subtests.
 */
static int level = -1;

/*
 * The last diagnostic data to be used in the YAML Diagnostic
 * block.
 *
 * Contains filename, line number and failed expression or assert
 * name and "got" and "expected" fields. All entries are separated
 * by \n.
 * The longest field is filename here, so PATH_MAX * 3 as
 * the diagnostic string length should be enough.
 *
 * The first \0 means the end of diagnostic data.
 *
 * As far as `strchr()` searches until \0, all previous entries
 * are suppressed by the last one. If the first byte is \0 --
 * diagnostic is empty.
 */
#define TEST_DIAG_DATA_MAX (PATH_MAX * 3)
char test_diag_buf[TEST_DIAG_DATA_MAX] = {0};

const char *skip_reason = NULL;
const char *todo_reason = NULL;

/* Indent for the TAP. 4 spaces is default for subtest. */
static void indent(void)
{
	int i;
	for (i = 0; i < level; i++)
		printf("    ");
}

void test_message(const char *fmt, ...)
{
	va_list ap;
	indent();
	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static void test_print_tap_version(void)
{
	/*
	 * Since several TAP13 parsers in popular usage treat
	 * a repeated Version declaration as an error, even if the
	 * Version is indented, Subtests _should not_ include a
	 * Version, if TAP13 Harness compatibility is
	 * desirable [1].
	 */
	if (level == 0)
		test_message("TAP version %d", TAP_VERSION);
}

static void test_start_comment(const char *t_name)
{
	if (level > -1)
		/*
		 * Inform about starting subtest, easier for
		 * humans to read.
		 * Subtest with a name must be terminated by a
		 * Test Point with a matching Description [1].
		 */
		test_comment("Subtest: %s", t_name);
}

void _test_print_skip_all(const char *group_name, const char *reason)
{
	test_start_comment(group_name);
	/*
	 * XXX: This test isn't started yet, so set indent level
	 * manually.
	 */
	level++;
	test_print_tap_version();
	/*
	 * XXX: `SKIP_DIRECTIVE` is not necessary here according
	 * to the TAP14 specification [1], but some harnesses may
	 * fail to parse the output without it.
	 */
	test_message("1..0" SKIP_DIRECTIVE "%s", reason);
	level--;
}

/* Just inform TAP parser how many tests we want to run. */
static void test_plan(size_t planned)
{
	test_message("1..%lu", planned);
}

/* Human-readable output how many tests/subtests are failed. */
static void test_finish(size_t planned, size_t failed)
{
	const char *t_type = level == 0 ? "tests" : "subtests";
	if (failed > 0)
		test_comment("Failed %lu %s out of %lu",
		     failed, t_type, planned);
}

void test_set_skip_reason(const char *reason)
{
	skip_reason = reason;
}

void test_set_todo_reason(const char *reason)
{
	todo_reason = reason;
}

void test_save_diag_data(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(test_diag_buf, TEST_DIAG_DATA_MAX, fmt, ap);
	va_end(ap);
}

static void test_clear_diag_data(void)
{
	/*
	 * Limit buffer with zero byte to show that there is no
	 * any entry.
	 */
	test_diag_buf[0] = '\0';
}

static int test_diagnostic_is_set(void)
{
	return test_diag_buf[0] != '\0';
}

/*
 * Parse the last diagnostic data entry and print it in YAML
 * format with the corresponding additional half-indent in TAP
 * (2 spaces).
 * Clear diagnostic message to be sure that it's printed once.
 * XXX: \n separators are changed to \0 during parsing and
 * printing output for convenience in usage.
 */
static void test_diagnostic(void)
{
	test_message("  ---");
	char *ent = test_diag_buf;
	char *ent_end = NULL;
	while ((ent_end = strchr(ent, '\n')) != NULL) {
		char *next_ent = ent_end + 1;
		/*
		 * Limit string with the zero byte for
		 * formatted output. Anyway, don't need this \n
		 * anymore.
		 */
		*ent_end = '\0';
		test_message("  %s", ent);
		ent = next_ent;
	}
	test_message("  ...");
	test_clear_diag_data();
}

static jmp_buf test_run_env;

TEST_NORET void _test_exit(int status)
{
	longjmp(test_run_env, status);
}

static int test_run(const struct test_unit *test, size_t test_number,
		    void *test_state)
{
	int status = TEST_EXIT_SUCCESS;
	/*
	 * Run unit test. Diagnostic in case of failure setup by
	 * helpers assert macros defined in the header.
	 */
	int jmp_status;
	if ((jmp_status = setjmp(test_run_env)) == 0) {
		if (test->f(test_state) != TEST_EXIT_SUCCESS)
			status = TEST_EXIT_FAILURE;
	} else {
		status = jmp_status - TEST_JMP_STATUS_SHIFT;
	}
	const char *result = status == TEST_EXIT_SUCCESS ? "ok" : "not ok";

	/*
	 * Format suffix of the test message for SKIP or TODO
	 * directives.
	 */
#define SUFFIX_SZ 1024
	char suffix[SUFFIX_SZ] = {0};
	if (skip_reason) {
		snprintf(suffix, SUFFIX_SZ, SKIP_DIRECTIVE "%s", skip_reason);
		skip_reason = NULL;
	} else if (todo_reason) {
		/* Prevent count this test as failed. */
		status = TEST_EXIT_SUCCESS;
		snprintf(suffix, SUFFIX_SZ, TODO_DIRECTIVE "%s", todo_reason);
		todo_reason = NULL;
	}
#undef SUFFIX_SZ

	test_message("%s %lu - %s%s", result, test_number, test->name,
		     suffix);

	if (status && test_diagnostic_is_set())
		test_diagnostic();
	return status;
}

int _test_run_group(const char *group_name, const struct test_unit tests[],
		    size_t n_tests, void *test_state)
{
	/*
	 * XXX: Disable buffering for stdout to not mess with the
	 * output in case there are forking tests in the group.
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

	test_start_comment(group_name);

	level++;
	test_print_tap_version();

	test_plan(n_tests);

	size_t n_failed = 0;

	size_t i;
	for (i = 0; i < n_tests; i++) {
		size_t test_number = i + 1;
		/* Return 1 on failure, 0 on success. */
		n_failed += test_run(&tests[i], test_number, test_state);
	}

	test_finish(n_tests, n_failed);

	level--;
	return n_failed > 0 ? TEST_EXIT_FAILURE : TEST_EXIT_SUCCESS;
}
