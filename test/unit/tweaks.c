#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "diag.h"
#include "fiber.h"
#include "memory.h"
#include "trivia/util.h"
#include "tweaks.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static bool bool_var = true;
TWEAK_BOOL(bool_var);

static int64_t int_var = 42;
TWEAK_INT(int_var);

static uint64_t uint_var = 123;
TWEAK_UINT(uint_var);

static double double_var = 3.14;
TWEAK_DOUBLE(double_var);

enum my_enum {
	MY_FOO,
	MY_BAR,
	my_enum_MAX,
};

static const char *const my_enum_strs[] = {
	"FOO",
	"BAR",
};

static enum my_enum enum_var = MY_BAR;
TWEAK_ENUM(my_enum, enum_var);

static void
test_lookup(void)
{
	plan(6);
	header();
	ok(tweak_find("no_such_var") == NULL, "no_such_var not found");
	ok(tweak_find("bool_var") != NULL, "bool_var found");
	ok(tweak_find("int_var") != NULL, "int_var found");
	ok(tweak_find("uint_var") != NULL, "uint_var found");
	ok(tweak_find("double_var") != NULL, "double_var found");
	ok(tweak_find("enum_var") != NULL, "enum_var found");
	footer();
	check_plan();
}

static bool
test_foreach_cb(const char *name, struct tweak *tweak, void *arg)
{
	(void)arg;
	struct tweak_value v;
	tweak_get(tweak, &v);
	if (strcmp(name, "bool_var") == 0) {
		is(v.type, TWEAK_VALUE_BOOL, "bool_var tweak value type");
		is(v.bval, true, "bool_var tweak value");
	} else if (strcmp(name, "int_var") == 0) {
		is(v.type, TWEAK_VALUE_INT, "int_var tweak value type");
		is(v.ival, 42, "int_var tweak value");
	} else if (strcmp(name, "uint_var") == 0) {
		is(v.type, TWEAK_VALUE_UINT, "uint_var tweak value type");
		is(v.ival, 123, "uint_var tweak value");
	} else if (strcmp(name, "double_var") == 0) {
		is(v.type, TWEAK_VALUE_DOUBLE, "double_var tweak value type");
		is(v.dval, 3.14, "double_var tweak value");
	} else if (strcmp(name, "enum_var") == 0) {
		is(v.type, TWEAK_VALUE_STR, "enum_var tweak value type");
		is(strcmp(v.sval, "BAR"), 0, "enum_var tweak value");
	}
	return true;
}

static void
test_foreach(void)
{
	plan(10);
	header();
	tweak_foreach(test_foreach_cb, NULL);
	footer();
	check_plan();
}

static bool
test_foreach_break_cb(const char *name, struct tweak *tweak, void *arg)
{
	(void)name;
	(void)tweak;
	int *count = arg;
	if (*count <= 0)
		return false;
	--*count;
	return true;
}

static void
test_foreach_break(void)
{
	plan(5);
	header();
	int count = 0;
	bool ret = tweak_foreach(test_foreach_break_cb, &count);
	ok(!ret, "iterate 0 ret");
	is(count, 0, "iterate 0 count");
	count = 2;
	ret = tweak_foreach(test_foreach_break_cb, &count);
	ok(!ret, "iterate 2 ret");
	is(count, 0, "iterate 2 count");
	count = 9000;
	ret = tweak_foreach(test_foreach_break_cb, &count);
	ok(ret, "iterate all ret");
	footer();
	check_plan();
}

static void
test_bool_var(void)
{
	plan(15);
	header();
	struct tweak *t;
	struct tweak_value v;
	t = tweak_find("bool_var");
	ok(t != NULL, "tweak found");
	tweak_get(t, &v);
	is(bool_var, true, "init var value");
	is(v.type, TWEAK_VALUE_BOOL, "init tweak value type");
	is(v.bval, true, "init tweak value");
	v.type = TWEAK_VALUE_INT;
	v.ival = 42;
	is(tweak_set(t, &v), -1, "set invalid tweak value type");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected boolean") == 0,
	   "diag after set invalid tweak value type");
	is(bool_var, true, "var value after failed set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_BOOL, "tweak value type after failed set");
	is(v.bval, true, "tweak value after failed set");
	v.type = TWEAK_VALUE_BOOL;
	v.bval = false;
	is(tweak_set(t, &v), 0, "set tweak value");
	is(bool_var, false, "var value after set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_BOOL, "tweak value type after set");
	is(v.bval, false, "tweak value after set");
	bool_var = true;
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_BOOL, "tweak value type after var update");
	is(v.bval, true, "tweak value after var update");
	footer();
	check_plan();
}

static void
test_int_var(void)
{
	plan(22);
	header();
	struct tweak *t;
	struct tweak_value v;
	t = tweak_find("int_var");
	ok(t != NULL, "tweak found");
	tweak_get(t, &v);
	is(int_var, 42, "init var value");
	is(v.type, TWEAK_VALUE_INT, "init tweak value type");
	is(v.ival, 42, "init tweak value");
	v.type = TWEAK_VALUE_BOOL;
	v.bval = true;
	is(tweak_set(t, &v), -1, "set invalid tweak value type");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected integer") == 0,
	   "diag after set invalid tweak value type");
	is(int_var, 42, "var value after failed set");
	v.type = TWEAK_VALUE_UINT;
	v.uval = (uint64_t)INT64_MAX + 1;
	is(tweak_set(t, &v), -1, "set too big value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, must be <= 9223372036854775807") == 0,
	   "diag after set too big value");
	is(int_var, 42, "var value after failed set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_INT, "tweak value type after failed set");
	is(v.ival, 42, "tweak value after failed set");
	v.type = TWEAK_VALUE_INT;
	v.ival = -11;
	is(tweak_set(t, &v), 0, "set tweak value to int");
	is(int_var, -11, "var value after set to int");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_INT, "tweak value type after set to int");
	is(v.ival, -11, "tweak value after set to int");
	v.type = TWEAK_VALUE_UINT;
	v.ival = 11;
	is(tweak_set(t, &v), 0, "set tweak value to uint");
	is(int_var, 11, "var value after set to uint");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_INT, "tweak value type after set to uint");
	is(v.ival, 11, "tweak value after set to uint");
	int_var = 42;
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_INT, "tweak value type after var update");
	is(v.ival, 42, "tweak value after var update");
	footer();
	check_plan();
}

static void
test_uint_var(void)
{
	plan(22);
	header();
	struct tweak *t;
	struct tweak_value v;
	t = tweak_find("uint_var");
	ok(t != NULL, "tweak found");
	tweak_get(t, &v);
	is(uint_var, 123, "init var value");
	is(v.type, TWEAK_VALUE_UINT, "init tweak value type");
	is(v.uval, 123, "init tweak value");
	v.type = TWEAK_VALUE_BOOL;
	v.bval = true;
	is(tweak_set(t, &v), -1, "set invalid tweak value type");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected integer") == 0,
	   "diag after set invalid tweak value type");
	is(uint_var, 123, "var value after failed set");
	v.type = TWEAK_VALUE_INT;
	v.ival = -1;
	is(tweak_set(t, &v), -1, "set negative value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, must be >= 0") == 0,
	   "diag after set negative value");
	is(uint_var, 123, "var value after failed set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_UINT, "tweak value type after failed set");
	is(v.uval, 123, "tweak value after failed set");
	v.type = TWEAK_VALUE_INT;
	v.ival = 11;
	is(tweak_set(t, &v), 0, "set tweak value to int");
	is(uint_var, 11, "var value after set to int");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_UINT, "tweak value type after set to int");
	is(v.uval, 11, "tweak value after set to int");
	v.type = TWEAK_VALUE_UINT;
	v.uval = 22;
	is(tweak_set(t, &v), 0, "set tweak value to uint");
	is(uint_var, 22, "var value after set to uint");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_UINT, "tweak value type after set to uint");
	is(v.uval, 22, "tweak value after set to uint");
	uint_var = 123;
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_UINT, "tweak value type after var update");
	is(v.uval, 123, "tweak value after var update");
	footer();
	check_plan();
}

static void
test_double_var(void)
{
	plan(23);
	header();
	struct tweak *t;
	struct tweak_value v;
	t = tweak_find("double_var");
	ok(t != NULL, "tweak found");
	tweak_get(t, &v);
	is(double_var, 3.14, "init var value");
	is(v.type, TWEAK_VALUE_DOUBLE, "init tweak value type");
	is(v.dval, 3.14, "init tweak value");
	v.type = TWEAK_VALUE_BOOL;
	v.bval = true;
	is(tweak_set(t, &v), -1, "set invalid tweak value type");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected number") == 0,
	   "diag after set invalid tweak value type");
	is(double_var, 3.14, "var value after failed set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_DOUBLE, "tweak value type after failed set");
	is(v.dval, 3.14, "tweak value after failed set");
	v.type = TWEAK_VALUE_INT;
	v.ival = -11;
	is(tweak_set(t, &v), 0, "set tweak value to int");
	is(double_var, -11, "var value after set to int");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_DOUBLE, "tweak value type after set to int");
	is(v.dval, -11, "tweak value after set to int");
	v.type = TWEAK_VALUE_UINT;
	v.uval = 11;
	is(tweak_set(t, &v), 0, "set tweak value to uint");
	is(double_var, 11, "var value after set to uint");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_DOUBLE, "tweak value type after set to uint");
	is(v.dval, 11, "tweak value after set to uint");
	v.type = TWEAK_VALUE_DOUBLE;
	v.dval = 0.5;
	is(tweak_set(t, &v), 0, "set tweak value to double");
	is(double_var, 0.5, "var value after set to double");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_DOUBLE, "tweak value type after set to double");
	is(v.dval, 0.5, "tweak value after set to double");
	double_var = 3.14;
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_DOUBLE, "tweak value type after var update");
	is(v.dval, 3.14, "tweak value after var update");
	footer();
	check_plan();
}

static void
test_enum_var(void)
{
	plan(17);
	header();
	struct tweak *t;
	struct tweak_value v;
	t = tweak_find("enum_var");
	ok(t != NULL, "tweak found");
	tweak_get(t, &v);
	is(enum_var, MY_BAR, "init var value");
	is(v.type, TWEAK_VALUE_STR, "init tweak value type");
	is(strcmp(v.sval, "BAR"), 0, "init tweak value");
	v.type = TWEAK_VALUE_INT;
	v.ival = 123;
	is(tweak_set(t, &v), -1, "set invalid tweak value type");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected one of: 'FOO', 'BAR'") == 0,
	   "diag after set invalid tweak value type");
	v.type = TWEAK_VALUE_STR;
	v.sval = "FUZZ";
	is(tweak_set(t, &v), -1, "set invalid tweak value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected one of: 'FOO', 'BAR'") == 0,
	   "diag after set invalid tweak value");
	is(enum_var, MY_BAR, "var value after failed set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_STR, "tweak value type after failed set");
	is(strcmp(v.sval, "BAR"), 0, "tweak value after failed set");
	v.type = TWEAK_VALUE_STR;
	v.sval = "FOO";
	is(tweak_set(t, &v), 0, "set tweak value");
	is(enum_var, MY_FOO, "var value after set");
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_STR, "tweak value type after set");
	is(strcmp(v.sval, "FOO"), 0, "tweak value after set");
	enum_var = MY_BAR;
	tweak_get(t, &v);
	is(v.type, TWEAK_VALUE_STR, "tweak value type after var update");
	is(strcmp(v.sval, "BAR"), 0, "tweak value after var update");
	footer();
	check_plan();
}

static int
test_tweaks(void)
{
	plan(8);
	header();
	test_lookup();
	test_foreach();
	test_foreach_break();
	test_bool_var();
	test_int_var();
	test_uint_var();
	test_double_var();
	test_enum_var();
	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	int rc = test_tweaks();
	fiber_free();
	memory_free();
	return rc;
}
