local tap = require('tap')
local utils = require('utils')

local test = tap.test('lj-586-debug-non-string-error')
test:plan(1)

-- XXX: This is quite an exotic test case in that point of view
-- that testing the debug interactive interface always ends with
-- sending commands to another instance via stdin. However, the
-- module with test helpers lacks the suitable routine.
-- `utils.makecmd()` doesn't fit for this, since `debug.debug()`
-- captures `io.stdin` and waits at `fgets()` in debug busy loop.
-- As it's already mentioned, such tests are not usual, so there
-- is no need to introduce a new helper to utils module (at least
-- for now). Hence, we use `utils.luacmd()` function to obtain the
-- interpreter command, run `debug.debug()` with it, send two
-- commands via pipeline to the launched process, capture its
-- stdout and stderr and test the resulting output against the
-- expected one.
--
-- Debugger prompt.
local ldb = 'lua_debug> '
local magic = 42
local luabin = utils.exec.luacmd(arg)
local stderr = {
  -- XXX: The first debugger prompt printed at the start.
  ldb,
  -- The resulting error yielded by debugger console.
  '(error object is not a string)\n',
  -- XXX: New debugger prompt printed after the `error()` command.
  ldb,
  -- XXX: The last debugger prompt after `print()` command.
  ldb,
}

-- XXX: Since stderr is merged to stdout, debugger prompts and
-- errors are yielded before the magic given to `print()` command
local expected = table.concat(stderr)..magic

local cmd = ([[cat <<EOF | %s -e 'debug.debug()' 2>&1
error({})
print(%d)
EOF]]):format(luabin, magic)

local res = io.popen(cmd):read('*all'):gsub('%s+$', '')
test:ok(res == expected, 'handle non-string error in debug.debug()')
test:done(true)
