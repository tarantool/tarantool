#include "reflection.h"

#include "unit.h"

static struct type_info type_Object = {
	.parent = NULL,
	.name = "Object",
	.methods = NULL,
};
static struct type_info type_Database = {
	.parent = &type_Object,
	.name = "Database",
	.methods = NULL,
};
static struct type_info type_Tarantool = {
	.parent = &type_Database,
	.name = "Tarantool",
	.methods = NULL
};

int
main()
{
	plan(4);

	/* inheritance */
	ok(type_assignable(&type_Object, &type_Tarantool), "assignable");
	ok(type_assignable(&type_Database, &type_Tarantool), "assignable");
	ok(type_assignable(&type_Tarantool, &type_Tarantool), "assignable");
	ok(!type_assignable(&type_Tarantool, &type_Database), "assignable");

	return check_plan();
}
