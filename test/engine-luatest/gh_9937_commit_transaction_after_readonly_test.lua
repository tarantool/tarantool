local t = require('luatest')
local server = require('luatest.server')

local g = t.group('storage', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
    end, {cg.params.engine})
end)

g.test_try_commit_after_readonly = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        local inserted_before_ro = false
        local insert_fiber = fiber.new(function()
            box.begin()
            s:insert{1}
            inserted_before_ro = true
            box.ctl.wait_ro()
            box.commit()
        end)
        t.helpers.retrying({}, function()
            t.assert(inserted_before_ro)
        end)
        box.cfg{read_only = true}
        t.helpers.retrying({}, function()
            t.assert_equals(insert_fiber:status(), "dead")
        end)
        t.assert_equals(#s:select(), 0)
    end)
    t.helpers.retrying({}, function()
        t.assert(
            cg.server:grep_log("ER_READONLY.*")
        )
    end)
end

g.test_try_commit_local_temporary_after_readonly = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx')
    cg.server:exec(function(engine)
        local st = box.schema.space.create('test_temp',
                                           {engine = engine, temporary=true})
        st:create_index('pk')
        local sl = box.schema.space.create('test_local',
                                           {engine = engine, is_local=true})
        sl:create_index('pk')
        local fiber = require('fiber')
        local inserted_before_ro = false
        local insert_fiber = fiber.new(function()
            box.begin()
            st:insert{1}
            sl:insert{1}
            inserted_before_ro = true
            box.ctl.wait_ro()
            box.commit()
        end)
        t.helpers.retrying({}, function()
            t.assert(inserted_before_ro)
        end)
        box.cfg{read_only = true}
        t.helpers.retrying({}, function()
            t.assert_equals(insert_fiber:status(), "dead")
        end)
        t.assert_equals(#st:select(), 1)
        t.assert_equals(#st:select(), 1)
    end, {cg.params.engine})
end

g.after_each(function(cg)
    cg.server:update_box_cfg{read_only = false}
end)

g.after_all(function(cg)
    cg.server:drop()
end)
