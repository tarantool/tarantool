local tap = require('tap')

-- Test file to demonstrate fd leakage in case of OOM during the
-- `loadfile()` call. The test fails before the patch when run
-- under Valgrind with the `--track-fds=yes` option.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/1249.
local test = tap.test('lj-1249-loadfile-fd-leak')

test:plan(4)

local allocinject = require('allocinject')

allocinject.enable_null_alloc()

-- Just use the /dev/null as the surely available file.
-- OOM is due to the creation of the string "@/dev/null" as the
-- filename to be stored.
local res, errmsg = pcall(loadfile, '/dev/null')

allocinject.disable()

-- Sanity checks.
test:ok(not res, 'correct status, OOM on filename creation')
test:like(errmsg, 'not enough memory',
          'correct error message, OOM on filename creation')

-- Now try to read the directory. It can be opened but not read as
-- a file on Linux-like systems.

-- On macOS and BSD-like systems, the content of the directory may
-- be read and contain some internal data, which we are not
-- interested in.
test:skipcond({
  ['Disabled on non-Linux systems'] = jit.os ~= 'Linux',
})

local DIRNAME = '/dev'
local GCSTR_OBJSIZE = 24

-- Now the OOM error should be obtained on the creation of the
-- error message that the given file (or directory) is not
-- readable. But the string for the file name should be allocated
-- without OOM, so set the corresponding limit for the string
-- object.
-- Don't forget to count the leading '@' and trailing '\0'.
allocinject.enable_null_limited_alloc(#DIRNAME + GCSTR_OBJSIZE + 1 + 1)

-- Error since can't read the directory (but actually the OOM on
-- parsing preparation is raised before, due to allocation limit).
res, errmsg = pcall(loadfile, DIRNAME)

allocinject.disable()

-- Sanity checks.
test:ok(not res, 'correct status, OOM on error message creation')
test:like(errmsg, 'not enough memory',
          'correct error message, OOM on error message creation')

test:done(true)
