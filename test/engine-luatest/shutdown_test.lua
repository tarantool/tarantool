local server = require('luatest.server')
local utils = require('luatest.utils')
local justrun = require('luatest.justrun')
local fio = require('fio')
local fiber = require('fiber')
local t = require('luatest')

local g = t.group('engine', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

local function test_no_hang_on_shutdown(server)
    local channel = fiber.channel()
    fiber.create(function()
        server:stop()
        channel:put('finished')
    end)
    t.assert(channel:get(60) ~= nil)
end

-- Test we can interrupt loop on secondary index creation.
g.test_shutdown_secondary_index_build = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        box.schema.create_space('test', {engine = engine})
        box.space.test:create_index('pk')
        fiber.set_slice(100)
        box.begin()
        for i=1,10000 do
            box.space.test:insert{i, i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_BUILD_INDEX_TIMEOUT', 0.01)
        fiber.new(function()
            box.space.test:create_index('sk', {parts = {2}})
        end)
    end, {cg.params.engine})
    test_no_hang_on_shutdown(cg.server)
end

-- Luatest server currently does not allow to check process exit code.
local g_crash = t.group('crash', {{engine = 'memtx'}, {engine = 'vinyl'}})

g_crash.before_each(function(cg)
    local id = ('%s-%s'):format('server', utils.generate_id())
    cg.workdir = fio.pathjoin(server.vardir, id)
    fio.mkdir(cg.workdir)
end)

-- Test shutdown when there is an active transaction.
g_crash.test_shutdown_with_active_txn = function(cg)
    local script_base = [[
        box.cfg{memtx_use_mvcc_engine = true}

        box.schema.space.create('test', {engine = '%s'})
        box.space.test:create_index('pk')
        box.begin()
        box.space.test:replace{5}
        box.space.test:get(10)
        box.space.test:select()
        os.exit()
    ]]
    local script = script_base:format(cg.params.engine)

    local result = justrun.tarantool(cg.workdir, {}, {'-e', script},
                                    {nojson = true, quote_args = true})
    t.assert_equals(result.exit_code, 0)
end
