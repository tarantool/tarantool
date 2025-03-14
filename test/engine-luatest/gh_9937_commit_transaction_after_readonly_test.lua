local t = require('luatest')
local server = require('luatest.server')

local g = t.group('storage', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
        local sl = box.schema.space.create('test_local',
                                           {engine = engine, is_local=true})
        sl:create_index('pk')
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
        insert_fiber:set_joinable(true)
        t.helpers.retrying({}, function()
            t.assert(inserted_before_ro)
        end)
        box.cfg{read_only = true}
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.READONLY,
        }, function()
            local ok, err = insert_fiber:join()
            if not ok then
                error(err)
            end
        end)
        t.assert_equals(#s:select(), 0)
    end)
end

g.test_try_commit_local_after_readonly = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local sl = box.space.test_local
        local inserted_before_ro = false
        local insert_fiber = fiber.new(function()
            box.begin()
            sl:insert{1}
            inserted_before_ro = true
            box.ctl.wait_ro()
            box.commit()
        end)
        insert_fiber:set_joinable(true)
        t.helpers.retrying({}, function()
            t.assert(inserted_before_ro)
        end)
        box.cfg{read_only = true}
        insert_fiber:join()
        t.assert_equals(#sl:select(), 1)
    end)
end

g.test_try_commit_temporary_after_readonly = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx')
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local st = box.schema.space.create('test_temp',
                                           {engine = engine, temporary=true})
        st:create_index('pk')
        local inserted_before_ro = false
        local insert_fiber = fiber.new(function()
            box.begin()
            st:insert{1}
            inserted_before_ro = true
            box.ctl.wait_ro()
            box.commit()
        end)
        insert_fiber:set_joinable(true)
        t.helpers.retrying({}, function()
            t.assert(inserted_before_ro)
        end)
        box.cfg{read_only = true}
        insert_fiber:join()
        t.assert_equals(#st:select(), 1)
    end, {cg.params.engine})
end

g.after_each(function(cg)
    cg.server:update_box_cfg{read_only = false}
end)

g.after_all(function(cg)
    cg.server:drop()
end)
