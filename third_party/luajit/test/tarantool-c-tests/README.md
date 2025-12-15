# Tarantool C tests
This directory contains C tests for Tarantool's fork of LuaJIT.

They should test C API, some unit functionality, etc..

## How to start

The group of unit tests is declared like the following:
```c
void *t_state = NULL;
const struct test_unit tgroup[] = {
      test_unit_def(test_base),
      test_unit_def(test_simple),
};
return test_run_group(tgroup, t_state);
```

Each of the unit tests has the following definition:

```c
static int test_base(void *test_state)
```

## Assertions

The following assertions are defined in <test.h> to be used instead default
glibc `assert()`:
* `assert_true(cond)` -- check that condition `cond` is true.
* `assert_false(cond)` -- check that condition `cond` is false.
* `assert_ptr{_not}_equal(a, b)` -- check that pointer `a` is {not} equal to
  the `b`.
* `assert_int{_not}_equal(a, b)` -- check that `int` variable `a` is {not}
  equal to the `b`.
* `assert_sizet{_not}_equal(a, b)` -- check that `size_t` variable `a` is {not}
  equal to the `b`.
* `assert_double{_not}_equal(a, b)` -- check that two doubles are {not}
  **exactly** equal.
* `assert_str{_not}_equal(a, b)` -- check that `char *` variable `a` is {not}
  equal to the `b`.

## Directives

The following directives are supported to stop unit test or group of tests
earlier:
* `skip("reason")` -- skip the current test.
* `skip_all("reason")` -- skip the current group of tests.
* `todo("reason")` -- skip the current test marking as TODO.
* `bail_out("reason")` -- exit the entire process due to some emergency.

## Testing with Lua source code

Sometimes we need to test C API for modules, that show some Lua metrics (like
`luaM_metrics` or sysprof). In these cases, the required Lua script should be
named like the following: `<ctestname-script.lua>` and contains a table with a
bunch of Lua functions named the same as the unit C test, that uses this
function.

```lua
local M = {}
M.base = function()
  -- ...
end

M.test_simple = function()
  -- ...
end

return M
```

The script is loaded via `utils_load_aux_script(L, script_name)`. It loads the
file and place the table with functions at the top of Lua stack. Each function
is get from the table via `utils_get_aux_lfunc(L)` helper in the corresponding
test.
