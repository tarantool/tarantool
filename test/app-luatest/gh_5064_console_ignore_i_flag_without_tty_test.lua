local t = require('luatest')

local g = t.group()

local result_str =  [[tarantool> 42
---
- 42
...

tarantool> ]]

local TARANTOOL_PATH = arg[-1]

g.test_console_ignores_i_flag_without_tty = function()
    -- Unset XDG_STATE_HOME and HOME environment variables to skip readline
    -- history file write. Otherwise it clogs user's own history.
    --
    -- Suppress prompt changes by custom readline config to simplify assertions
    -- using the INPUTRC environment variable.
    local cmd = ("unset XDG_STATE_HOME; unset HOME; " ..
        "printf '42\n' | INPUTRC=non_existent_file %s " ..
        "-i 2>/dev/null"):format(TARANTOOL_PATH)
    local fh = io.popen(cmd, 'r')

    -- Readline on CentOS 7 produces \e[?1034h escape sequence before tarantool> prompt, remove it.
    local result = fh:read('*a'):gsub('\x1b%[%?1034h', '')

    fh:close()
    t.assert_equals(result, result_str)
end
