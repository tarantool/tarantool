local t = require('luatest')
local server = require('luatest.server')

local g = t.group('storage', {
    {engine = 'memtx'},
    {engine = 'vinyl'}
})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        memtx_use_mvcc_engine = true,
    }})
    cg.server:start()
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_try_commit_after_readonly = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local log = require('log')
        local s = box.space.test
        local inserted_before_ro = false
        local insert_fiber = fiber.new(function()
            box.begin()
            log.info("read-only status before yield is %s", box.info.ro)
            t.assert_not(box.info.ro)
            s:insert{1}
            inserted_before_ro = true
            t.helpers.retrying({delay = 0.1}, function()
                t.assert(box.info.ro)
            end)
            log.info("read-only status after yield is %s", box.info.ro)
            box.commit()
        end)
        t.helpers.retrying({delay = 0.1}, function()
            t.assert(inserted_before_ro)
        end)
        box.cfg{read_only = true}
        t.helpers.retrying({delay = 0.1}, function()
            t.assert_equals(insert_fiber:status(), "dead")
        end)
        t.assert_equals(#s:select(), 0)
    end)
    t.helpers.retrying({delay = 0.1}, function()
        t.assert(cg.server:grep_log(
            "ER_READONLY.*the node became read%-only.*", 65536
        ))
    end)
end
