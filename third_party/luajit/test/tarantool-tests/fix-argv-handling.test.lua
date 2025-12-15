local tap = require('tap')
local test = tap.test('fix-argv-handling'):skipcond({
  ['DYLD_INSERT_LIBRARIES does not work on macOS'] = jit.os == 'OSX',
  ['Skipped for static build'] = os.getenv('LUAJIT_BUILDMODE') ~= 'dynamic',
})

test:plan(1)

-- XXX: Since the Linux kernel 5.18-rc1 release, `argv` is forced
-- to a single empty string if it is empty [1], so the issue is
-- not reproducible on new kernels.
--
-- luacheck: push no max_comment_line_length
--
-- [1]: https://lore.kernel.org/all/20220201000947.2453721-1-keescook@chromium.org/
--
-- luacheck: pop

local utils = require('utils')
local execlib = require('execlib')
local cmd = utils.exec.luabin(arg)

-- Start the LuaJIT with an empty argv array and mocked
-- `luaL_newstate`.
local output = execlib.empty_argv_exec(cmd)

-- Without the patch, the test fails with a segmentation fault
-- instead of returning an error.
test:like(output, 'cannot create state', 'correct argv handling')
test:done(true)
