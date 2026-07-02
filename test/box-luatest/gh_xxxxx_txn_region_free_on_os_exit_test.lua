local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')
local justrun = require('luatest.justrun')
local utils = require('luatest.utils')

local g = t.group('gh_xxxxx')

g.before_all(function(cg)
    local id = ('%s-%s'):format('server', utils.generate_id())
    cg.workdir = fio.pathjoin(server.vardir, id)
    fio.mkdir(cg.workdir)
end)

g.test_txn_region_free_on_os_exit = function(cg)
    t.tarantool.skip_if_not_enterprise()
    local script = [[
        box.cfg{}

        box.schema.space.create('test', {
            engine = 'memcs',
            format = {{name = 'a', type = 'unsigned'}},
            field_count = 1
        })
        box.space.test:create_index('pk')

        box.begin()
        box.space.test:replace{5}
        os.exit()
    ]]
    local result = justrun.tarantool(cg.workdir, {}, {'-e', script},
                                     {nojson = true, quote_args = true})
    t.assert_equals(result.exit_code, 0)
end
