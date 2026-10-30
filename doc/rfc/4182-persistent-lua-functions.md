# Persistent functions in Tarantool

* **Status**: In progress
* **Start date**: 10-05-2019
* **Authors**: Kirill Shcherbatov @kshcherbatov kshcherbatov@tarantool.org, Vladimir Davydov @locker vdavydov.dev@gmail.com, Konstantin Osipov @kostja kostja@tarantool.org
* **Issues**: [#4182](https://github.com/tarantool/tarantool/issues/4182), [#1260](https://github.com/tarantool/tarantool/issues/1260)

## Summary
Persistent Lua functions in Tarantool are Lua functions that are part of
the schema and are thus immediately available after restart.

## Background and motivation
Today Lua functions defined in Tarantool are a part of the runtime environment
and must be defined again after program restart.  We need to introduce a
machinery to persist them (make them a part of the database state). This is
a useful feature itself and moreover it is a dependency for functional
indexes: all parts of a functional index definition must be persisted in
database schema.

## Detailed design
Let's store function definition in _func and automatically load/define
function on bootstrap (as well as replicate it).

### Extend schema with some new fields
We need to extend _func:format with new fields:
1. name: ``id``\
   type: ``unsigned``\
   Unique routine id
2. name: ``owner``\
   type: ``unsigned``\
   The object owner
3. name: ``name``\
   type: ``string``\
   collation: ``unicode_ci``\
   Routine name
4. name: ``stuid``\
   type: ``unsigned``\
   This makes Tarantool treat the function’s caller as the function’s creator.
5. name: ``language``\
   type: ``string``\
   default: ``LUA``\
   Procedure language -  `LUA`, `C`, `SQL`(reserved)
6. name: ``body``\
   type: ``string``\
   default: ``''``\
   Function language-dependent body
7. name: ``routine_type``\
   type: ``string``\
   default: ``FUNCTION``\
   Type of registered object: `FUNCTION` or `PROCEDURE`; The procedure is a function that never returns result.
8. name: ``param_list``\
   type: ``map``\
   is_nullable: ``true``\
   default: ``{}``\
   An array of maps describing arguments e.g. {name = `a`, type = `string`}. `{}`(undefined) by default.
   This field will be usefull when static field types validation is performed for function arguments.
9. name: ``returns``\
   type: ``string``\
   default: ``any``\
   A field_type-compatible string describing the returned value type.
10. name: ``aggregate``\
    type: ``string``\
    default: ``NONE``\
    Whether this routine is an SQL aggregate function: `NONE` or `GROUP`.
11. name: ``sql_data_access``\
    type: ``string``\
    default: ``NONE``\
    Returns one of the following values:\
    `NONE` = Function does not contain SQL.\
    `CONTAINS` = Function possibly contains SQL.\
    `READS` = Function possibly reads SQL data.\
    `MODIFIES` = Function possibly modifies SQL data.
12. name: ``is_deterministic``\
    type: ``boolean``\
    default: ``false``\
    Whether the routine is deterministic (can produce only one result for a given list of parameters) or not.
13. name: ``is_sandboxed``\
    type: ``boolean``\
    default: ``false``\
    Whether the Lua routine should be executed in isolated environment with limited number of available modules. This option is compatible only with Lua function.
14. name: ``is_null_call``\
    type: ``boolean``\
    default: ``true``\
    Indicates whether the routine will be called if any one of its arguments is NULL. This option is compatible only with SQL functions.
14. name: ``exports``\
    type: ``array``\
    default: ``{'Lua'}``\
    An array of `enum('LUA', 'SQL')` strings - the Tarantool's frontends where new function must be also available.
15. name: ``opts``\
    type: ``map``\
    default: ``{}``\
    Options map (reserved).
16. name: ``comment``\
    type: ``string``\
    default: ``''``
    Comment associated with the routine.
17. name: ``created``\
    type: ``string``\
    Date and time the routine was created (format `0000-00-00 00:00:00`).
18. name: ``last_altered``\
    type: ``string``\
    Date and time the routine was modified (format `0000-00-00 00:00:00`). Initially `last_altered == created`.

The updated interface to create a function in Tarantool is:
```
body_string = [[function(a, b) return a + b end]]

box.schema.func.create('funcname',
	<if_not_exists = boolean [false]>,
    <setuid = boolean [false]>,
    <language = enum('LUA', 'C') ['LUA']>,
	<body = string [''],
    <is_deterministic = boolean [false]>,
	<is_sandboxed = boolean [false]>,
    <returns = string ['any']>
    <param_list = array [{}]>,
    <comment = string ['']>'
})
```

### Persistent functions in Lua
Persistent Lua function object must be compiled and expored into Lua on
insertion into the system table (in contrast with a C function, which .so
file is loaded on first use). This is necessary to export a name referencing
an entry in the 'persistent' table and to safely construct a
sandbox, as well as verify function body. The function body still may be invalid,
or undefined, but there's nothing we can do about it. This impacts
functional indexes, which need to evaluate the function to fill the index:
such index may still not be constructible even if its function is defined.

The function creation process could also be unsafe. To create a new
function object, we must evaluate an expression given by a user.
This expression may be invalid, for example `body = 'fiber.yield()'` instead
of `body = 'function() return fiber.yield() end`. Tarantool thus will
load such function definitions using a Lua sandbox.

#### Sandboxing
Persistent Lua functions mustn't refer to runtime environment objects like
global variables or Lua modules: using these in a function may lead to a
yield, event loop stalls, or break completely if such module or object is
not available when a function is invoked. The function itself may leave
traces on the global environment, which is also undesirable. 
To address these issues, Tarantool will for now only support functions with
`is_sandboxed = true` property. For such functions it will
create a **unique** sandbox via `setfenv` for each function object when
loading it.
The sandbox will provide access to the following functions and modules:
```
assert, error, type, unpack, pairs, ipairs, pcall, xpcall,
print, next, select, string, table, tonumber, math, utf8
```

All Lua sandboxed functions are expected to be stateless.

#### Privileges
The reworked persistent Lua functions use the same security model as
was implemented for functions not having a body. All access checks are
performed on each function call. When ``setuid`` field is defined, the id of
the user is a set-definer-uid one.

### SQL Functions

#### Background
Currently Tarantool has a ``box.internal.sql_create_function`` mechanism  to
make Lua functions callable from SQL statements.
```
sql_create_function("func_name", "type", func_lua_object,
                    func_arg_num, is_deterministic);
```
That internally calls
```
int
sql_create_function_v2(sql * db, const char *zFunc,
			enum field_type returns, int nArg,
			int flags,
			void *p,
			void (*xSFunc) (sql_context *, int,
					sql_value **),
			void (*xStep) (sql_context *, int,
					  sql_value **),
			void (*xFinal) (sql_context *),
			void (*xDestroy) (void *));
```
With prepared context
```
struct lua_sql_func_info {
	int func_ref;
} func_info = {.func_ref = luaL_ref(L, LUA_REGISTRYINDEX);};

sql_create_function_v2(db, normalized_name, type,
                       func_arg_num,
                       is_deterministic ? SQL_DETERMINISTIC : 0,
                       func_info, lua_sql_call,
                       NULL, NULL, lua_sql_destroy);
```

A persistent Lua function has everything what ``sql_create_function_v2`` needs:
1. ``func_lua.base.def.name``,
2. ``func_lua.lua_ref``,
3. ``func_lua.base.def.is_deterministic``
4. ``func_lua.base.def.param_count`` (it is the size of the ``param_list`` array)

Therefore the internal ``box.internal.sql_create_function`` endpoint becomes
redundant and **will be deleted**.

**To control which Lua-functions are exported into SQL frontend, we would use `_func.exports` array.
Only a function that has `'SQL'` string in this array is exported in SQL.**

The SQL subsystem has an own function hash and a function definition object
- FuncDef.
It is inconsistent with Tarantool function definition cache  and it is not
scallable, so it will be reworked. SQL subsystem will use Tarantool
function hash.

#### SQL Built-ins

SQL defines some names for builtins. They are:
```
   TRIM, TYPEOF, PRINTF, UNICODE, CHAR, HEX, VERSION,
   QUOTE, REPLACE, SUBSTR, GROUP_CONCAT, JULIANDAY, DATE,
   TIME, DATETIME, STRFTIME, CURRENT_TIME, CURRENT_TIMESTAMP,
   CURRENT_DATE, LENGTH, POSITION, ROUND, UPPER, LOWER,
   IFNULL, RANDOM, CEIL, CEILING, CHARACTER_LENGTH,
   CHAR_LENGTH, FLOOR, MOD, OCTET_LENGTH, ROW_COUNT, COUNT,
   LIKE, ABS, EXP, LN, POWER, SQRT, SUM, TOTAL, AVG,
   RANDOMBLOB, NULLIF, ZEROBLOB, MIN, MAX, COALESCE, EVERY,
   EXISTS, EXTRACT, SOME, GREATER, LESSER, SOUNDEX
```
(the functions are currently not defined as Tarantool UDFs but may appear in
future)

To avoid name clash, we will reserve these names by adding entries for them
in `_func` system space. `_func.name` index collation will change
to use unicode_ci. All built-ins will be added to the bootstrap snapshot.

We also reserve service SQL method names
```
 _sql_stat_get, _sql_stat_push, _sql_stat_init
```
required to work with SQL analyze statistics.

#### Functions access from SQL
SQL may call all functions having 'SQL' in their exports array.

A new function `LUA` will be added to evaluate an arbitrary Lua string in
an SQL request.
```
box.execute('SELECT lua(\'return box.cfg.memtx_memory\')')
```
