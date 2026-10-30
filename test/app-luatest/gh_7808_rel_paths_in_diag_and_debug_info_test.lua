local netbox = require('net.box')
local t = require('luatest')
local tarantool = require('tarantool')

local g = t.group()

g.test_rel_paths_in_diag = function(_)
    local _, _, id, major_version = tarantool.build.compiler:find('^(%a+)-(%d+)%..+')
    major_version = tonumber(major_version)
    t.skip_if(id == 'Clang' and major_version < 9 or id == 'GNU' and major_version < 8,
              'compiler does not support `-fmacro-prefix-map` flag')

    local _, err = pcall(function() netbox.self:call('') end)
    t.assert_str_matches(err.trace[1].file, '^[^/].*')
end
