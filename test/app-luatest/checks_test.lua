-- luatest installs external checks module.
-- After https://github.com/tarantool/checks/pull/54,
-- external checks will override built-in one, yet
-- we want to test built-in one here.
local rock_utils = require('third_party.checks.test.rock_utils')
rock_utils.remove_override('checks')
rock_utils.assert_builtin('checks')

require('third_party.checks.test.test')
