local fio = require('fio')
local t = require('luatest')
local treegen = require('test.treegen')
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

-- Verify that work_dir is created at startup.
--
-- Also verify that if the directory is set as a relative path,
-- config:reload() doesn't attempt to create the same path inside
-- the work_dir.
--
-- IOW, if the work_dir is a/b/c, we shouldn't create a/b/c/a/b/c
-- at reload.
g.test_work_dir = function(g)
    local dir = treegen.prepare_directory(g, {}, {})

    local verify = loadstring(([[
        local fio = require('fio')

        local work_dir = fio.pathjoin('%s', 'a/b/c')
        assert(fio.path.is_dir(work_dir))
        assert(not fio.path.exists(fio.pathjoin(work_dir, 'a')))
    ]]):format(dir))

    local data_dir = fio.pathjoin(dir, '{{ instance_name }}')
    helpers.reload_success_case(g, {
        dir = dir,
        options = {
            ['process.work_dir'] = 'a/b/c',

            -- Place all the necessary directories and files into
            -- a location outside of the work_dir.
            --
            -- It allows to test the work_dir creation logic
            -- independently of creation of other directories.
            ['process.pid_file'] = fio.pathjoin(dir, '{{ instance_name }}.pid'),
            ['vinyl.dir'] = data_dir,
            ['wal.dir'] = data_dir,
            ['snapshot.dir'] = data_dir,
            ['console.socket'] = fio.pathjoin(dir,
                '{{ instance_name }}.control'),
            ['iproto.listen'] = fio.pathjoin(dir, '{{ instance_name }}.iproto'),
        },
        verify = verify,
        verify_2 = verify,
    })
end
