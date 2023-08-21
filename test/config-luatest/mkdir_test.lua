local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

g.test_nested_dirs = function(g)
    local verify = function()
        local fio = require('fio')

        local dirs = {'a/b', 'd/e/f', 'g/h/i', 'j/k/l', 'm/n', 'p/q'}
        for _, dir in ipairs(dirs) do
            t.assert_equals(fio.path.is_dir(dir), true)
        end
    end

    helpers.success_case(g, {
        options = {
            ['process.pid_file'] = 'a/b/c.pid',
            ['vinyl.dir'] = 'd/e/f',
            ['wal.dir'] = 'g/h/i',
            ['snapshot.dir'] = 'j/k/l',
            ['console.socket'] = 'm/n/o.socket',
            ['log.to'] = 'file',
            ['log.file'] = 'p/q/r.log',
        },
        verify = verify,
    })
end
