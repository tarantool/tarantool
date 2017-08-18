#include "unit.h"

#include <string.h>
#include "reflection.h"

extern const struct type_info type_Object;
struct Object {
	Object()
		: type(&type_Object)
	{}

	virtual ~Object()
	{}

	const struct type_info *type;
	Object(const struct type_info *type_arg)
		: type(type_arg)
	{}
};
const struct type_info type_Object = make_type("Object", NULL);

extern const struct type_info type_Database;
struct Database: public Object {
	Database()
		: Object(&type_Database),
		  m_int(0),
		  m_str{'\0'}
	{}

	virtual const char *
	getString() const
	{
		return m_str;
	}

	virtual void
	putString(const char *str)
	{
		snprintf(m_str, sizeof(m_str), "%s", str);
	}

	virtual int
	getInt() const
	{
		return m_int;
	}

	virtual void
	putInt(int val) {
		m_int = val;
	}
protected:
	Database(const struct type_info *type)
		: Object(type),
		  m_int(0),
		  m_str{'\0'}
	{}
	int m_int;
	char m_str[128];
};
static const struct method_info database_methods[] = {
	make_method(&type_Database, "getString", &Database::getString),
	make_method(&type_Database, "getInt", &Database::getInt),
	make_method(&type_Database, "putString", &Database::putString),
	make_method(&type_Database, "putInt", &Database::putInt),
	METHODS_SENTINEL
};
const struct type_info type_Database = make_type("Database", &type_Object,
	database_methods);

extern const struct type_info type_Tarantool;
struct Tarantool: public Database {
	Tarantool()
		: Database(&type_Tarantool)
	{}

	void inc() {
		++m_int;
	}
};
static const struct method_info tarantool_methods[] = {
	make_method(&type_Tarantool, "inc", &Tarantool::inc),
	METHODS_SENTINEL
};
const struct type_info type_Tarantool = make_type("Tarantool", &type_Database,
	tarantool_methods);

int
main()
{
	plan(30);

	Object obj;
	Tarantool tntobj;
	const struct method_info *get_string = type_method_by_name(tntobj.type,
		"getString");
	const struct method_info *put_string = type_method_by_name(tntobj.type,
		"putString");
	const struct method_info *get_int = type_method_by_name(tntobj.type,
		"getInt");
	const struct method_info *put_int = type_method_by_name(tntobj.type,
		"putInt");
	const struct method_info *inc = type_method_by_name(tntobj.type,
		"inc");

	/* struct type_info members */
	ok(strcmp(type_Object.name, "Object") == 0, "type.name");
	is(type_Object.parent, NULL, "type.parent");
	is(type_Database.parent, &type_Object, "type.parent");

	/* inheritance */
	ok(type_assignable(&type_Object, &type_Tarantool), "is_instance");
	ok(type_assignable(&type_Database, &type_Tarantool), "is_instance");
	ok(type_assignable(&type_Tarantool, &type_Tarantool), "is_instance");
	ok(!type_assignable(&type_Tarantool, &type_Database), "is_instance");

	/* methods */
	const char *methods_order[] = {
		"inc",
		"getString",
		"getInt",
		"putString",
		"putInt"
	};
	int i = 0;
	type_foreach_method(&type_Tarantool, method) {
		ok(strcmp(method->name, methods_order[i]) == 0, "methods order");
		++i;
	}


	/*
	 * struct method_info members
	 */
	is(get_string->owner, &type_Database, "method.owner");
	ok(strcmp(get_string->name, "getString") == 0, "method.name");
	is(get_string->rtype, CTYPE_CONST_CHAR_PTR, "method.rtype (non void)");
	is(put_string->rtype, CTYPE_VOID, "method.rtype (void)");
	is(get_string->nargs, 0, "method.nargs (zero)");
	is(put_string->nargs, 1, "method.nargs (non-zero)");
	is(put_string->atype[0], CTYPE_CONST_CHAR_PTR, "method.atype");
	is(get_string->isconst, true, "method.isconst");
	is(put_string->isconst, false, "!method.isconst");

	/*
	 * Invokable
	 */
	ok(!method_invokable<int>(get_string, &tntobj),
		"!invokable<invalid args>");
	ok(!(method_invokable<const char *, int> (get_string, &tntobj)),
		"!invokable<extra args>");
	ok(!method_invokable<int>(get_string, &obj),
		"!invokable<>(invalid object)");
	ok(method_invokable<const char *>(get_string, &tntobj),
		"invokable<const char *>");
	ok((method_invokable<void, const char *>(put_string, &tntobj)),
		"invokable<void, const char *>");

	/*
	 * Invoke
	 */

	/* int */
	method_invoke<void, int>(put_int, &tntobj, 48);
	int iret = method_invoke<int>(get_int, &tntobj);
	is(iret, 48, "invoke (int)");

	/* const char */
	method_invoke<void, const char *>(put_string, &tntobj, "test string");
	const char *sret = method_invoke<const char *>(get_string, &tntobj);
	ok(strcmp(sret, "test string") == 0, "invoke (const char *)");

	method_invoke<void>(inc, &tntobj);
	iret = method_invoke<int>(get_int, &tntobj);
	is(iret, 49, "invoke (void)");

	const Tarantool *tntconstptr = &tntobj;
	ok((!method_invokable<void, const char *>(put_string, tntconstptr)),
		"!invokable<>() on const method with non-const object");

	return check_plan();
}
