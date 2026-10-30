local t = require('luatest')
local server = require('luatest.server')

local g = t.group('storage', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:update_box_cfg{read_only = false}
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_try_commit_after_readonly = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
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
    end, {cg.params.engine})
end

local function commit_success_template(cg, space_opt)
    cg.server:exec(function(space_opt)
        local fiber = require('fiber')
        local s = box.schema.space.create('test', space_opt)
        s:create_index('pk')
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
        local ok, _ = insert_fiber:join()
        t.assert(ok)
        t.assert_equals(#s:select(), 1)
    end, {space_opt})
end

g.test_try_commit_local_after_readonly = function(cg)
    commit_success_template(cg, {engine = cg.params.engine, is_local = true})
end

g.test_try_commit_temporary_after_readonly = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx')
    commit_success_template(cg, {engine = cg.params.engine, temporary = true})
end
