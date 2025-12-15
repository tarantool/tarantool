local tap = require('tap')
local test = tap.test('c-library-path-length')
test:plan(2)

-- It doesn't really matter how long that
-- string is, if it is longer than 4096.
local long_path = string.rep('/path', 1024)
package.cpath = long_path

local res, err = package.loadlib(long_path, 'func')
test:ok(res == nil, 'loaded library with a too large path')
test:like(err, 'path too long', 'incorrect error')

test:done(true)
